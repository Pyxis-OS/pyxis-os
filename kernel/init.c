#include <arch/cpu.h>
#include <kernel/init.h>
#include <kernel/log.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vm.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>

[[noreturn]] void user_address_space_test(void)
{
  struct vm_space *space;
  uintptr_t address = 0x400000;

  KASSERT(vm_space_create(&space) == MM_OK);

  KASSERT(vm_alloc_at(space, address, PAGE_SIZE,
    PAGE_USER | PAGE_WRITE) == MM_OK);

  KASSERT(vm_space_activate(space) == MM_OK);
  *(volatile uint8_t *)address = 0xf4;
  KASSERT(vm_protect(space, address, PAGE_USER | PAGE_EXEC) == MM_OK);

  uintptr_t stack_base = 0x800000;

  KASSERT(vm_alloc_at(space, stack_base, PAGE_SIZE,
    PAGE_USER | PAGE_WRITE) == MM_OK);

  __asm__ volatile(
    "mov %[flags], %%r11\n"
    "mov %[stack], %%rsp\n"
    "sysretq\n"
    :
    : "c" (address),
    [stack] "r"(stack_base + PAGE_SIZE),
    [flags] "i"(UINT64_C(1) << 1)
    : "r11", "memory");

  __builtin_unreachable();

  KASSERT(vm_space_activate(vm_kernel_space()) == MM_OK);

  KASSERT(vm_space_destroy(space) == MM_OK);
}

[[noreturn]] void kernel_init(const struct boot_info *boot)
{
  vm_init();
  if (!heap_init()) {
    panic("cannot initialize the TLSF heap");
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

  user_address_space_test();

  klog("Caelum ready: image=%zu bytes; kernel initialization complete, halting\n",
       boot->kernel_size);
  cpu_halt();
}
