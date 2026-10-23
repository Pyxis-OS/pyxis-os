#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/lwip.h>
#include <kernel/task.h>
#include <lwip/init.h>
#include <lwip/ip4.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <caelum_hooks.h>
#include "connection.h"
#include "stream.h"

#define TCP_PACKET_LIFETIME_MS 3000
#define NS_PER_MS UINT64_C(1000000)

/* These project Caelum's addresses into lwIP, not a second route database.
 * Both use Caelum's IP transmitter, including deferred local delivery. */
static struct netif local_projection;
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
  struct netif *ingress = ip_current_input_netif();
  enum net_result result = ingress ?
      net_ipv4_route_on(ingress->state, from, to, &route) :
      net_ipv4_route(from, to, &route);
  return result == NET_OK ? route.interface->tcp_projection : NULL;
}

int caelum_lwip_accept(struct netif *interface, const ip4_addr_t *destination)
{
  net_worker_assert_context();
  uint32_t address = lwip_ntohl(ip4_addr_get_u32(destination));
  const struct net_interface *native = interface->state;
  if (native == &net_loopback) {
    return net_ipv4_is_loopback(address);
  }
  return address && address == net_ipv4_address(native);
}

static err_t submit_packet(struct netif *interface, struct pbuf *buffer,
    const ip4_addr_t *destination)
{
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
  enum net_result result = net_ipv4_submit(interface->state, packet,
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
  const struct net_interface *native = interface->state;
  struct ipv4_route route;
  uint32_t from = lwip_ntohl(ip4_addr_get_u32(source));
  uint32_t to = lwip_ntohl(ip4_addr_get_u32(destination));
  if (net_ipv4_route_on(native, from, to, &route) != NET_OK ||
      native->tcp_projection != interface) {
    return ERR_RTE;
  }
  if (pcb) {
    struct tcp_connection *connection = tcp_connection_from_pcb(pcb);
    if (connection->interface != native || connection->address_epoch != route.address_epoch) {
      return ERR_RTE;
    }
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
  const struct net_interface *native = interface->state;
  interface->name[1] = native == &net_loopback ? 'l' : 'n';
  interface->mtu = native->mtu;
  interface->output = submit_packet;
  return ERR_OK;
}

void net_lwip_refresh_address(struct net_interface *interface)
{
  net_worker_assert_context();
  KASSERT(interface->tcp_projection);
  tcp_connections_changed();
  tcp_calls_changed();
  tcp_connections_service();
  ip4_addr_t address = { .addr = lwip_htonl(net_ipv4_address(interface)) };
  /* Native epoch checks reject stale bindings before further native effects. */
  netif_set_ipaddr(interface->tcp_projection, &address);
}

size_t net_lwip_projection_size(void)
{
  return sizeof(struct netif);
}

void net_lwip_publish_interface(struct net_interface *interface)
{
  net_worker_assert_context();
  ip4_addr_t address = {.addr = lwip_htonl(net_ipv4_address(interface))};
  KASSERT(netif_add(interface->tcp_projection, &address, NULL, NULL, interface,
      initialize_interface, ip4_input));
  netif_set_up(interface->tcp_projection);
  netif_set_link_up(interface->tcp_projection);
}

void net_lwip_init(void)
{
  net_worker_assert_context();
  lwip_init();
  tcp_connections_init();

  net_loopback.tcp_projection = &local_projection;
  net_lwip_publish_interface(&net_loopback);
  kinfo("net: lwIP TCP packet bridge and connection service ready\n");
}

void net_lwip_receive(const struct net_interface *interface, const uint8_t *data, size_t length)
{
  net_worker_assert_context();
  KASSERT(length >= IPV4_HEADER_SIZE && length <= NET_PACKET_MAX_BYTES);
  KASSERT(interface->tcp_projection && interface->tcp_projection->state == interface);
  ++bridge_stats.received;
  bool previous = caelum_lwip_set_protocol_allocations(true);
  struct pbuf *packet = pbuf_alloc(PBUF_RAW, length, PBUF_RAM);
  if (!packet) {
    ++bridge_stats.no_memory;
    caelum_lwip_set_protocol_allocations(previous);
    return;
  }
  if (pbuf_take(packet, data, length) != ERR_OK) {
    pbuf_free(packet);
    caelum_lwip_set_protocol_allocations(previous);
    return;
  }
  /* ip4_input consumes the copy; caller still owns the original RX storage. */
  ip4_input(packet, interface->tcp_projection);
  caelum_lwip_set_protocol_allocations(previous);
  /* Window-only ACKs need not invoke a native sent/receive callback. */
  tcp_calls_changed();
  tcp_connections_changed();
}

void net_lwip_service(void)
{
  net_worker_assert_context();
  /* Only TCP's cyclic timer is enabled; it reschedules relative to now, so a
   * delayed worker cannot accumulate a catch-up loop of expired TCP timers. */
  tcp_connections_service();
  bool previous = caelum_lwip_set_protocol_allocations(true);
  sys_check_timeouts();
  caelum_lwip_set_protocol_allocations(previous);
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
