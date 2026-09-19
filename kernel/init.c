#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/init.h>
#include <kernel/boot.h>
#include <kernel/image.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/user.h>
#include <kernel/fb/tty.h>
#include <kernel/space.h>

#define INITIAL_STACK_BASE UINT64_C(0x800000)
#define INITIAL_STACK_SIZE PAGE_SIZE


static enum image_result load_initial_image(const struct boot_module *module,
                                            struct vm_space **space,
                                            uintptr_t *entry)
{
  size_t page_offset = module->physical & (PAGE_SIZE - 1);
  KASSERT(module->size && module->size <= SIZE_MAX - page_offset - (PAGE_SIZE - 1));
  size_t mapped_size = (page_offset + module->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  phys_addr_t first_frame = module->physical - page_offset;
  uintptr_t mapping;
  KASSERT(vm_reserve(vm_kernel_space(), mapped_size, PAGE_SIZE, &mapping) == MM_OK);

  /* The boot module frames remain reserved, borrowed backing. Give them a
   * read-only kernel mapping independent of the discarded Limine HHDM. */
  for (size_t offset = 0; offset < mapped_size; offset += PAGE_SIZE) {
    KASSERT(vm_map(vm_kernel_space(), mapping + offset, first_frame + offset, 0) == MM_OK);
  }

  enum image_result result = image_load((const void *)(mapping + page_offset),
                                        module->size, space, entry);

  for (size_t offset = 0; offset < mapped_size; offset += PAGE_SIZE) {
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), mapping + offset, &physical) == MM_OK);
    KASSERT(physical == first_frame + offset);
  }
  KASSERT(vm_release(vm_kernel_space(), mapping, mapped_size) == MM_OK);
  return result;
}

static void queue_initial_image(const struct boot_info *boot)
{
  struct vm_space *space;
  uintptr_t entry;
  enum image_result result = load_initial_image(&boot->initial_image, &space, &entry);
  if (result != IMAGE_OK) {
    panic("cannot load initial userspace image (error %u)", (unsigned)result);
  }

  enum mm_result status = vm_alloc_at(space, INITIAL_STACK_BASE, INITIAL_STACK_SIZE,
                                      PAGE_USER | PAGE_WRITE);
  if (status != MM_OK) {
    KASSERT(vm_space_destroy(space) == MM_OK);
    panic("cannot allocate initial user stack (error %u)", (unsigned)status);
  }

  klog("userspace: P1F image=%zu bytes entry=%p stack=%p\n",
       boot->initial_image.size, (void *)entry,
       (void *)(INITIAL_STACK_BASE + INITIAL_STACK_SIZE));
  size_t cpu_index = arch_cpu_count() > 1 ? 1 : 0;
  klog("userspace: initial task pinned to CPU %zu\n", cpu_index);
  status = user_task_create_on(cpu_index, space, entry,
                               INITIAL_STACK_BASE + INITIAL_STACK_SIZE);
  if (status != MM_OK) {
    KASSERT(vm_space_destroy(space) == MM_OK);
    panic("cannot create initial user task (error %u)", (unsigned)status);
  }
}

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  vm_init();
  if (!heap_init()) {
    panic("cannot initialize the TLSF heap");
  }

  boot_start_cpus();

  space_init_all(&boot->framebuffer);
  //tty_init(&boot->framebuffer, &aardvark_scheme, &bizcat);

  user_init();

  struct pmm_stats memory = pmm_get_stats();
  klog("PMM: total=%zu free=%zu allocated=%zu frames, metadata=%zu pages\n",
       memory.total_frames, memory.free_frames, memory.allocated_frames, memory.metadata_pages);

  struct vm_stats virtual = vm_get_stats(vm_kernel_space());
  struct heap_stats heap = heap_get_stats();

  klog("VM: total=%zu reserved=%zu backed=%zu pages, records=%zu/%u\n",
       virtual.total_pages, virtual.reserved_pages, virtual.backed_pages,
       virtual.range_records, VM_MAX_RANGES);
  klog("heap: TLSF pools=%zu bytes=%zu, alignment=16, live allocations=%zu\n",
       heap.pools, heap.pool_bytes, heap.live_allocations);

  queue_initial_image(boot);
  klog("Caelum ready: starting preemptive userspace\n");
  user_schedule();
}
