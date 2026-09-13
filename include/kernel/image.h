#ifndef KERNEL_IMAGE_H
#define KERNEL_IMAGE_H

#include <stddef.h>
#include <stdint.h>

struct vm_space;

enum image_result {
  IMAGE_OK,
  IMAGE_INVALID,
  IMAGE_NO_MEMORY,
};

/* Load P1F bytes from a stable, readable kernel mapping after heap/VM init.
 * On success the caller owns a new, inactive space with shared kernel mappings
 * and an entry address. No stack is allocated and no code is executed.
 * On failure, allocations are unwound and both outputs are cleared.
 * The active space is unchanged. Single CPU, interrupts disabled. */
enum image_result image_load(const void *bytes, size_t size,
                             struct vm_space **space, uintptr_t *entry);

#endif
