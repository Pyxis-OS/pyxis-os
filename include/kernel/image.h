#ifndef KERNEL_IMAGE_H
#define KERNEL_IMAGE_H

#include <stddef.h>
#include <stdint.h>

struct vm_space;

enum image_result {
  IMAGE_OK,
  IMAGE_INVALID,
  IMAGE_NO_MEMORY,
  IMAGE_BUSY,
};

/* Load P1F bytes from a stable, readable kernel mapping after heap/VM init.
 * On success the caller owns a new, inactive space with shared kernel mappings
 * and an entry address. Segments must fit the user address range and must not intersect the caller's page-aligned [reserved_base, reserved_end).
 * Validation precedes backing. No stack is allocated and no code is executed.
 * On failure, allocations are unwound and both outputs are cleared.
 * The active space is unchanged. Retained preparation actor, interrupts disabled. */
enum image_result image_load(const void *bytes, size_t size,
    uintptr_t reserved_base, uintptr_t reserved_end,
    struct vm_space **space, uintptr_t *entry);

#endif
