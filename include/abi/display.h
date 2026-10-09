#ifndef ABI_DISPLAY_H
#define ABI_DISPLAY_H

#include <abi/message.h>

#define DISPLAY_RIGHT_DRAW (UINT64_C(1) << 0)

#define DISPLAY_ACQUIRE UINT64_C(1)
/* Operation 2 was PRESENT, a continuously sampled mapping; SUBMIT replaced it. */
#define DISPLAY_RELEASE UINT64_C(3)
#define DISPLAY_SIZE UINT64_C(4)
#define DISPLAY_REPLACE UINT64_C(5)
#define DISPLAY_SUBMIT UINT64_C(6)

#define DISPLAY_SLOT_COUNT 3

/* REPLACE includes the expected current destination generation and SUBMIT the
 * finished slot; the other requests are just a message_header. */
struct display_replace_request {
  struct message_header header;
  uint64_t generation;
};

struct display_submit_request {
  struct message_header header;
  uint64_t slot;
};

/* SUBMIT's reply: a slot the program holds, to render next, and whether this
 * submission replaced a pending frame that was never shown. */
struct display_submit_reply {
  uint64_t next;
  uint64_t dropped;
};

/* ACQUIRE and REPLACE return this layout and slot_count zeroed frames, each
 * page-rounded, writable and NX; slot i starts at address + i * size. Pixels
 * are native uint32_t words with three 8-bit channels at the returned shifts;
 * other bits are zero. pitch is bytes per row; size includes page padding, not
 * additional pixels. Dimensions cover the space below the navigation bar. The
 * layout stays fixed until REPLACE or RELEASE; generation identifies its
 * geometry. Every slot starts held by the program; render slot 0 first. */
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
  uint32_t slot_count;
};

/* Current destination below navigation, returned atomically with its generation.
 * SIZE needs DRAW but no acquired session; it grants no mode-setting authority.
 * A screen resize clips existing slots to this destination without changing
 * those slots. Generation starts at 1 and advances on each committed resize. */
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
 *
 * Frames are handed over whole. The program renders a complete frame into a
 * slot it holds and SUBMITs it; that slot becomes the pending frame, and the
 * program must not write it again until a SUBMIT reply names it. At each
 * presentation the presenter takes the pending frame, if any, as the current
 * one and repaints from it until another replaces it; the previous current
 * slot then returns to the program. A SUBMIT while another frame is pending
 * replaces it: the replaced frame is never shown, its slot returns at once and
 * the reply sets dropped. A slot is never returned while pending or current,
 * and every reply names a held slot. Slots keep the frame they last held, so
 * redraw every pixel. An unknown slot, or one the program does not hold, is
 * BAD_REQUEST and changes nothing. No vblank or tear-free scanout guarantee.
 *
 * The first SUBMIT makes the session available for presentation. Later SUBMIT
 * and REPLACE preserve the user's graphics/terminal layer choice.
 * SUBMIT/REPLACE/RELEASE require the acquiring process; RELEASE returns no
 * reply bytes. REPLACE returns BUSY on a generation mismatch and otherwise maps
 * a zeroed current-size slot set at a disjoint address, every slot held and
 * nothing pending; the frame on screen stays until the next SUBMIT replaces it.
 * Success invalidates the old pointers before returning, preserves ownership
 * and visible selection, and returns a display_buffer. Failure keeps the old
 * session and slots intact. Reply storage overlapping the old slots is BAD_BUFFER.
 * RELEASE unmaps and restores the TTY; exit/fault does the same automatically.
 * Closing/copying a handle does not release/transfer an acquired session.
 * The slots are not a private-memory allocation; MEMORY_RELEASE cannot free them. */
_Static_assert(sizeof(struct display_buffer) == 64, "display buffer layout");
_Static_assert(sizeof(struct display_replace_request) == 24, "display replace request layout");
_Static_assert(sizeof(struct display_submit_request) == 24, "display submit request layout");
_Static_assert(sizeof(struct display_submit_reply) == 16, "display submit reply layout");
_Static_assert(sizeof(struct display_size_reply) == 48, "display size reply layout");

#endif
