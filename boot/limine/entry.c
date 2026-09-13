#include <limine.h>
#include <arch/init.h>
#include <arch/layout.h>
#include <kernel/boot.h>
#include <kernel/init.h>
#include <kernel/log.h>
#include <kernel/panic.h>

#define REQUIRED_BASE_REVISION 6
#define PAGING_REQUEST_MIN_MAX_REVISION 1

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(REQUIRED_BASE_REVISION);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_request = {
  .id = LIMINE_MEMMAP_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
  .id = LIMINE_HHDM_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request address_request = {
  .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
  .id = LIMINE_MODULE_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_paging_mode_request paging_request = {
  .id = LIMINE_PAGING_MODE_REQUEST_ID,
  .revision = PAGING_REQUEST_MIN_MAX_REVISION,
  .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
  .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
  .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
};

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t requests_end[] = LIMINE_REQUESTS_END_MARKER;

static struct boot_info boot;

static enum boot_region_type region_type(uint64_t type)
{
  switch (type) {
  case LIMINE_MEMMAP_USABLE:
    return BOOT_USABLE;
  case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
  case LIMINE_MEMMAP_ACPI_NVS:
    return BOOT_ACPI;
  case LIMINE_MEMMAP_BAD_MEMORY:
    return BOOT_BAD;
  case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
    return BOOT_LOADER;
  case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
    return BOOT_KERNEL;
  default:
    return BOOT_RESERVED;
  }
}

static void validate_responses(void)
{
  if (!LIMINE_BASE_REVISION_SUPPORTED(base_revision)) {
    panic("Limine protocol base revision %u unsupported", REQUIRED_BASE_REVISION);
  }

  if (!memory_request.response || !hhdm_request.response ||
      !address_request.response || !paging_request.response) {
    panic("missing required Limine response (memory/HHDM/executable/paging)");
  }

  if (paging_request.response->mode != LIMINE_PAGING_MODE_X86_64_4LVL) {
    panic("Caelum requires exactly four-level paging");
  }
}

static void copy_executable_placement(void)
{
  boot.kernel_phys = address_request.response->physical_base;
  boot.kernel_virt = address_request.response->virtual_base;
  boot.kernel_size = (uintptr_t)__kernel_end - (uintptr_t)__kernel_start;
  boot.bootstrap_direct_offset = hhdm_request.response->offset;

  if (boot.kernel_virt != (uintptr_t)__kernel_start ||
      (boot.kernel_phys & (ARCH_PAGE_SIZE - 1)) ||
      boot.kernel_phys > UINT64_MAX - boot.kernel_size ||
      boot.bootstrap_direct_offset < HIGHER_HALF_BASE ||
      (boot.bootstrap_direct_offset & (ARCH_PAGE_SIZE - 1))) {
    panic("invalid executable placement or bootstrap direct map");
  }
}

static void copy_memory_map(void)
{
  const struct limine_memmap_response *map = memory_request.response;
  if (!map->entry_count || map->entry_count > BOOT_MAX_REGIONS || !map->entries) {
    panic("invalid memory map or capacity exceeded (limit %u)", BOOT_MAX_REGIONS);
  }

  boot.region_count = map->entry_count;
  uint64_t previous_end = 0;
  bool kernel_reserved = false;
  for (size_t i = 0; i < boot.region_count; ++i) {
    const struct limine_memmap_entry *source = map->entries[i];
    if (!source || !source->length || source->base < previous_end ||
        source->base > UINT64_MAX - source->length) {
      panic("invalid, overlapping or unsorted memory region %zu", i);
    }

    struct boot_region *dest = &boot.regions[i];
    *dest = (struct boot_region){
      .base = source->base,
      .length = source->length,
      .type = region_type(source->type),
    };

    previous_end = dest->base + dest->length;
    if (dest->type == BOOT_KERNEL && dest->base <= boot.kernel_phys &&
        previous_end >= boot.kernel_phys + boot.kernel_size) {
      kernel_reserved = true;
    }
  }

  if (!kernel_reserved) {
    panic("kernel image is not covered by an executable memory reservation");
  }
}

static void copy_initial_image(void)
{
  const struct limine_module_response *response = module_request.response;
  if (!response || response->module_count != 1 || !response->modules ||
      !response->modules[0]) {
    panic("expected exactly one initial userspace image module");
  }

  const struct limine_file *module = response->modules[0];
  uintptr_t address = (uintptr_t)module->address;
  if (!module->size || address < boot.bootstrap_direct_offset ||
      module->size > UINTPTR_MAX - address) {
    panic("invalid initial image module extent");
  }

  uint64_t physical = address - boot.bootstrap_direct_offset;
  uint64_t first_frame = physical & ~(ARCH_PAGE_SIZE - 1);
  uint64_t end = physical + module->size;
  if (end > UINT64_MAX - (ARCH_PAGE_SIZE - 1)) {
    panic("initial image module extent overflows");
  }
  uint64_t frame_end = (end + ARCH_PAGE_SIZE - 1) & ~(ARCH_PAGE_SIZE - 1);

  /* Check every frame we will map is reserved from the PMM. Only physical
   * placement and byte size survive; never retain a Limine file structure. */
  uint64_t covered = first_frame;
  for (size_t i = 0; i < boot.region_count && covered < frame_end; ++i) {
    const struct boot_region *region = &boot.regions[i];
    uint64_t region_end = region->base + region->length;
    if (region_end <= covered) {
      continue;
    }
    if (region->base > covered || region->type != BOOT_KERNEL) {
      break;
    }
    covered = region_end;
  }
  if (covered < frame_end) {
    panic("initial image is outside executable/module reservations");
  }

  boot.initial_image = (struct boot_module){.physical = physical, .size = module->size};
}

[[noreturn]] void limine_entry(void);

[[noreturn]] void limine_entry(void)
{
  early_init();

  validate_responses();
  copy_executable_placement();
  copy_memory_map();
  copy_initial_image();

  klog("Limine: base revision %u, %zu memory regions, kernel phys=0x%lx virt=%p\n",
       REQUIRED_BASE_REVISION, boot.region_count, boot.kernel_phys,
       (void *)boot.kernel_virt);

  /* Responses and their arrays are never consulted again. */
  arch_init(&boot);
  kernel_init(&boot);
}
