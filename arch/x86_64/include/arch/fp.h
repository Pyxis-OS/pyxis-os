#ifndef ARCH_FP_H
#define ARCH_FP_H

#include <stddef.h>
#include <stdint.h>

struct arch_fp_cpu {
  uint32_t features1, features7, features71, features71d, extended_features;
  uint32_t save_features;
  uint32_t ymm_offset, ymm_size, state_size;
  bool supported;
  _Atomic bool configured;
};

/* Boot only: each CPU probes before online publication. The BSP finalizes
 * after discovery; APs join before entering the scheduler. */
void arch_fp_detect(void);
void arch_fp_finalize(void);
void arch_fp_ap_wait(void);

/* Immutable after the boot rendezvous. State storage is kernel-owned. */
size_t arch_fp_size(void);
void arch_fp_save(void *area);
void arch_fp_restore(const void *area);

#endif
