#include <kernel/mm/kernel_owner.h>
#include <abi/launcher.h>
#include <arch/cpu.h>
#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/task.h>
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
    /* Fixed I/O actors retain the source operation without a short lock while
     * shared range preparation admits. Refusal still precedes any capture. */
    cpu_enable_interrupts();
    bool admitted = vm_kernel_owner_acquire(&owner);
    cpu_disable_interrupts();
    if (!admitted) {
      return CALL_BUSY;
    }
  }
  enum mm_result result = vm_alloc(vm_kernel_space(), backing_bytes, PAGE_SIZE,
      PAGE_WRITE, &address);
  vm_kernel_owner_release(&owner);
  if (result != MM_OK) {
    KASSERT(result == MM_NO_MEMORY || result == MM_BUSY);
    return result == MM_BUSY ? CALL_BUSY : CALL_NO_MEMORY;
  }
  *capture = (struct image_capture){
    .address = address, .size = size, .backing_bytes = backing_bytes,
  };
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


const void *image_capture_read_begin(struct image_capture *capture)
{
  KASSERT(!cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  KASSERT(capture && capture->address && capture->size && capture->backing_bytes);
  KASSERT(capture->phase == IMAGE_CAPTURE_PRODUCING);
  struct task *actor = task_current_actor();
  size_t cpu = arch_cpu_index();
  KASSERT(actor || cpu == 0);
  capture->reader = actor;
  capture->preparation_cpu = cpu;
  capture->remote_read = cpu != 0;
  capture->phase = IMAGE_CAPTURE_READING;
  return (const void *)capture->address;
}

void image_capture_read_end(struct image_capture *capture)
{
  KASSERT(!cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  KASSERT(capture && capture->phase == IMAGE_CAPTURE_READING);
  KASSERT(capture->reader == task_current_actor() &&
      capture->preparation_cpu == arch_cpu_index());
  capture->reader = NULL;
  capture->phase = IMAGE_CAPTURE_CLOSED;
}

void image_capture_release(struct image_capture *capture)
{
  KASSERT(!cpu_interrupts_enabled() && capture);
  KASSERT(capture->phase != IMAGE_CAPTURE_READING);
  if (capture->address) {
    KASSERT(capture->size && capture->backing_bytes);
    if (capture->phase == IMAGE_CAPTURE_CLOSED) {
      KASSERT(arch_space_active(arch_kernel_space()) &&
          capture->preparation_cpu == arch_cpu_index());
      vm_kernel_retire_shared(capture->address, capture->backing_bytes,
          capture->remote_read);
    } else if (arch_cpu_index() == 0) {
      vm_kernel_retire(capture->address, capture->backing_bytes);
    } else {
      /* Even an unread capture has producer translations on CPU0. An AP RAM
       * owner must confirm their retirement before it can recycle this VA. */
      KASSERT(arch_space_active(arch_kernel_space()));
      vm_kernel_retire_shared(capture->address, capture->backing_bytes, true);
    }
  } else {
    KASSERT(!capture->size && !capture->backing_bytes);
  }
  *capture = (struct image_capture){0};
}
