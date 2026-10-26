#include <limine.h>
#include <arch/init.h>
#include <arch/layout.h>
#include <kernel/boot.h>
#include <kernel/init.h>
#include <kernel/log.h>
#include <kernel/fb/early_console.h>
#include <kernel/panic.h>
#include <kernel/memory.h>
#include "mp.h"

#define REQUIRED_BASE_REVISION 6
#define PAGING_REQUEST_MIN_MAX_REVISION 1
#define FRAMEBUFFER_BITS_PER_PIXEL 32
#define FRAMEBUFFER_CHANNEL_BITS 8
#define EFI_PAGE_BYTES UINT64_C(4096)

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t requests_start[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests")))
static volatile uint64_t base_revision[] = LIMINE_BASE_REVISION(REQUIRED_BASE_REVISION);

__attribute__((used, section(".limine_requests")))
static volatile struct limine_memmap_request memory_request = {
  .id = LIMINE_MEMMAP_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_efi_memmap_request efi_memory_request = {
  .id = LIMINE_EFI_MEMMAP_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_efi_system_table_request efi_system_table_request = {
  .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
  .id = LIMINE_HHDM_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_rsdp_request rsdp_request = {
  .id = LIMINE_RSDP_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_date_at_boot_request date_request = {
  .id = LIMINE_DATE_AT_BOOT_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_address_request address_request = {
  .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_file_request executable_file_request = {
  .id = LIMINE_EXECUTABLE_FILE_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_executable_cmdline_request command_line_request = {
  .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_module_request module_request = {
  .id = LIMINE_MODULE_REQUEST_ID,
};

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
  .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
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

struct efi_table_header {
  uint64_t signature;
  uint32_t revision, size, crc32, reserved;
};

struct efi_system_table_prefix {
  struct efi_table_header header;
  uint64_t vendor;
  uint32_t firmware_revision, padding;
  uint64_t console_in_handle, console_in;
  uint64_t console_out_handle, console_out;
  uint64_t error_handle, error;
  uint64_t runtime_services;
};

struct efi_runtime_table_prefix {
  struct efi_table_header header;
  uint64_t get_time, set_time, get_wakeup_time, set_wakeup_time;
  uint64_t set_virtual_map, convert_pointer, get_variable;
  uint64_t get_next_variable_name, set_variable;
};

#define EFI_SYSTEM_TABLE_SIGNATURE UINT64_C(0x5453595320494249)
#define EFI_RUNTIME_TABLE_SIGNATURE UINT64_C(0x56524553544e5552)

_Static_assert(offsetof(struct efi_system_table_prefix, runtime_services) == 88,
    "EFI system table layout");
_Static_assert(offsetof(struct efi_runtime_table_prefix, set_virtual_map) == 56 &&
    offsetof(struct efi_runtime_table_prefix, get_variable) == 72 &&
    offsetof(struct efi_runtime_table_prefix, set_variable) == 88,
    "EFI runtime table layout");

static bool efi_hhdm_pointer(const void *pointer, size_t bytes, uint64_t *physical)
{
  uintptr_t address = (uintptr_t)pointer;
  if (!pointer || address < boot.bootstrap_direct_offset ||
      bytes > UINTPTR_MAX - address) {
    return false;
  }
  uint64_t base = address - boot.bootstrap_direct_offset;
  for (size_t i = 0; i < boot.region_count; ++i) {
    const struct boot_region *region = &boot.regions[i];
    if (region->type == BOOT_FIRMWARE && base >= region->base &&
        bytes <= region->length && base - region->base <= region->length - bytes) {
      *physical = base;
      return true;
    }
  }
  return false;
}

static void copy_efi_runtime_table(void)
{
  const struct limine_efi_system_table_response *response =
      efi_system_table_request.response;
  uint64_t system_physical, runtime_physical;
  if (!boot.efi_map_valid || !response ||
      !efi_hhdm_pointer(response->address, sizeof(struct efi_system_table_prefix),
          &system_physical)) {
    return;
  }
  struct efi_system_table_prefix system;
  memcpy(&system, response->address, sizeof(system));
  if (system.header.signature != EFI_SYSTEM_TABLE_SIGNATURE ||
      system.header.size < sizeof(system) ||
      system.runtime_services > UINT64_MAX - boot.bootstrap_direct_offset) {
    return;
  }
  const void *runtime = (const void *)(uintptr_t)(
      system.runtime_services + boot.bootstrap_direct_offset);
  if (!efi_hhdm_pointer(runtime, sizeof(struct efi_runtime_table_prefix),
      &runtime_physical)) {
    return;
  }
  struct efi_runtime_table_prefix table;
  memcpy(&table, runtime, sizeof(table));
  if (table.header.signature != EFI_RUNTIME_TABLE_SIGNATURE ||
      table.header.size < sizeof(table) || !table.set_virtual_map ||
      !table.get_variable || !table.set_variable) {
    return;
  }
  boot.efi_system_table_physical = system_physical;
  boot.efi_runtime_table_physical = runtime_physical;
  boot.efi_set_virtual_map_physical = table.set_virtual_map;
  boot.efi_get_variable_physical = table.get_variable;
  boot.efi_set_variable_physical = table.set_variable;
}

static void copy_efi_memory_map(void)
{
  const struct limine_efi_memmap_response *map = efi_memory_request.response;
  struct efi_descriptor {
    uint32_t type, padding;
    uint64_t physical, virtual, pages, attributes;
  };
  if (!map || !map->memmap || map->desc_version != 1 ||
      map->desc_size < sizeof(struct efi_descriptor) || !map->memmap_size ||
      map->memmap_size % map->desc_size ||
      map->memmap_size > UINTPTR_MAX - (uintptr_t)map->memmap ||
      map->memmap_size / map->desc_size > BOOT_MAX_REGIONS ||
      map->memmap_size > BOOT_EFI_MAP_BYTES) {
    return;
  }
  size_t count = map->memmap_size / map->desc_size;
  for (size_t i = 0; i < count; ++i) {
    struct efi_descriptor descriptor;
    memcpy(&descriptor, (const uint8_t *)map->memmap + i * map->desc_size,
        sizeof(descriptor));
    if (!descriptor.pages || descriptor.pages > (UINT64_MAX - descriptor.physical) / EFI_PAGE_BYTES ||
        (descriptor.physical & (EFI_PAGE_BYTES - 1))) {
      return;
    }
    boot.efi_regions[i] = (struct boot_efi_region){
      .base = descriptor.physical, .length = descriptor.pages * EFI_PAGE_BYTES,
      .attributes = descriptor.attributes, .type = descriptor.type,
    };
  }
  boot.efi_region_count = count;
  memcpy(boot.efi_map_words, map->memmap, map->memmap_size);
  boot.efi_map_size = map->memmap_size;
  boot.efi_descriptor_size = map->desc_size;
  boot.efi_descriptor_version = map->desc_version;
  boot.efi_map_valid = true;
}

static enum boot_region_type region_type(uint64_t type)
{
  switch (type) {
  case LIMINE_MEMMAP_USABLE:
    return BOOT_USABLE;
  case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
  case LIMINE_MEMMAP_ACPI_NVS:
    return BOOT_ACPI;
  case LIMINE_MEMMAP_RESERVED_MAPPED:
    return BOOT_FIRMWARE;
  case LIMINE_MEMMAP_BAD_MEMORY:
    return BOOT_BAD;
  case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
    return BOOT_LOADER;
  case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
    return BOOT_KERNEL;
  case LIMINE_MEMMAP_FRAMEBUFFER:
    return BOOT_FRAMEBUFFER;
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
      !address_request.response || !executable_file_request.response ||
      !paging_request.response) {
    panic("missing required Limine response (memory/HHDM/executable/file/paging)");
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

static void split_memory_region(size_t index, uint64_t boundary)
{
  if (boot.region_count == BOOT_MAX_REGIONS) {
    panic("boot file reservation exceeds memory map capacity (limit %u)", BOOT_MAX_REGIONS);
  }
  struct boot_region region = boot.regions[index];
  for (size_t i = boot.region_count; i > index; --i) {
    boot.regions[i] = boot.regions[i - 1];
  }
  ++boot.region_count;
  boot.regions[index].length = boundary - region.base;
  boot.regions[index + 1].base = boundary;
  boot.regions[index + 1].length = region.base + region.length - boundary;
}

static void reserve_kernel_file(uint64_t first_frame, uint64_t frame_end)
{
  /* Original file bytes may be loader-reclaimable, unlike loaded ELF segments.
   * Preserve only these frames when loader memory is eventually reclaimed. */
  for (size_t i = 0; i < boot.region_count; ++i) {
    struct boot_region region = boot.regions[i];
    uint64_t region_end = region.base + region.length;
    if (region_end <= first_frame) {
      continue;
    }
    if (region.base >= frame_end) {
      break;
    }
    if (region.type == BOOT_KERNEL) {
      continue;
    }
    if (region.base < first_frame) {
      split_memory_region(i, first_frame);
      ++i;
    }
    if (region_end > frame_end) {
      split_memory_region(i, frame_end);
    }
    boot.regions[i].type = BOOT_KERNEL;
  }
}

static struct boot_module copy_file_extent(const struct limine_file *file,
                                          const char *name, bool executable)
{
  if (!file) {
    panic("missing %s file", name);
  }
  uintptr_t address = (uintptr_t)file->address;
  if (!file->size || file->size > SIZE_MAX ||
      address < boot.bootstrap_direct_offset || file->size > UINTPTR_MAX - address) {
    panic("invalid %s file extent", name);
  }
  uint64_t physical = address - boot.bootstrap_direct_offset;
  if (file->size > UINT64_MAX - physical) {
    panic("%s file extent overflows", name);
  }
  uint64_t first_frame = physical & ~(ARCH_PAGE_SIZE - 1);
  uint64_t end = physical + file->size;
  if (end > UINT64_MAX - (ARCH_PAGE_SIZE - 1)) {
    panic("%s file page extent overflows", name);
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
    if (region->base > covered ||
        (region->type != BOOT_KERNEL && !(executable && region->type == BOOT_LOADER))) {
      break;
    }
    covered = region_end;
  }
  if (covered < frame_end) {
    panic("%s file is outside boot file reservations", name);
  }
  if (executable) {
    reserve_kernel_file(first_frame, frame_end);
  }
  return (struct boot_module){.physical = physical, .size = file->size};
}

static void copy_uuid(uint8_t bytes[16], const struct limine_uuid *uuid)
{
  bytes[0] = uuid->a;
  bytes[1] = uuid->a >> 8;
  bytes[2] = uuid->a >> 16;
  bytes[3] = uuid->a >> 24;
  bytes[4] = uuid->b;
  bytes[5] = uuid->b >> 8;
  bytes[6] = uuid->c;
  bytes[7] = uuid->c >> 8;
  for (size_t i = 0; i < 8; ++i) {
    bytes[8 + i] = uuid->d[i];
  }
}

static void copy_boot_files(void)
{
  const struct limine_module_response *response = module_request.response;
  if (!response || response->module_count != 1 || !response->modules ||
      !response->modules[0]) {
    panic("expected exactly one initrd module");
  }

  boot.initrd = copy_file_extent(response->modules[0], "initrd", false);
  boot.kernel_file = copy_file_extent(executable_file_request.response->executable_file,
                                    "kernel ELF", true);
  const struct limine_file *file = executable_file_request.response->executable_file;
  switch (file->media_type) {
  case LIMINE_MEDIA_TYPE_GENERIC: boot.origin.media = BOOT_MEDIA_DISK; break;
  case LIMINE_MEDIA_TYPE_OPTICAL: boot.origin.media = BOOT_MEDIA_OPTICAL; break;
  case LIMINE_MEDIA_TYPE_TFTP: boot.origin.media = BOOT_MEDIA_NETWORK; break;
  default: boot.origin.media = BOOT_MEDIA_UNKNOWN; break;
  }
  if (boot.origin.media == BOOT_MEDIA_DISK) {
    copy_uuid(boot.origin.disk, &file->gpt_disk_uuid);
    copy_uuid(boot.origin.partition, &file->gpt_part_uuid);
    uint8_t disk_nonzero = 0, partition_nonzero = 0;
    for (size_t i = 0; i < 16; ++i) {
      disk_nonzero |= boot.origin.disk[i];
      partition_nonzero |= boot.origin.partition[i];
    }
    boot.origin.gpt = disk_nonzero && partition_nonzero;
  }
}

static void copy_acpi_address(void)
{
  if (!rsdp_request.response) {
    return;
  }

  /* Base revision 6 returns the RSDP through the bootstrap direct map. */
  uintptr_t address = (uintptr_t)rsdp_request.response->address;
  if (address < boot.bootstrap_direct_offset) {
    panic("invalid ACPI RSDP address");
  }
  boot.acpi_rsdp = address - boot.bootstrap_direct_offset;
}

static void copy_framebuffer(void)
{
  const struct limine_framebuffer_response *response = framebuffer_request.response;
  if (!response || !response->framebuffer_count) {
    return;
  }
  if (!response->framebuffers || !response->framebuffers[0]) {
    panic("invalid Limine framebuffer response");
  }

  const struct limine_framebuffer *source = response->framebuffers[0];
  if (source->memory_model != LIMINE_FRAMEBUFFER_RGB ||
      source->bpp != FRAMEBUFFER_BITS_PER_PIXEL ||
      source->red_mask_size != FRAMEBUFFER_CHANNEL_BITS ||
      source->green_mask_size != FRAMEBUFFER_CHANNEL_BITS ||
      source->blue_mask_size != FRAMEBUFFER_CHANNEL_BITS ||
      source->red_mask_shift > FRAMEBUFFER_BITS_PER_PIXEL - FRAMEBUFFER_CHANNEL_BITS ||
      source->green_mask_shift > FRAMEBUFFER_BITS_PER_PIXEL - FRAMEBUFFER_CHANNEL_BITS ||
      source->blue_mask_shift > FRAMEBUFFER_BITS_PER_PIXEL - FRAMEBUFFER_CHANNEL_BITS) {
    panic("framebuffer requires 32-bit RGB with eight-bit channels");
  }

  uint32_t red_mask = UINT32_C(0xff) << source->red_mask_shift;
  uint32_t green_mask = UINT32_C(0xff) << source->green_mask_shift;
  uint32_t blue_mask = UINT32_C(0xff) << source->blue_mask_shift;
  uintptr_t address = (uintptr_t)source->address;
  if ((red_mask & green_mask) || (red_mask & blue_mask) || (green_mask & blue_mask) ||
      !source->width || !source->height || source->width > SIZE_MAX / sizeof(uint32_t) ||
      source->pitch < source->width * sizeof(uint32_t) ||
      (source->pitch % sizeof(uint32_t)) || (address % sizeof(uint32_t)) ||
      source->height > SIZE_MAX / source->pitch || address < boot.bootstrap_direct_offset) {
    panic("invalid framebuffer layout");
  }

  size_t size = source->height * source->pitch;
  if (size > UINTPTR_MAX - address) {
    panic("framebuffer extent overflows");
  }
  uint64_t physical = address - boot.bootstrap_direct_offset;

  /* The framebuffer response gives the address. The map only confirms these
   * bytes are unavailable to the PMM. Limine reports the framebuffer's exact
   * extent: only usable entries are rounded to pages, so one ending mid-page
   * (2880x1800x4 bytes) leaves the rest of that page unreported. */
  uint64_t end = physical + size;
  uint64_t covered = physical;
  for (size_t i = 0; i < boot.region_count && covered < end; ++i) {
    const struct boot_region *region = &boot.regions[i];
    uint64_t region_end = region->base + region->length;
    if (region_end <= covered) {
      continue;
    }
    if (region->base > covered || region->type != BOOT_FRAMEBUFFER) {
      break;
    }
    covered = region_end;
  }
  if (covered < end) {
    panic("framebuffer is outside framebuffer reservations");
  }

  /* Its partial boundary pages are mapped whole, so they must not hold RAM
   * that Caelum allocates, reclaims or occupies. */
  uint64_t first_page = physical & ~(ARCH_PAGE_SIZE - 1);
  if (end > UINT64_MAX - (ARCH_PAGE_SIZE - 1)) {
    panic("framebuffer page extent overflows");
  }
  uint64_t page_end = (end + ARCH_PAGE_SIZE - 1) & ~(ARCH_PAGE_SIZE - 1);
  for (size_t i = 0; i < boot.region_count; ++i) {
    const struct boot_region *region = &boot.regions[i];
    bool allocatable = region->type == BOOT_USABLE || region->type == BOOT_LOADER ||
                       region->type == BOOT_KERNEL;
    if (allocatable && region->base < page_end &&
        region->base + region->length > first_page) {
      panic("framebuffer pages overlap allocatable memory");
    }
  }

  boot.framebuffer = (struct boot_framebuffer){
    .physical = physical,
    .size = size,
    .width = source->width,
    .height = source->height,
    .pitch = source->pitch,
    .red_shift = source->red_mask_shift,
    .green_shift = source->green_mask_shift,
    .blue_shift = source->blue_mask_shift,
  };
}

static void copy_command_line(void)
{
  if (!command_line_request.response || !command_line_request.response->cmdline) {
    kwarn("boot options: no kernel command line supplied; using defaults\n");
    boot.command_line[0] = '\0';
    return;
  }
  const char *source = command_line_request.response->cmdline;
  for (size_t i = 0; i < sizeof(boot.command_line); ++i) {
    boot.command_line[i] = source[i];
    if (!source[i]) {
      return;
    }
  }
  panic("kernel command line exceeds %zu bytes", sizeof(boot.command_line) - 1);
}

[[noreturn]] void limine_entry(void);

[[noreturn]] void limine_entry(void)
{
  early_init();

  validate_responses();
  copy_executable_placement();
  copy_memory_map();
  copy_efi_memory_map();
  copy_efi_runtime_table();
  /* Validation needs the memory map; start the screen before other checks. */
  copy_framebuffer();
  if (boot.framebuffer.size) {
    early_console_start(&boot.framebuffer, boot.bootstrap_direct_offset + boot.framebuffer.physical);
    kinfo("early console: %zux%zu framebuffer\n", boot.framebuffer.width, boot.framebuffer.height);
  } else {
    kinfo("early console: no framebuffer; serial output only\n");
  }
  copy_command_line();
  copy_acpi_address();
  copy_boot_files();
  if (date_request.response) {
    boot.utc_seconds = date_request.response->timestamp;
    boot.utc_available = true;
  }
  limine_capture_cpus(&boot);

  kinfo("Limine: base revision %u, %zu memory regions, kernel phys=0x%lx virt=%p\n",
        REQUIRED_BASE_REVISION, boot.region_count, boot.kernel_phys,
        (void *)boot.kernel_virt);

  /* MP handoff records are borrowed through temporary mappings until AP
   * startup completes. Other responses and arrays are never consulted again. */
  arch_init(&boot);
  kernel_init(&boot);
}
