#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/paging.h>
#include <kernel/memory.h>
#include <kernel/mm/device_pages.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/task.h>

static void require_context(void)
{
  KASSERT(cpu_interrupts_enabled() && cpu_current() == cpu_bsp());
  KASSERT(task_current_actor() && arch_space_active(arch_kernel_space()));
}

static size_t batch_size(size_t remaining)
{
  return remaining < PMM_FRAME_BATCH_MAX ? remaining : PMM_FRAME_BATCH_MAX;
}

/* No short lock or scratch mapping survives a step. The logical VM owner
 * remains with this retained task while other actors may run. */
static uint64_t yield_step(uint64_t flags)
{
  cpu_restore_interrupts(flags);
  kernel_task_yield_if_runnable();
  return cpu_save_interrupts();
}

static void unmap_pages(uintptr_t address, const phys_addr_t *frames,
    size_t count, uint64_t *flags)
{
  while (count) {
    size_t step = batch_size(count);
    size_t first = count - step, removed_count = 0;
    phys_addr_t removed[PMM_FRAME_BATCH_MAX];
    while (removed_count < step) {
      size_t got = vm_unmap_kernel_run(address + (first + removed_count) * PAGE_SIZE,
          removed, step - removed_count);
      KASSERT(got);
      for (size_t i = 0; i < got; ++i) {
        KASSERT(removed[i] == frames[first + removed_count + i]);
      }
      removed_count += got;
    }
    count = first;
    if (count) {
      *flags = yield_step(*flags);
    }
  }
}

static void free_frames(phys_addr_t *frames, size_t count, uint64_t *flags)
{
  while (count) {
    size_t step = batch_size(count);
    count -= step;
    pmm_free_frames(frames + count, step);
    if (count) {
      *flags = yield_step(*flags);
    }
  }
  kfree(frames);
}

enum mm_result device_pages_allocate(struct device_pages *pages, size_t bytes,
    enum device_pages_cache cache)
{
  require_context();
  if (!pages || pages->address || pages->frames || pages->bytes || pages->quarantined ||
      !bytes || bytes > SIZE_MAX - (PAGE_SIZE - 1) ||
      (cache != DEVICE_PAGES_WB && cache != DEVICE_PAGES_WC)) {
    return MM_INVALID;
  }
  bytes = (bytes + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
  size_t count = bytes / PAGE_SIZE;
  if (count > SIZE_MAX / sizeof(phys_addr_t)) {
    return MM_INVALID;
  }
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_acquire(&owner)) {
    return MM_BUSY;
  }
  uint64_t flags = cpu_save_interrupts();
  enum mm_result result = MM_NO_MEMORY;
  phys_addr_t *frames = kmalloc(count * sizeof(*frames));
  uintptr_t address = 0;
  size_t allocated = 0, mapped = 0;
  if (!frames) {
    goto done;
  }
  result = vm_reserve(vm_kernel_space(), bytes, PAGE_SIZE, &address);
  if (result != MM_OK) {
    goto done;
  }
  while (allocated < count) {
    size_t wanted = batch_size(count - allocated);
    size_t got = pmm_alloc_frames(frames + allocated, wanted);
    allocated += got;
    if (got != wanted) {
      result = MM_NO_MEMORY;
      goto done;
    }
    flags = yield_step(flags);
  }
  while (mapped < count) {
    size_t step = batch_size(count - mapped);
    for (size_t i = 0; i < step; ++i) {
      arch_frame_zero(frames[mapped + i]);
    }
    if (cache == DEVICE_PAGES_WC &&
        !arch_frames_prepare_scanout(frames + mapped, step)) {
      result = MM_INVALID;
      goto done;
    }
    size_t end = mapped + step;
    while (mapped < end) {
      size_t done_pages;
      result = cache == DEVICE_PAGES_WC ?
          vm_map_scanout_run(address + mapped * PAGE_SIZE, frames + mapped,
              end - mapped, &done_pages) :
          vm_map_kernel_run(address + mapped * PAGE_SIZE, frames + mapped,
              end - mapped, &done_pages);
      mapped += done_pages;
      if (result != MM_OK) {
        goto done;
      }
      KASSERT(done_pages);
    }
    flags = yield_step(flags);
  }
  *pages = (struct device_pages){.address = address, .frames = frames,
      .bytes = bytes, .cache = cache};
  vm_kernel_owner_release(&owner);
  cpu_restore_interrupts(flags);
  return MM_OK;

done:
  /* No reference escaped: local unpublished rollback requires no remote ACK. */
  if (address) {
    unmap_pages(address, frames, mapped, &flags);
    KASSERT(vm_release(vm_kernel_space(), address, bytes) == MM_OK);
  }
  if (frames) {
    free_frames(frames, allocated, &flags);
  }
  vm_kernel_owner_release(&owner);
  cpu_restore_interrupts(flags);
  return result;
}

enum mm_result device_pages_release(struct device_pages *pages, bool confirm_remote)
{
  require_context();
  if (!pages) {
    return MM_INVALID;
  }
  if (pages->quarantined) {
    return MM_BUSY;
  }
  if (!pages->address) {
    return !pages->frames && !pages->bytes ? MM_OK : MM_INVALID;
  }
  struct vm_kernel_owner owner = {0};
  if (!vm_kernel_owner_acquire(&owner)) {
    return MM_BUSY;
  }
  /* Consumer joining includes each writer's fence. Drain this owner's WC
   * stores too before either remote confirmation or local unmapping. */
  cpu_store_fence();
  if (confirm_remote && !vm_kernel_flush_remote()) {
    uint64_t flags = cpu_save_interrupts();
    vm_kernel_quarantine(pages->address, pages->bytes);
    pages->quarantined = true;
    vm_kernel_owner_release(&owner);
    cpu_restore_interrupts(flags);
    return MM_BUSY;
  }
  uint64_t flags = cpu_save_interrupts();
  size_t count = pages->bytes / PAGE_SIZE;
  unmap_pages(pages->address, pages->frames, count, &flags);
  KASSERT(vm_release(vm_kernel_space(), pages->address, pages->bytes) == MM_OK);
  free_frames((phys_addr_t *)pages->frames, count, &flags);
  *pages = (struct device_pages){0};
  vm_kernel_owner_release(&owner);
  cpu_restore_interrupts(flags);
  return MM_OK;
}
