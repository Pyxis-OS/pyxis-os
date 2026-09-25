#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/lwip.h>
#include <kernel/task.h>
#include <lwip/init.h>
#include <lwip/ip4.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/prot/ip4.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <caelum_hooks.h>
#include "../wire.h"
#include "connection.h"

#define TCP_PACKET_LIFETIME_MS 3000
#define NS_PER_MS UINT64_C(1000000)

/* These project Caelum's addresses into lwIP, not a second route database.
 * Both use Caelum's IP transmitter, including deferred local delivery. */
static struct netif local_interface, assigned_interface;
static bool output_active;
static uint64_t output_generation;

static struct {
  uint64_t received, no_memory, submitted, rejected;
} bridge_stats;

struct netif *caelum_lwip_route(const ip4_addr_t *source, const ip4_addr_t *destination)
{
  net_worker_assert_context();
  struct ipv4_route route;
  uint32_t from = source ? lwip_ntohl(ip4_addr_get_u32(source)) : 0;
  uint32_t to = lwip_ntohl(ip4_addr_get_u32(destination));
  if (net_ipv4_route(from, to, &route) != NET_OK) {
    return NULL;
  }
  return net_ipv4_is_loopback(route.source) ? &local_interface : &assigned_interface;
}

int caelum_lwip_accept(struct netif *interface, const ip4_addr_t *destination)
{
  net_worker_assert_context();
  uint32_t address = lwip_ntohl(ip4_addr_get_u32(destination));
  if (net_ipv4_is_loopback(address)) {
    return interface == &local_interface;
  }
  return interface == &assigned_interface && address && address == net_ipv4_address();
}

static err_t submit_packet(struct netif *interface, struct pbuf *buffer,
    const ip4_addr_t *destination)
{
  (void)interface;
  (void)destination;
  net_worker_assert_context();
  KASSERT(output_active);
  if (buffer->tot_len < IPV4_HEADER_SIZE || buffer->tot_len > NET_PACKET_MAX_BYTES) {
    return ERR_BUF;
  }

  uint64_t flags = cpu_save_interrupts();
  struct net_packet *packet = net_packet_allocate(buffer->tot_len);
  cpu_restore_interrupts(flags);
  if (!packet) {
    ++bridge_stats.no_memory;
    return ERR_MEM;
  }
  packet->tcp_generation = output_generation;
  if (pbuf_copy_partial(buffer, packet->data, buffer->tot_len, 0) != buffer->tot_len) {
    flags = cpu_save_interrupts();
    net_packet_release(packet);
    cpu_restore_interrupts(flags);
    return ERR_BUF;
  }

  struct ipv4_completion completion = { IPV4_NOTIFY_TCP, output_generation };
  enum net_result result = net_ipv4_submit(packet,
      task_deadline_after_ms(TCP_PACKET_LIFETIME_MS), completion);
  if (result == NET_OK) {
    ++bridge_stats.submitted;
    return ERR_OK;
  }

  flags = cpu_save_interrupts();
  net_packet_release(packet);
  cpu_restore_interrupts(flags);
  ++bridge_stats.rejected;
  switch (result) {
  case NET_QUEUE_FULL:
  case NET_NO_MEMORY: return ERR_MEM;
  case NET_NO_ROUTE:
  case NET_UNAVAILABLE: return ERR_RTE;
  case NET_TIMED_OUT: return ERR_TIMEOUT;
  default: return ERR_VAL;
  }
}

/* Called around each TCP IP submission, including control packets with no PCB.
 * Context cannot escape this call: loopback never invokes input inline. */
err_t caelum_lwip_output(const struct tcp_pcb *pcb, struct pbuf *packet,
    const ip4_addr_t *source, const ip4_addr_t *destination,
    u8_t ttl, u8_t tos, struct netif *interface)
{
  net_worker_assert_context();
  KASSERT(!output_active);
  if (caelum_lwip_route(source, destination) != interface) {
    return ERR_RTE;
  }
  output_active = true;
  output_generation = pcb ? tcp_connection_generation(pcb) : 0;
  err_t result = ip4_output_if(packet, source, destination, ttl, tos, IPV4_PROTOCOL_TCP, interface);
  output_generation = 0;
  output_active = false;
  return result;
}

static err_t initialize_interface(struct netif *interface)
{
  interface->name[0] = 'c';
  interface->name[1] = interface == &local_interface ? 'l' : 'n';
  interface->mtu = NET_PACKET_MAX_BYTES;
  interface->output = submit_packet;
  return ERR_OK;
}

void net_lwip_refresh_address(void)
{
  net_worker_assert_context();
  tcp_connections_service();
  ip4_addr_t address = { .addr = lwip_htonl(net_ipv4_address()) };
  /* lwIP invalidates PCBs bound to an old address before publishing the new one. */
  netif_set_ipaddr(&assigned_interface, &address);
}

void net_lwip_init(void)
{
  net_worker_assert_context();
  lwip_init();
  tcp_connections_init();

  ip4_addr_t address = { .addr = lwip_htonl(IPV4_LOOPBACK_ADDRESS) };
  KASSERT(netif_add(&local_interface, &address, NULL, NULL, NULL,
      initialize_interface, ip4_input));
  KASSERT(netif_add(&assigned_interface, NULL, NULL, NULL, NULL,
      initialize_interface, ip4_input));
  netif_set_up(&local_interface);
  netif_set_link_up(&local_interface);
  netif_set_up(&assigned_interface);
  netif_set_link_up(&assigned_interface);
  net_lwip_refresh_address();
  klog("net: lwIP TCP packet bridge and connection service ready\n");
}

void net_lwip_receive(const uint8_t *data, size_t length)
{
  net_worker_assert_context();
  KASSERT(length >= IPV4_HEADER_SIZE && length <= NET_PACKET_MAX_BYTES);
  ++bridge_stats.received;
  struct pbuf *packet = pbuf_alloc(PBUF_RAW, length, PBUF_RAM);
  if (!packet) {
    ++bridge_stats.no_memory;
    return;
  }
  if (pbuf_take(packet, data, length) != ERR_OK) {
    pbuf_free(packet);
    return;
  }
  struct netif *interface = net_ipv4_is_loopback(net_read_u32(data + offsetof(struct ip_hdr, dest))) ?
      &local_interface : &assigned_interface;
  /* ip4_input consumes the copy; caller still owns the original RX storage. */
  ip4_input(packet, interface);
}

void net_lwip_service(void)
{
  net_worker_assert_context();
  /* Only TCP's cyclic timer is enabled; it reschedules relative to now, so a
   * delayed worker cannot accumulate a catch-up loop of expired TCP timers. */
  tcp_connections_service();
  sys_check_timeouts();
  tcp_connections_service();
}

bool net_lwip_next_deadline(uint64_t *deadline)
{
  net_worker_assert_context();
  uint64_t now = arch_monotonic_ns();
  u32_t delay = sys_timeouts_sleeptime();
  bool found = tcp_connections_next_deadline(deadline);
  if (delay != SYS_TIMEOUTS_SLEEPTIME_INFINITE) {
    uint64_t nanoseconds = (uint64_t)delay * NS_PER_MS;
    uint64_t timer = now > UINT64_MAX - nanoseconds ? UINT64_MAX : now + nanoseconds;
    if (!found || timer < *deadline) {
      *deadline = timer;
    }
    found = true;
  }
  return found;
}
