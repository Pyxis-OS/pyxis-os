#include <kernel/mm/kernel_owner.h>
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
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_try(&owner)) {
    return CALL_BUSY;
  }
  enum mm_result result = vm_alloc(vm_kernel_space(), backing_bytes, PAGE_SIZE,
      PAGE_WRITE, &address);
  vm_kernel_owner_release(&owner);
  if (result != MM_OK) {
    KASSERT(result == MM_NO_MEMORY || result == MM_BUSY);
    return result == MM_BUSY ? CALL_BUSY : CALL_NO_MEMORY;
  }
  *capture = (struct image_capture){address, size, backing_bytes};
  return CALL_OK;
}

enum call_status image_capture_allocate_wait(uint64_t size, struct image_capture *capture)
{
  KASSERT(arch_cpu_index() == 0 && cpu_interrupts_enabled());
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_acquire(&owner)) {
    return CALL_BUSY;
  }
  uint64_t flags = cpu_save_interrupts();
  enum call_status status = image_capture_allocate(size, capture);
  vm_kernel_owner_release(&owner);
  cpu_restore_interrupts(flags);
  return status;
}

void image_capture_release(struct image_capture *capture)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (capture->address) {
    KASSERT(capture->size && capture->backing_bytes);
    /* Only BSP readers ever saw this selected-image copy. The retained VM
     * record now owns its complete range and cleanup attribution. */
    vm_kernel_retire(capture->address, capture->backing_bytes);
  } else {
    KASSERT(!capture->size && !capture->backing_bytes);
  }
  *capture = (struct image_capture){0};
}
