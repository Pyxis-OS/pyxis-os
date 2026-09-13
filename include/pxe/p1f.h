#ifndef PXE_P1F_H
#define PXE_P1F_H

#include <stdint.h>

#define P1F_MAGIC UINT64_C(0x54414c4631455850) /* "PXE1FLAT" on disk. */
#define P1F_PAGE_SIZE UINT64_C(4096)
#define P1F_USER_LIMIT (UINT64_C(1) << 47)

#define P1F_EXECUTE (UINT64_C(1) << 0)
#define P1F_WRITE (UINT64_C(1) << 1)
#define P1F_READ (UINT64_C(1) << 2)

/* Fixed-address x86_64 images. All fields are little-endian uint64_t values.
 * The header is followed by segment_count descriptors, then their file bytes
 * in descriptor order, with no padding or trailing bytes. Read descriptors via
 * memcpy: the caller's image buffer need not be aligned for these structures. */
struct p1f_header {
  uint64_t magic;
  uint64_t entry;
  uint64_t segment_count;
};

/* Segments are in ascending virtual-address order, start on 4 KiB boundaries,
 * and do not overlap even after rounding memory_size up to a page boundary.
 * memory_size is nonzero; file_size <= memory_size. The tail is zero-filled.
 * All segments are readable; writable executable segments are unsupported.
 * The entry must lie within an executable segment's unrounded memory extent.
 * Page zero and addresses at or above P1F_USER_LIMIT are unavailable. */
struct p1f_segment {
  uint64_t virtual_address;
  uint64_t file_size;
  uint64_t memory_size;
  uint64_t flags;
};

_Static_assert(sizeof(struct p1f_header) == 24, "P1F header layout");
_Static_assert(sizeof(struct p1f_segment) == 32, "P1F segment layout");

#endif
