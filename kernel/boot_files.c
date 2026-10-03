#include <arch/clock.h>
#include <kernel/boot.h>
#include <kernel/boot_files.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>

#define BOOT_FILE_CLOCK_SAMPLE_BYTES (256 * PAGE_SIZE)

static struct initrd_file kernel_file;
static struct initrd_file archive_file;

enum mm_result boot_files_init(const struct boot_info *boot)
{
  if (kernel_file.data || !boot || !boot->kernel_file.size) {
    return MM_INVALID;
  }
  struct initrd_file archive;
  if (initrd_archive(&archive) != INITRD_OK) {
    return MM_INVALID;
  }

  const struct boot_module *module = &boot->kernel_file;
  size_t page_offset = module->physical & (PAGE_SIZE - 1);
  if (module->size > SIZE_MAX - page_offset - (PAGE_SIZE - 1)) {
    return MM_INVALID;
  }
  size_t mapped_size = (page_offset + module->size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  phys_addr_t first_frame = module->physical - page_offset;
  if (first_frame > UINT64_MAX - mapped_size) {
    return MM_INVALID;
  }

  uintptr_t mapping;
  enum mm_result result = vm_reserve(vm_kernel_space(), mapped_size, PAGE_SIZE, &mapping);
  if (result != MM_OK) {
    return result;
  }

  size_t mapped = 0;
  while (mapped < mapped_size) {
    result = vm_map(vm_kernel_space(), mapping + mapped, first_frame + mapped, 0);
    if (result != MM_OK) {
      break;
    }
    mapped += PAGE_SIZE;
    if (!(mapped % BOOT_FILE_CLOCK_SAMPLE_BYTES)) {
      arch_clock_maintain();
    }
  }
  if (result == MM_OK) {
    kernel_file = (struct initrd_file){
      .data = (const void *)(mapping + page_offset),
      .size = module->size,
    };
    archive_file = archive;
    return MM_OK;
  }

  while (mapped) {
    mapped -= PAGE_SIZE;
    phys_addr_t physical;
    KASSERT(vm_unmap(vm_kernel_space(), mapping + mapped, &physical) == MM_OK);
    KASSERT(physical == first_frame + mapped);
  }
  KASSERT(vm_release(vm_kernel_space(), mapping, mapped_size) == MM_OK);
  return result;
}

const struct initrd_file *boot_files_kernel(void)
{
  return kernel_file.data ? &kernel_file : NULL;
}

const struct initrd_file *boot_files_archive(void)
{
  return kernel_file.data ? &archive_file : NULL;
}
