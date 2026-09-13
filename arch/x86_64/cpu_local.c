#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/layout.h>
#include <stddef.h>

#define DOUBLE_FAULT_STACK_BYTES (16 * 1024)

static char bsp_double_fault_stack[DOUBLE_FAULT_STACK_BYTES]
  __attribute__((aligned(ARCH_PAGE_SIZE)));
static struct cpu_local bsp_cpu = {
  .double_fault_stack_top = (uintptr_t)(bsp_double_fault_stack + sizeof(bsp_double_fault_stack)),
};

struct cpu_local *cpu_bsp(void)
{
  return &bsp_cpu;
}

void cpu_install_local(struct cpu_local *cpu)
{
  _Static_assert(offsetof(struct cpu_local, self) == CPU_SELF_OFFSET, "GS self offset");
  _Static_assert(offsetof(struct cpu_local, syscall_stack_top) == CPU_SYSCALL_STACK_OFFSET,
                 "SYSCALL stack offset");
  _Static_assert(offsetof(struct cpu_local, saved_user_rsp) == CPU_USER_RSP_OFFSET,
                 "SYSCALL user RSP offset");

  cpu->self = cpu;
  write_msr(IA32_GS_BASE, (uintptr_t)cpu);
  write_msr(IA32_KERNEL_GS_BASE, 0);
  write_msr(IA32_FS_BASE, 0);
}
