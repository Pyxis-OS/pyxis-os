#ifndef KERNEL_MM_TYPES_H
#define KERNEL_MM_TYPES_H
#include <stddef.h>
#include <stdint.h>

#define PAGE_SIZE UINT64_C(4096)
typedef uint64_t phys_addr_t;

/* Readable mappings; absence of PAGE_EXEC means NX. */
enum page_permissions {
  PAGE_WRITE = 1,
  PAGE_EXEC = 2,
  PAGE_USER = 4,
};

enum mm_result {
  MM_OK,
  MM_INVALID,
  MM_NO_MEMORY,
  MM_COLLISION,
  MM_NOT_MAPPED,
};

#endif
