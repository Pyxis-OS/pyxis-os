#include <abi/launcher.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/user/image_capture.h>

enum call_status image_capture_allocate(uint64_t size, struct image_capture *capture)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(capture && !capture->address && !capture->size && !capture->backing_bytes);
  if (!size) {
    return CALL_BAD_REQUEST;
  }
  if (size > LAUNCH_CAPTURED_IMAGE_MAX_SIZE) {
    return CALL_LIMIT;
  }
  size_t backing_bytes = (size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
  uintptr_t address;
  enum mm_result result = vm_alloc(vm_kernel_space(), backing_bytes, PAGE_SIZE,
      PAGE_WRITE, &address);
  if (result != MM_OK) {
    KASSERT(result == MM_NO_MEMORY);
    return CALL_NO_MEMORY;
  }
  *capture = (struct image_capture){address, size, backing_bytes};
  return CALL_OK;
}

void image_capture_release(struct image_capture *capture)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (capture->address) {
    KASSERT(capture->size && capture->backing_bytes);
    KASSERT(vm_free(vm_kernel_space(), capture->address, capture->backing_bytes) == MM_OK);
  } else {
    KASSERT(!capture->size && !capture->backing_bytes);
  }
  *capture = (struct image_capture){0};
}
