#include <arch/user.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/user.h>

static struct vm_space *active_user_space;

int user_run(struct vm_space *space, uintptr_t entry, uintptr_t stack_top)
{
  KASSERT(!active_user_space && space && space != vm_kernel_space());

  KASSERT(vm_space_activate(space) == MM_OK);
  active_user_space = space;
  int status = arch_run_user(entry, stack_top);

  /* This is the suspended caller's stack again. The user root must stop being
   * active before the caller can release its mappings and page-table frames. */
  KASSERT(vm_space_activate(vm_kernel_space()) == MM_OK);
  active_user_space = NULL;
  return status;
}

[[noreturn]] void user_exit(int status)
{
  KASSERT(active_user_space);
  arch_return_from_user(status);
}
