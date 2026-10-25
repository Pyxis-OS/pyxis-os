#ifndef ARCH_HWP_H
#define ARCH_HWP_H

#include <stdbool.h>
#include <stdint.h>

enum arch_hwp_status {
  ARCH_HWP_UNAVAILABLE,
  ARCH_HWP_FAILED,
  ARCH_HWP_APPLIED,
};

enum arch_hwp_step {
  ARCH_HWP_READ_ENABLE,
  ARCH_HWP_WRITE_ENABLE,
  ARCH_HWP_CONFIRM_ENABLE,
  ARCH_HWP_READ_CAPABILITIES,
  ARCH_HWP_CHECK_CAPABILITIES,
  ARCH_HWP_READ_CONTROL,
  ARCH_HWP_CHECK_CONTROL,
  ARCH_HWP_WRITE_REQUEST,
  ARCH_HWP_READ_REQUEST,
  ARCH_HWP_CONFIRM_REQUEST,
};

/* Local boot result, immutable before this CPU publishes online. These are
 * register facts, not measured frequency or a public power-control interface. */
struct arch_hwp_state {
  enum arch_hwp_status status;
  enum arch_hwp_step step;
  uint32_t features, msr;
  uint64_t enable, capabilities, control, request, observed;
};

/* Own CPU, IF=0, kernel GS and IDT installed. No allocation or remote access. */
void arch_hwp_init(void);
/* BSP after all CPU online publications have been acquired. */
void arch_hwp_report(void);

struct exception_frame;
/* Only the two guarded MSR instruction sites recover from kernel #GP(0). */
bool arch_hwp_fault(struct exception_frame *frame);

#endif
