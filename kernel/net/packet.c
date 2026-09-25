#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/net/packet.h>
#include <kernel/panic.h>

/* All owners, including queued packets, count against this BSP/IF=0 budget. */
static size_t live_packets;

struct net_packet *net_packet_allocate(size_t length)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!length || length > NET_PACKET_MAX_BYTES || live_packets == NET_PACKET_LIMIT) {
    return NULL;
  }

  struct net_packet *packet = kmalloc(sizeof(*packet) + length);
  if (!packet) {
    return NULL;
  }
  packet->length = length;
  packet->tcp_generation = 0;
  ++live_packets;
  return packet;
}

void net_packet_release(struct net_packet *packet)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!packet) {
    return;
  }
  KASSERT(live_packets);
  --live_packets;
  kfree(packet);
}
