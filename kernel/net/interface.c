#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/log.h>
#include <kernel/net/interface.h>
#include <kernel/net/ipv4.h>
#include <kernel/panic.h>
#include <kernel/task.h>

#define NET_WORK_BUDGET 8

const struct net_interface net_loopback = {
  .name = "lo",
  .mtu = NET_PACKET_MAX_BYTES,
};

/* BSP/IF=0 serializes queue publication and the worker's wait handoff.
 * The queue owns its pointers; interface metadata survives until reboot. */
static struct {
  struct net_packet *packets[NET_RECEIVE_QUEUE_LIMIT];
  size_t head, count;
  struct task_wait *wait;
  bool ready;
  uint64_t submitted, received, queue_full;
} loopback;

enum net_result net_transmit(const struct net_interface *interface,
    struct net_packet *packet)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (interface != &net_loopback || !packet || !packet->length ||
      packet->length > net_loopback.mtu) {
    return NET_INVALID;
  }
  if (!loopback.ready) {
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
  if (loopback.wait) {
    struct task_wait *wait = loopback.wait;
    loopback.wait = NULL;
    task_wait_wake(wait);
  }
  return NET_OK;
}

/* Sole BSP worker, IF=1. Check and publish the wait with producer access
 * excluded, so a packet arriving before park cannot leave the worker asleep. */
static struct net_packet *next_packet(void)
{
  uint64_t flags = cpu_save_interrupts();
  while (!loopback.count) {
    struct task_wait *wait = task_wait_prepare();
    loopback.wait = wait;
    task_wait_sleep(wait);
    loopback.wait = NULL;
  }

  struct net_packet *packet = loopback.packets[loopback.head];
  loopback.packets[loopback.head] = NULL;
  loopback.head = (loopback.head + 1) % NET_RECEIVE_QUEUE_LIMIT;
  --loopback.count;
  cpu_restore_interrupts(flags);
  return packet;
}

static void receive_packet(struct net_packet *packet)
{
  ++loopback.received;
  net_ipv4_receive(packet);

  uint64_t flags = cpu_save_interrupts();
  net_packet_release(packet);
  cpu_restore_interrupts(flags);
}

static void network_worker(void *argument)
{
  (void)argument;
  unsigned handled = 0;
  for (;;) {
    struct net_packet *packet = next_packet();
    receive_packet(packet);
    if (++handled == NET_WORK_BUDGET) {
      handled = 0;
      /* A past deadline yields without imposing an extra timer delay. */
      kernel_task_sleep_until(0);
    }
  }
}

enum mm_result net_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !loopback.ready);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  enum mm_result result = kernel_task_create(network_worker, NULL);
  if (result == MM_OK) {
    loopback.ready = true;
    klog("net: lo 127.0.0.1/8 MTU=%zu, IPv4/ICMP worker ready\n", net_loopback.mtu);
  }
  return result;
}
