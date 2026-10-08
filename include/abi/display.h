#ifndef ABI_DISPLAY_H
#define ABI_DISPLAY_H

#include <abi/message.h>

#define DISPLAY_RIGHT_DRAW (UINT64_C(1) << 0)

#define DISPLAY_ACQUIRE UINT64_C(1)
#define DISPLAY_PRESENT UINT64_C(2)
#define DISPLAY_RELEASE UINT64_C(3)
#define DISPLAY_SIZE UINT64_C(4)
#define DISPLAY_REPLACE UINT64_C(5)

/* REPLACE includes the expected current destination generation; the other
 * requests are just a message_header. */
struct display_replace_request {
  struct message_header header;
  uint64_t generation;
};

/* ACQUIRE and REPLACE return this layout and a
 * zeroed, page-rounded mapping, writable and NX. Pixels are native uint32_t
 * words with three 8-bit channels at the returned shifts; other bits are zero.
 * pitch is bytes per row; size includes page padding, not additional pixels.
 * Dimensions cover the space below the navigation bar. This mapping's layout
 * stays fixed until REPLACE or RELEASE; generation identifies its geometry. */
struct display_buffer {
  uint64_t address;
  uint64_t size;
  uint64_t width;
  uint64_t height;
  uint64_t pitch;
  uint64_t generation;
  uint32_t red_shift;
  uint32_t green_shift;
  uint32_t blue_shift;
  uint32_t reserved;
};

/* Current destination below navigation, returned atomically with its generation.
 * SIZE needs DRAW but no acquired session; it grants no mode-setting authority.
 * A screen resize clips an existing mapping to this destination without changing
 * that mapping. Generation starts at 1 and advances on each committed resize. */
struct display_size_reply {
  uint64_t width;
  uint64_t height;
  uint64_t pitch;
  uint64_t generation;
  uint32_t red_shift;
  uint32_t green_shift;
  uint32_t blue_shift;
  uint32_t reserved;
};

/* DRAW authorizes every operation, only in the display's own space. ACQUIRE
 * is exclusive and returns BUSY even if this process already owns graphics.
 * First PRESENT selects the mapping for periodic presentation. Later PRESENT
 * and REPLACE preserve the user's graphics/terminal layer choice; writes may
 * appear without another call. No snapshot, vblank or tear-free guarantee.
 * PRESENT/REPLACE/RELEASE require the acquiring process. PRESENT and RELEASE
 * return no reply bytes. REPLACE returns BUSY on a generation mismatch and
 * otherwise maps a zeroed current-size buffer at a disjoint address. Success
 * invalidates the old pointer before returning, preserves ownership and visible
 * selection, and returns a display_buffer. Failure keeps the old session and
 * mapping intact. Reply storage overlapping the old mapping is BAD_BUFFER.
 * RELEASE unmaps and restores the TTY; exit/fault does the same automatically.
 * Closing/copying a handle does not release/transfer an acquired session.
 * The mapping is not a private-memory allocation; MEMORY_RELEASE cannot free it. */
_Static_assert(sizeof(struct display_buffer) == 64, "display buffer layout");
_Static_assert(sizeof(struct display_replace_request) == 24, "display replace request layout");
_Static_assert(sizeof(struct display_size_reply) == 48, "display size reply layout");

#endif
