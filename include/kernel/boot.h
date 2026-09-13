#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <stddef.h>
#include <stdint.h>

#define BOOT_MAX_REGIONS 256

enum boot_region_type {
  BOOT_USABLE,
  BOOT_RESERVED,
  BOOT_ACPI,
  BOOT_BAD,
  BOOT_LOADER,
  BOOT_KERNEL,
};

struct boot_region {
  uint64_t base;
  uint64_t length;
  enum boot_region_type type;
};

/* The initial image remains in boot-reserved physical frames. The kernel must
 * map them before reading; neither the Limine pointer nor its HHDM survives. */
struct boot_module {
  uint64_t physical;
  size_t size;
};

/* All retained metadata is copied into the kernel image. No response pointers. */
struct boot_info {
  uint64_t kernel_phys;
  uintptr_t kernel_virt;
  size_t kernel_size;
  uint64_t bootstrap_direct_offset; /* Valid only before arch_init returns. */
  struct boot_module initial_image;
  size_t region_count;
  struct boot_region regions[BOOT_MAX_REGIONS];
};

#endif
