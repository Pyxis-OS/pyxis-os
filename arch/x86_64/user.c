#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/descriptors.h>
#include <arch/layout.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/user.h>
#include <stddef.h>

#define CPUID_FEATURE_FXSR (1u << 24)
#define CPUID_FEATURE_SSE (1u << 25)
#define CPUID_FEATURE_SSE2 (1u << 26)
#define CR0_MONITOR_COPROCESSOR (UINT64_C(1) << 1)
#define CR0_EMULATE_FPU (UINT64_C(1) << 2)
#define CR0_TASK_SWITCHED (UINT64_C(1) << 3)
#define CR0_NUMERIC_ERROR (UINT64_C(1) << 5)
#define CR4_OSFXSR (UINT64_C(1) << 9)
#define CR4_OSXMMEXCPT (UINT64_C(1) << 10)
#define CR4_FSGSBASE (UINT64_C(1) << 16)
#define CR4_OSXSAVE (UINT64_C(1) << 18)
#define X87_DEFAULT_CONTROL 0x037f
#define MXCSR_DEFAULT 0x1f80
#define USER_STACK_ALIGNMENT 16

struct switch_frame {
  uint64_t r15, r14, r13, r12, rbp, rbx;
  uintptr_t entry;
};

void arch_user_init(void)
{
  _Static_assert(sizeof(struct arch_fp_state) == 512, "FXSAVE64 area size");
  _Static_assert(offsetof(struct arch_fp_state, mxcsr) == 24, "FXSAVE MXCSR offset");
  _Static_assert(offsetof(struct arch_fp_state, xmm) == 160, "FXSAVE XMM offset");

  uint32_t eax, ebx, ecx, edx;
  cpuid(CPUID_BASIC_FEATURES, &eax, &ebx, &ecx, &edx);
  uint32_t required = CPUID_FEATURE_FXSR | CPUID_FEATURE_SSE | CPUID_FEATURE_SSE2;
  if ((edx & required) != required) {
    panic("userspace requires FXSAVE, SSE and SSE2 support");
  }

  write_cr0((read_cr0() | CR0_MONITOR_COPROCESSOR | CR0_NUMERIC_ERROR) &
            ~(CR0_EMULATE_FPU | CR0_TASK_SWITCHED));
  /* Eager x87/SSE preservation only: AVX and user FS/GS-base instructions stay
   * disabled. Kernel compilation forbids FP/SIMD register use. */
  write_cr4((read_cr4() | CR4_OSFXSR | CR4_OSXMMEXCPT) &
            ~(CR4_OSXSAVE | CR4_FSGSBASE));
  apic_timer_start();
}

void arch_user_state_init(struct arch_user_state *state)
{
  memset(state, 0, sizeof(*state));
  state->ds = GDT_USER_DATA_SELECTOR;
  state->es = GDT_USER_DATA_SELECTOR;
  state->fp.control = X87_DEFAULT_CONTROL;
  state->fp.mxcsr = MXCSR_DEFAULT;
}

void arch_user_save(struct arch_user_state *state)
{
  __asm__ volatile("fxsave64 %0" : "=m"(state->fp));
  __asm__ volatile("mov %%ds, %0" : "=rm"(state->ds));
  __asm__ volatile("mov %%es, %0" : "=rm"(state->es));
  __asm__ volatile("mov %%fs, %0" : "=rm"(state->fs));
  __asm__ volatile("mov %%gs, %0" : "=rm"(state->gs));
  state->fs_base = read_msr(IA32_FS_BASE);
  state->gs_base = read_msr(IA32_KERNEL_GS_BASE);
}

void arch_user_restore(const struct arch_user_state *state)
{
  __asm__ volatile("fxrstor64 %0" : : "m"(state->fp));
  __asm__ volatile("mov %0, %%ds" : : "rm"(state->ds) : "memory");
  __asm__ volatile("mov %0, %%es" : : "rm"(state->es) : "memory");
  __asm__ volatile("mov %0, %%fs" : : "rm"(state->fs) : "memory");
  uint64_t kernel_gs = (uintptr_t)cpu_current();
  __asm__ volatile("mov %0, %%gs" : : "rm"(state->gs) : "memory");
  /* Loading a selector may replace its hidden base; restore the MSRs last. */
  write_msr(IA32_FS_BASE, state->fs_base);
  write_msr(IA32_GS_BASE, kernel_gs);
  write_msr(IA32_KERNEL_GS_BASE, state->gs_base);
}

uintptr_t arch_context_prepare(uintptr_t stack_top, void (*entry)(void))
{
  /* RET must leave a C entry stack at 8 modulo 16, like a real CALL. */
  struct switch_frame *frame = (void *)(stack_top - sizeof(uintptr_t) - sizeof(*frame));
  memset(frame, 0, sizeof(*frame));
  frame->entry = (uintptr_t)entry;
  return (uintptr_t)frame;
}

bool arch_user_entry_valid(uintptr_t entry, uintptr_t stack_top)
{
  /* A one-past-the-end stack pointer must itself be canonical for IRETQ. */
  return entry && entry <= LOWER_HALF_MAX && stack_top &&
         stack_top <= LOWER_HALF_MAX && !(stack_top & (USER_STACK_ALIGNMENT - 1));
}

void arch_user_set_kernel_stack(uintptr_t stack_top)
{
  struct cpu_local *cpu = cpu_current();
  cpu->syscall_stack_top = stack_top;
  gdt_set_kernel_stack(&cpu->descriptors, stack_top);
}

[[noreturn]] void arch_bad_user_return(uintptr_t entry, uintptr_t stack_top)
{
  klog("userspace: invalid syscall return rip=%p rsp=%p\n",
       (void *)entry, (void *)stack_top);
  user_fault();
}
