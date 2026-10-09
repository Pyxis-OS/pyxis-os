#include <abi/launcher.h>
#include <arch/smp.h>
#include <kernel/image.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user/launch.h>

#define INITIAL_STACK_TOP UINT64_C(0x7ffffffff000)

enum mm_result user_initial_stack_size(uint64_t requested, size_t *size)
{
  *size = 0;
  if (!requested) {
    requested = LAUNCH_INITIAL_STACK_MIN_SIZE;
  }
  if (requested < LAUNCH_INITIAL_STACK_MIN_SIZE ||
      requested > LAUNCH_INITIAL_STACK_MAX_SIZE || requested % PAGE_SIZE) {
    return MM_INVALID;
  }
  *size = requested;
  return MM_OK;
}

enum mm_result user_process_load(struct space *space, const void *bytes, size_t size,
    uint64_t initial_stack_bytes, struct process **process, uintptr_t *entry,
    uintptr_t *stack_top)
{
  KASSERT(arch_cpu_index() == 0 && process && entry && stack_top);
  *process = NULL;
  *entry = 0;
  *stack_top = 0;
  size_t stack_size;
  enum mm_result result = user_initial_stack_size(initial_stack_bytes, &stack_size);
  if (result != MM_OK) {
    return result;
  }
  uintptr_t stack_base = INITIAL_STACK_TOP - stack_size;
  uintptr_t guard_base = stack_base - PAGE_SIZE;
  struct vm_space *address_space;
  uintptr_t start;
  enum image_result image = image_load(bytes, size,
      guard_base, INITIAL_STACK_TOP, &address_space, &start);
  if (image != IMAGE_OK) {
    return image == IMAGE_NO_MEMORY ? MM_NO_MEMORY : MM_INVALID;
  }
  /* Image validation excludes this interval before backing. Reserve the guard
   * so later allocations cannot fill it; stack and image belong to this VM. */
  result = vm_reserve_at(address_space, guard_base, PAGE_SIZE);
  if (result == MM_OK) {
    result = vm_alloc_at(address_space, stack_base,
        stack_size, PAGE_USER | PAGE_WRITE);
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
