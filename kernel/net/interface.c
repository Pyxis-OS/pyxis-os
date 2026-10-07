#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/net/interface.h>
#include <kernel/net/arp.h>
#include <kernel/net/config.h>
#include <kernel/net/udp.h>
#include <kernel/net/tcp.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/lwip.h>
#include <kernel/net/echo.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/net/driver.h>
#include <kernel/net/log_udp.h>
#include <stdatomic.h>

#define NET_WORK_BUDGET 8

const struct net_interface net_loopback = {
  .name = "lo",
  .mtu = NET_PACKET_MAX_BYTES,
};

/* BSP/IF=0 serializes packet queue publication.
 * The queue owns its pointers; interface metadata survives until reboot. */
static struct {
  struct net_packet *packets[NET_RECEIVE_QUEUE_LIMIT];
  size_t head, count;
  uint64_t submitted, received, queue_full;
} loopback;

/* AP echo submissions also wake this worker. Notification is remembered across
 * the gap between checking protocol work and publishing the worker wait. */
static atomic_bool worker_ready, worker_locked;
static struct task_wait *worker_wait;
static bool worker_notified;

static void network_worker(void *argument);

void net_worker_assert_context(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  KASSERT((flags & RFLAGS_INTERRUPT_ENABLE) && kernel_task_is_current(network_worker, NULL));
  cpu_restore_interrupts(flags);
}

static void lock_worker(void)
{
  while (atomic_exchange_explicit(&worker_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_worker(void)
{
  atomic_store_explicit(&worker_locked, false, memory_order_release);
}

bool net_worker_available(void)
{
  return atomic_load_explicit(&worker_ready, memory_order_acquire);
}

void net_worker_notify(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_worker();
  worker_notified = true;
  if (worker_wait) {
    struct task_wait *wait = worker_wait;
    worker_wait = NULL;
    task_wait_wake(wait);
  }
  unlock_worker();
}

enum net_result net_transmit(const struct net_interface *interface,
    struct net_packet *packet)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (interface != &net_loopback || !packet || !packet->length ||
      packet->length > net_loopback.mtu) {
    return NET_INVALID;
  }
  if (!net_worker_available()) {
    return NET_UNAVAILABLE;
  }
  if (loopback.count == NET_RECEIVE_QUEUE_LIMIT) {
    ++loopback.queue_full;
    return NET_QUEUE_FULL;
  }

  size_t tail = (loopback.head + loopback.count) % NET_RECEIVE_QUEUE_LIMIT;
  loopback.packets[tail] = packet;
  ++loopback.count;
  ++loopback.submitted;
  net_worker_notify();
  return NET_OK;
}

void net_loopback_cancel_tcp(uint64_t generation)
{
  net_worker_assert_context();
  KASSERT(generation);
  uint64_t flags = cpu_save_interrupts();
  size_t kept = 0;
  for (size_t i = 0; i < loopback.count; ++i) {
    size_t index = (loopback.head + i) % NET_RECEIVE_QUEUE_LIMIT;
    struct net_packet *packet = loopback.packets[index];
    loopback.packets[index] = NULL;
    if (packet->tcp_generation == generation) {
      net_packet_release(packet);
    } else {
      loopback.packets[(loopback.head + kept) % NET_RECEIVE_QUEUE_LIMIT] = packet;
      ++kept;
    }
  }
  loopback.count = kept;
  cpu_restore_interrupts(flags);
}

/* Sole BSP worker, IF=1. Packet ownership remains serialized by BSP/IF=0. */
static struct net_packet *next_packet(void)
{
  uint64_t flags = cpu_save_interrupts();
  struct net_packet *packet = NULL;
  if (loopback.count) {
    packet = loopback.packets[loopback.head];
    loopback.packets[loopback.head] = NULL;
    loopback.head = (loopback.head + 1) % NET_RECEIVE_QUEUE_LIMIT;
    --loopback.count;
  }
  cpu_restore_interrupts(flags);
  return packet;
}

static void wait_for_work(void)
{
  uint64_t deadline;
  bool timed = net_lwip_next_deadline(&deadline);
  uint64_t tcp_deadline;
  if (net_tcp_next_deadline(&tcp_deadline) && (!timed || tcp_deadline < deadline)) {
    deadline = tcp_deadline;
    timed = true;
  }
  uint64_t flags = cpu_save_interrupts();
  uint64_t echo_deadline;
  if (net_echo_next_deadline(&echo_deadline) && (!timed || echo_deadline < deadline)) {
    deadline = echo_deadline;
    timed = true;
  }
  uint64_t udp_deadline;
  if (net_udp_next_deadline(&udp_deadline) && (!timed || udp_deadline < deadline)) {
    deadline = udp_deadline;
    timed = true;
  }
  uint64_t transport_deadline;
  if (net_driver_next_deadline(&transport_deadline) && (!timed || transport_deadline < deadline)) {
    deadline = transport_deadline;
    timed = true;
  }
  uint64_t log_deadline;
  if (net_log_udp_next_deadline(&log_deadline) && (!timed || log_deadline < deadline)) {
    deadline = log_deadline;
    timed = true;
  }
  uint64_t arp_deadline;
  if (net_arp_next_deadline(&arp_deadline) && (!timed || arp_deadline < deadline)) {
    deadline = arp_deadline;
    timed = true;
  }
  lock_worker();
  if (worker_notified || loopback.count || (timed && task_deadline_expired(deadline))) {
    worker_notified = false;
    unlock_worker();
    cpu_restore_interrupts(flags);
    return;
  }
  struct task_wait *wait = task_wait_prepare();
  worker_wait = wait;
  unlock_worker();

  if (!timed) {
    task_wait_sleep(wait);
  } else {
    task_wait_sleep_until(wait, deadline);
  }
  lock_worker();
  worker_wait = NULL;
  unlock_worker();
  cpu_restore_interrupts(flags);
}

static void receive_packet(struct net_packet *packet)
{
  ++loopback.received;
  net_ipv4_receive(&net_loopback, false, packet->data, packet->length);

  uint64_t flags = cpu_save_interrupts();
  net_packet_release(packet);
  cpu_restore_interrupts(flags);
}

static void network_worker(void *argument)
{
  (void)argument;
  net_lwip_init();
  for (;;) {
    bool transport_busy = net_driver_service();
    bool serviced = net_config_service();
    serviced |= net_log_udp_service();
    serviced |= net_udp_service();
    serviced |= net_echo_service();
    net_lwip_service();
    net_arp_service();
    unsigned handled = 0;
    while (handled < NET_WORK_BUDGET) {
      struct net_packet *packet = next_packet();
      if (!packet) {
        break;
      }
      receive_packet(packet);
      ++handled;
    }
    serviced |= net_tcp_service();
    if (transport_busy || serviced || handled == NET_WORK_BUDGET) {
      /* A past deadline yields without imposing an extra timer delay. */
      kernel_task_sleep_until(0);
    } else {
      wait_for_work();
    }
  }
}

enum mm_result net_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !net_worker_available());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  enum mm_result result = kernel_task_create(network_worker, NULL);
  if (result == MM_OK) {
    atomic_store_explicit(&worker_ready, true, memory_order_release);
    net_lwip_identity_start();
    klog("net: lo 127.0.0.1/8 MTU=%zu, IPv4/ICMP/UDP worker ready\n", net_loopback.mtu);
  }
  return result;
}
