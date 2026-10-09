#include <arch/smp.h>
#include <kernel/image.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user/launch.h>

#define INITIAL_STACK_TOP UINT64_C(0x7ffffffff000)
#define INITIAL_STACK_SIZE (1 * 1024 * 1024)
#define INITIAL_STACK_BASE (INITIAL_STACK_TOP - INITIAL_STACK_SIZE)
#define INITIAL_STACK_GUARD_BASE (INITIAL_STACK_BASE - PAGE_SIZE)

enum mm_result user_process_load(struct space *space, const void *bytes, size_t size,
    struct process **process, uintptr_t *entry, uintptr_t *stack_top)
{
  KASSERT(arch_cpu_index() == 0 && process && entry && stack_top);
  *process = NULL;
  *entry = 0;
  *stack_top = 0;
  struct vm_space *address_space;
  uintptr_t start;
  enum image_result image = image_load(bytes, size,
      INITIAL_STACK_GUARD_BASE, INITIAL_STACK_TOP, &address_space, &start);
  if (image != IMAGE_OK) {
    return image == IMAGE_NO_MEMORY ? MM_NO_MEMORY : MM_INVALID;
  }
  /* Image validation excludes this interval before backing. Reserve the guard
   * so later allocations cannot fill it; stack and image belong to this VM. */
  enum mm_result result = vm_reserve_at(address_space,
      INITIAL_STACK_GUARD_BASE, PAGE_SIZE);
  if (result == MM_OK) {
    result = vm_alloc_at(address_space, INITIAL_STACK_BASE,
        INITIAL_STACK_SIZE, PAGE_USER | PAGE_WRITE);
  }
  if (result == MM_OK) {
    result = process_create(space, address_space, process);
  }
  if (result != MM_OK) {
    KASSERT(vm_space_destroy(address_space) == MM_OK);
    return result;
  }
  *entry = start;
  *stack_top = INITIAL_STACK_TOP;
  return MM_OK;
}
