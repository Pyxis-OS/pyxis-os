#ifndef ARCH_USER_H
#define ARCH_USER_H

#include <stdint.h>

struct arch_fp_state {
  uint16_t control, status;
  uint8_t tag, reserved;
  uint16_t opcode;
  uint64_t instruction, operand;
  uint32_t mxcsr, mxcsr_mask;
  uint8_t x87[128];
  uint8_t xmm[256];
  uint8_t padding[96];
} __attribute__((aligned(16)));

struct arch_user_state {
  uint64_t fs_base, gs_base;
  uint16_t ds, es, fs, gs;
  struct arch_fp_state fp;
};

void arch_user_init(void);
void arch_user_state_init(struct arch_user_state *state);
void arch_user_save(struct arch_user_state *state);
void arch_user_restore(const struct arch_user_state *state);
bool arch_user_entry_valid(uintptr_t entry, uintptr_t stack_top);

/* Stays on the calling CPU, IF=0. Both stacks must be mapped in every
 * participating space.
 * Switch preserves C callee-saved registers; interrupt entry saves user GPRs. */
void arch_context_switch(uintptr_t *old_stack, uintptr_t new_stack);
uintptr_t arch_context_prepare(uintptr_t stack_top, void (*entry)(void));

/* Sets both TSS.RSP0 and SYSCALL's explicit stack target, with IF=0. */
void arch_user_set_kernel_stack(uintptr_t stack_top);

/* Starts a fresh user context with zeroed GPRs and IF=1. The caller already
 * activated its space, restored extended state and selected its entry stack. */
[[noreturn]] void arch_enter_user(uintptr_t entry, uintptr_t stack_top);
[[noreturn]] void arch_bad_user_return(uintptr_t entry, uintptr_t stack_top);

#endif
