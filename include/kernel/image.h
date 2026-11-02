#ifndef KERNEL_IMAGE_H
#define KERNEL_IMAGE_H

#include <pxe/p1d.h>
#include <stdbool.h>
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

/* A validated P1D or P2D image (include/pxe/p1d.h). Segment descriptors are
 * read with image_dynamic_segment; payload is the file offset of the first
 * segment's bytes. span is the page-rounded end of the last segment: a
 * library's image occupies [base, base + span). */
struct image_dynamic {
  bool library;
  uint64_t entry;
  uint64_t dynamic_address;
  size_t segment_count;
  size_t payload;
  uint64_t span;
};

/* Checks the container only, never the tables: magic and header, segment
 * order, page alignment, flags, W^X and RELRO, the dynamic section inside one
 * read-only segment, the entry rules, the payload size and, for P2D, the
 * trailing symbol section. A library's segments are offsets from zero. */
bool image_dynamic_validate(const void *bytes, size_t size, struct image_dynamic *image);
struct p1f_segment image_dynamic_segment(const void *bytes, size_t index);

#endif
