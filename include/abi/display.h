#ifndef ABI_DISPLAY_H
#define ABI_DISPLAY_H

#include <abi/message.h>

#define DISPLAY_RIGHT_DRAW (UINT64_C(1) << 0)

#define DISPLAY_ACQUIRE UINT64_C(1)
#define DISPLAY_PRESENT UINT64_C(2)
#define DISPLAY_RELEASE UINT64_C(3)

/* All requests are just a message_header. ACQUIRE returns this layout and a
 * zeroed, page-rounded mapping, writable and NX. Pixels are native uint32_t
 * words with three 8-bit channels at the returned shifts; other bits are zero.
 * pitch is bytes per row; size includes page padding, not additional pixels.
 * Dimensions cover the space below the navigation bar and stay fixed. */
struct display_buffer {
  uint64_t address;
  uint64_t size;
  uint64_t width;
  uint64_t height;
  uint64_t pitch;
  uint32_t red_shift;
  uint32_t green_shift;
  uint32_t blue_shift;
  uint32_t reserved;
};

/* DRAW authorizes every operation, only in the display's own space. ACQUIRE
 * is exclusive and returns BUSY even if this process already owns graphics.
 * PRESENT selects the mapping for periodic presentation; writes thereafter
 * may appear without another call. No snapshot, vblank or tear-free guarantee.
 * PRESENT/RELEASE require the acquiring process and return no reply bytes.
 * RELEASE unmaps and restores the TTY; exit/fault does the same automatically.
 * Closing/copying a handle does not release/transfer an acquired session.
 * The mapping is not a private-memory allocation; MEMORY_RELEASE cannot free it. */
_Static_assert(sizeof(struct display_buffer) == 56, "display buffer layout");

#endif
