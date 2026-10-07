#ifndef KERNEL_NET_PANIC_TX_H
#define KERNEL_NET_PANIC_TX_H

#include <arch/cpu.h>
#include <kernel/net/log_udp.h>
#include <stdatomic.h>
#include <stdbool.h>

#define NET_PANIC_GATE_BUSY UINT64_C(1)
#define NET_PANIC_GATE_FATAL UINT64_C(2)
#define NET_PANIC_OPERATION_SHIFT 2
#define NET_PANIC_CPU_SHIFT 32
#define NET_PANIC_HANDOFF_POLLS 100000
#define NET_PANIC_COMPLETION_POLLS 1000000

struct net_panic_gate {
  atomic_uint_fast64_t state;
};

/* Driver hardware mutations run with IF=0. The operation and CPU are published
 * together, before touching DMA/MMIO. Do not span allocation, logging, protocol
 * callbacks or sleeping. Fatal ownership never returns to normal networking. */
static inline bool net_panic_gate_enter(struct net_panic_gate *gate, unsigned operation)
{
  if (!net_log_udp_enabled()) {
    return true;
  }
  /* Fatal entry may happen before the selected controller is published.
   * Refuse a later activation even when there was no driver gate to claim. */
  if (net_log_udp_panicking()) {
    return false;
  }
  uint_fast64_t expected = 0;
  uint_fast64_t state = (uint64_t)net_log_udp_worker_cpu() << NET_PANIC_CPU_SHIFT |
      (uint64_t)operation << NET_PANIC_OPERATION_SHIFT | NET_PANIC_GATE_BUSY;
  return atomic_compare_exchange_strong_explicit(&gate->state, &expected, state,
      memory_order_acquire, memory_order_relaxed);
}

static inline void net_panic_gate_leave(struct net_panic_gate *gate)
{
  if (!net_log_udp_enabled()) {
    return;
  }
  atomic_fetch_and_explicit(&gate->state, NET_PANIC_GATE_FATAL, memory_order_release);
}

static inline bool net_panic_gate_closed(const struct net_panic_gate *gate)
{
  return net_log_udp_panicking() ||
      (atomic_load_explicit(&gate->state, memory_order_acquire) & NET_PANIC_GATE_FATAL);
}

/* Same-CPU interruption cannot resume: report the interrupted operation so its
 * driver can recover only the publication windows it explicitly understands.
 * A different CPU waits only for the bounded hardware section, then gives up. */
static inline bool net_panic_gate_take(struct net_panic_gate *gate, unsigned *interrupted)
{
  if (!net_log_udp_enabled()) {
    return false;
  }
  uint_fast64_t state = atomic_fetch_or_explicit(&gate->state, NET_PANIC_GATE_FATAL,
      memory_order_acq_rel);
  *interrupted = 0;
  if (state & NET_PANIC_GATE_FATAL) {
    return false;
  }
  if ((state & NET_PANIC_GATE_BUSY) &&
      (state >> NET_PANIC_CPU_SHIFT) == cpu_initial_apic_id()) {
    *interrupted = (uint32_t)state >> NET_PANIC_OPERATION_SHIFT;
    return true;
  }
  for (unsigned poll = 0; poll < NET_PANIC_HANDOFF_POLLS; ++poll) {
    if (!(atomic_load_explicit(&gate->state, memory_order_acquire) & NET_PANIC_GATE_BUSY)) {
      return true;
    }
    __asm__ volatile("pause");
  }
  return false;
}

#endif
