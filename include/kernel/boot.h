#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_MAX_REGIONS 256

enum boot_region_type {
  BOOT_USABLE,
  BOOT_RESERVED,
  BOOT_ACPI,
  BOOT_FIRMWARE,
  BOOT_BAD,
  BOOT_LOADER,
  BOOT_KERNEL,
  BOOT_FRAMEBUFFER,
};

struct boot_region {
  uint64_t base;
  uint64_t length;
  enum boot_region_type type;
};

/* The boot archive remains in boot-reserved physical frames. The kernel must
 * map them before reading; neither the Limine pointer nor its HHDM survives. */
struct boot_module {
  uint64_t physical;
  size_t size;
};

/* One 32-bit RGB framebuffer with eight bits per channel. Physical storage is
 * device-owned; address is filled by paging_init with a supervisor-only kernel
 * mapping. Pitch includes row padding. No Limine pointers are retained. */
struct boot_framebuffer {
  uint64_t physical;
  uintptr_t address;
  size_t size;
  size_t width;
  size_t height;
  size_t pitch;
  uint8_t red_shift;
  uint8_t green_shift;
  uint8_t blue_shift;
};

/* All retained metadata is copied into the kernel image. No response pointers. */
struct boot_info {
  uint64_t kernel_phys;
  uintptr_t kernel_virt;
  size_t kernel_size;
  uint64_t bootstrap_direct_offset; /* Valid only before arch_init returns. */
  uint64_t acpi_rsdp; /* Physical; zero when the bootloader found no ACPI. */
  int64_t utc_seconds; /* Unix seconds from Limine; not an exact handoff sample. */
  bool utc_available;
  struct boot_module initrd;
  struct boot_framebuffer framebuffer;
  size_t region_count;
  struct boot_region regions[BOOT_MAX_REGIONS];
};

/* BSP only, after VM/heap setup: finish the bootloader's AP handoff. */
void boot_start_cpus(void);

#endif
