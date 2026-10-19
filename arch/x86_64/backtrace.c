#include <arch/backtrace.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/layout.h>
#include <arch/paging.h>
#include <kernel/log.h>
#include <stdatomic.h>
#include <stdbool.h>

#define FRAME_ALIGNMENT 8
#define FRAME_WORDS 2 /* Saved frame pointer, then the return address. */

#define NO_REPORTER UINT64_MAX

/* The APIC id of the CPU walking its backtrace, or NO_REPORTER. */
static _Atomic uint64_t reporter = NO_REPORTER;

/* Reasons are complete phrases so the log line reads on its own. */
static const char *frame_problem(uintptr_t frame, uintptr_t previous, uintptr_t first)
{
  if (!frame) {
    return NULL;
  }
  if (frame < HIGHER_HALF_BASE) {
    return "frame is not a kernel address";
  }
  if (frame & (FRAME_ALIGNMENT - 1)) {
    return "frame is misaligned";
  }
  if (previous && frame <= previous) {
    return "frame is not above the previous one";
  }
  if (frame - first > ARCH_BACKTRACE_STACK_SPAN - FRAME_WORDS * sizeof(uintptr_t)) {
    return "frame leaves the stack span";
  }
  if (!arch_kernel_address_mapped(frame) ||
      !arch_kernel_address_mapped(frame + sizeof(uintptr_t))) {
    return "frame is not mapped";
  }
  return NULL;
}

static void report_cpu(uintptr_t rip, bool rip_returns)
{
  const char *label = rip_returns ? "caller" : "rip";
  /* GS holds the user base during entry and exit, so trust it only when it
   * names kernel memory; the APIC identity needs no GS at all. */
  if (read_msr(IA32_GS_BASE) >= HIGHER_HALF_BASE) {
    kerror("cpu %lu apic %u %s 0x%lx\n", cpu_current()->index, cpu_initial_apic_id(), label, rip);
  } else {
    kerror("cpu ? apic %u %s 0x%lx\n", cpu_initial_apic_id(), label, rip);
  }
}

void arch_fatal_report(uintptr_t rip, bool rip_returns, uintptr_t frame)
{
  uint64_t apic = cpu_initial_apic_id();
  uint64_t expected = NO_REPORTER;
  if (!atomic_compare_exchange_strong_explicit(&reporter, &expected, apic,
        memory_order_relaxed, memory_order_relaxed)) {
    if (expected == apic) {
      kerror("backtrace: fault during the fatal report; not walking again\n");
    } else {
      report_cpu(rip, rip_returns);
      kerror("backtrace: another CPU (apic %lu) is already reporting; not walking\n", expected);
    }
    return;
  }
  report_cpu(rip, rip_returns);
  kerror("backtrace: return addresses, newest first\n");
  uintptr_t first = frame, previous = 0;
  const char *stop = NULL;
  unsigned count = 0;
  while (count < ARCH_BACKTRACE_FRAMES) {
    stop = frame_problem(frame, previous, first);
    if (!frame || stop) {
      break;
    }
    const uintptr_t *words = (const uintptr_t *)frame;
    uintptr_t next = words[0], return_address = words[1];
    /* A prepared task's first frame is zeroed: the chain's normal end. */
    if (!return_address) {
      frame = 0;
      break;
    }
    if (return_address < HIGHER_HALF_BASE) {
      stop = "return address is not kernel code";
      break;
    }
    kerror("  #%u 0x%lx\n", ++count, return_address);
    previous = frame;
    frame = next;
  }
  if (stop) {
    kerror("backtrace: stopped at 0x%lx: %s\n", frame, stop);
  } else if (count == ARCH_BACKTRACE_FRAMES && frame) {
    kerror("backtrace: stopped after %u frames\n", count);
  } else {
    kerror("backtrace: end of chain\n");
  }
}
