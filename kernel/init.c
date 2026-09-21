#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/smp.h>
#include <kernel/init.h>
#include <kernel/boot.h>
#include <kernel/image.h>
#include <kernel/initrd.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user.h>
#include <kernel/task.h>
#include <kernel/fb/tty.h>
#include <kernel/space.h>
#include <kernel/console.h>
#include <kernel/blob.h>

#define INITIAL_STACK_BASE UINT64_C(0x800000)
#define INITIAL_STACK_SIZE PAGE_SIZE
#define INITIAL_IMAGE_NAME "hello.pxe"
#define INITIAL_CONTENT_NAME "hello.txt"

static void queue_initial_image(void)
{
  struct initrd_file image;
  enum initrd_result archive_result = initrd_lookup(INITIAL_IMAGE_NAME, &image);
  if (archive_result != INITRD_OK) {
    panic("cannot find %s in initrd (error %u)", INITIAL_IMAGE_NAME,
          (unsigned)archive_result);
  }

  struct initrd_file content_file;
  archive_result = initrd_lookup(INITIAL_CONTENT_NAME, &content_file);
  if (archive_result != INITRD_OK) {
    panic("cannot find %s in initrd (error %u)", INITIAL_CONTENT_NAME,
          (unsigned)archive_result);
  }

  struct vm_space *space;
  uintptr_t entry;
  enum image_result result = image_load(image.data, image.size, &space, &entry);
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
       image.size, (void *)entry,
       (void *)(INITIAL_STACK_BASE + INITIAL_STACK_SIZE));
  size_t cpu_index = arch_cpu_count() > 1 ? 1 : 0;
  struct process *process;
  status = process_create(arch_cpu_at(cpu_index)->space, space, &process);
  if (status != MM_OK) {
    KASSERT(vm_space_destroy(space) == MM_OK);
    panic("cannot create initial process (error %u)", (unsigned)status);
  }

  handle_t output;
  enum capability_result grant = capability_install(&process->capabilities,
      &process->space->console->object, CAP_WRITE, &output);
  if (grant != CAP_OK) {
    KASSERT(process_destroy(process) == MM_OK);
    panic("cannot grant initial console (error %u)", (unsigned)grant);
  }

  struct blob_object *blob = blob_create(&content_file);
  if (!blob) {
    KASSERT(process_destroy(process) == MM_OK);
    panic("cannot create initial content blob");
  }
  handle_t content;
  grant = capability_install(&process->capabilities, &blob->object, CAP_READ,
      &content);
  object_release(&blob->object);
  if (grant != CAP_OK) {
    KASSERT(process_destroy(process) == MM_OK);
    panic("cannot grant initial content (error %u)", (unsigned)grant);
  }

  status = process_prepare_startup(process, output, content);
  if (status != MM_OK) {
    KASSERT(process_destroy(process) == MM_OK);
    panic("cannot prepare initial startup record (error %u)", (unsigned)status);
  }

  klog("userspace: initial task pinned to CPU %zu\n", cpu_index);
  status = user_task_create_on(cpu_index, process, entry,
                               INITIAL_STACK_BASE + INITIAL_STACK_SIZE);
  if (status != MM_OK) {
    KASSERT(process_destroy(process) == MM_OK);
    panic("cannot create initial user task (error %u)", (unsigned)status);
  }
}

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  vm_init();
  if (!heap_init()) {
    panic("cannot initialize the TLSF heap");
  }

  enum initrd_result archive_result = initrd_init(&boot->initrd);
  if (archive_result != INITRD_OK) {
    panic("cannot initialize boot archive (error %u)", (unsigned)archive_result);
  }
  klog("initrd: newc archive=%zu bytes, mapped read-only\n", boot->initrd.size);

  boot_start_cpus();

  space_init_all(&boot->framebuffer);

  task_init();

  enum mm_result result = kernel_task_create(space_present_task, NULL);
  if (result != MM_OK) {
    panic("cannot create presentation task (error %u)", (unsigned)result);
  }

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

  queue_initial_image();
  klog("Caelum ready: starting preemptive userspace\n");
  task_schedule();
}
