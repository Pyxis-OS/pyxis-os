#include <arch/smp.h>
#include <kernel/image.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user/launch.h>

enum mm_result user_process_load(struct space *space, const void *bytes, size_t size,
                                  struct process **process, uintptr_t *entry)
{
  KASSERT(arch_cpu_index() == 0 && process && entry);
  *process = NULL;
  *entry = 0;
  struct vm_space *address_space;
  uintptr_t start;
  enum image_result image = image_load(bytes, size, &address_space, &start);
  if (image != IMAGE_OK) {
    return image == IMAGE_NO_MEMORY ? MM_NO_MEMORY : MM_INVALID;
  }
  enum mm_result result = vm_alloc_at(address_space, USER_INITIAL_STACK_BASE,
      USER_INITIAL_STACK_SIZE, PAGE_USER | PAGE_WRITE);
  if (result == MM_OK) {
    result = process_create(space, address_space, process);
  }
  if (result != MM_OK) {
    KASSERT(vm_space_destroy(address_space) == MM_OK);
    return result;
  }
  *entry = start;
  return MM_OK;
}
