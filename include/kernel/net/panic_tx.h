#ifndef KERNEL_NET_PANIC_TX_H
#define KERNEL_NET_PANIC_TX_H

#include <arch/cpu.h>
#include <kernel/net/log_udp.h>
#include <stdatomic.h>
#include <stdbool.h>

#define NET_PANIC_GATE_BUSY UINT64_C(1)
#define NET_PANIC_GATE_FATAL UINT64_C(2)
#define NET_PANIC_GATE_PIPELINE UINT64_C(4)
#define NET_PANIC_GATE_DEBUG UINT64_C(8)
#define NET_PANIC_GATE_FLAGS (NET_PANIC_GATE_BUSY | NET_PANIC_GATE_FATAL | \
                              NET_PANIC_GATE_PIPELINE | NET_PANIC_GATE_DEBUG)
#define NET_PANIC_OPERATION_SHIFT 4
#define NET_PANIC_CPU_SHIFT 32
#define NET_PANIC_HANDOFF_POLLS 100000
#define NET_PANIC_COMPLETION_POLLS 1000000

struct net_panic_gate {
  atomic_uint_fast64_t state;
  uint64_t debug_generation;
};

/* Boot BSP, IF=0, before AP startup; called by either log or debug enable.
 * One predicate replaces the former log-only predicate on normal mutations. */
void net_panic_context_enable(void);
bool net_panic_context_enabled(void);
uint32_t net_panic_context_cpu(void);

/* Short BSP hardware sections, IF=0. PIPELINE records possible stack-held RX
 * completions/borrowed buffers, without locking across protocol callbacks. */
static inline bool net_panic_gate_enter(struct net_panic_gate *gate, unsigned operation)
{
  if (!net_panic_context_enabled()) {
    return true;
  }
  if (net_log_udp_panicking()) {
    return false;
  }
  uint_fast64_t expected = atomic_load_explicit(&gate->state, memory_order_relaxed);
  if (expected & (NET_PANIC_GATE_BUSY | NET_PANIC_GATE_FATAL | NET_PANIC_GATE_DEBUG)) {
    return false;
  }
  uint_fast64_t state = (uint64_t)net_panic_context_cpu() << NET_PANIC_CPU_SHIFT |
      (uint64_t)operation << NET_PANIC_OPERATION_SHIFT | NET_PANIC_GATE_BUSY |
      (expected & NET_PANIC_GATE_PIPELINE);
  return atomic_compare_exchange_strong_explicit(&gate->state, &expected, state,
      memory_order_acquire, memory_order_relaxed);
}

static inline void net_panic_gate_leave(struct net_panic_gate *gate)
{
  if (!net_panic_context_enabled()) {
    return;
  }
  atomic_fetch_and_explicit(&gate->state,
      NET_PANIC_GATE_FATAL | NET_PANIC_GATE_DEBUG | NET_PANIC_GATE_PIPELINE,
      memory_order_release);
}

static inline bool net_panic_gate_closed(const struct net_panic_gate *gate)
{
  return net_log_udp_panicking() ||
      (atomic_load_explicit(&gate->state, memory_order_acquire) &
       (NET_PANIC_GATE_FATAL | NET_PANIC_GATE_DEBUG));
}

/* Enabled ordinary worker only. Covers completion accounting, local RX batches,
 * protocol borrowing and repost; interruption here refuses debug takeover. */
static inline bool net_panic_gate_pipeline_begin(struct net_panic_gate *gate)
{
  uint_fast64_t expected = 0;
  return !net_log_udp_panicking() && atomic_compare_exchange_strong_explicit(
      &gate->state, &expected, NET_PANIC_GATE_PIPELINE,
      memory_order_acquire, memory_order_relaxed);
}

static inline void net_panic_gate_pipeline_end(struct net_panic_gate *gate)
{
  atomic_fetch_and_explicit(&gate->state,
      NET_PANIC_GATE_FATAL | NET_PANIC_GATE_DEBUG, memory_order_release);
}

static inline bool net_panic_gate_debug_begin(struct net_panic_gate *gate,
    uint64_t generation)
{
  uint_fast64_t expected = 0;
  if (!generation || net_log_udp_panicking() ||
      !atomic_compare_exchange_strong_explicit(&gate->state, &expected,
        NET_PANIC_GATE_DEBUG, memory_order_acq_rel, memory_order_relaxed)) {
    return false;
  }
  gate->debug_generation = generation;
  return true;
}

static inline bool net_panic_gate_debug_owned(const struct net_panic_gate *gate,
    uint64_t generation)
{
  return atomic_load_explicit(&gate->state, memory_order_acquire) ==
      NET_PANIC_GATE_DEBUG && gate->debug_generation == generation;
}

static inline bool net_panic_gate_debug_retained(const struct net_panic_gate *gate)
{
  return atomic_load_explicit(&gate->state, memory_order_acquire) & NET_PANIC_GATE_DEBUG;
}

static inline void net_panic_gate_debug_release(struct net_panic_gate *gate)
{
  gate->debug_generation = 0;
  atomic_store_explicit(&gate->state, 0, memory_order_release);
}

/* Fatal takeover preserves legacy same-CPU publication recovery. It never
 * steals a debugger-owned ring, even when that debugger has failed. */
static inline bool net_panic_gate_take(struct net_panic_gate *gate, unsigned *interrupted)
{
  if (!net_log_udp_enabled()) {
    return false;
  }
  uint_fast64_t state = atomic_load_explicit(&gate->state, memory_order_acquire);
  bool claimed = false;
  for (unsigned attempt = 0; attempt < NET_PANIC_HANDOFF_POLLS; ++attempt) {
    if (state & (NET_PANIC_GATE_FATAL | NET_PANIC_GATE_DEBUG)) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&gate->state, &state,
        state | NET_PANIC_GATE_FATAL, memory_order_acq_rel, memory_order_acquire)) {
      claimed = true;
      break;
    }
  }
  if (!claimed) {
    return false;
  }
  *interrupted = 0;
  if ((state & NET_PANIC_GATE_BUSY) &&
      (state >> NET_PANIC_CPU_SHIFT) == cpu_initial_apic_id()) {
    *interrupted = ((uint32_t)state & ~(uint32_t)NET_PANIC_GATE_FLAGS) >> NET_PANIC_OPERATION_SHIFT;
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
