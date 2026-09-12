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

/* All retained data is copied into the kernel image. No response pointers. */
struct boot_info {
  uint64_t kernel_phys;
  uintptr_t kernel_virt;
  size_t kernel_size;
  uint64_t bootstrap_direct_offset; /* Valid only before arch_init returns. */
  size_t region_count;
  struct boot_region regions[BOOT_MAX_REGIONS];
};

#endif
