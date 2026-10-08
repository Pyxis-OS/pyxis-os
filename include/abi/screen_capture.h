#ifndef ABI_SCREEN_CAPTURE_H
#define ABI_SCREEN_CAPTURE_H

#include <abi/handle.h>
#include <abi/message.h>

#define SCREEN_CAPTURE_RIGHT_CAPTURE (UINT64_C(1) << 0)
#define SCREEN_CAPTURE_FRAME UINT64_C(1)

/* FRAME is header-only and requires CAPTURE, independently of per-space DRAW.
 * Success transfers an owned READ-only FILE and this metadata together. Pixels
 * are native 32-bit words, with tightly packed rows and no device/page padding.
 * The full local screen includes navigation, the shown layer and visible cursor.
 * Bytes freeze one successful presenter composition, including any existing
 * single-buffer tearing; they do not promise an atomic application frame/vblank.
 * generation identifies the geometry at the captured frame boundary.
 * Close the file to release the snapshot; copies retain the same immutable bytes.
 * Only one pending/in-flight capture is admitted globally; others return BUSY.
 * Allocation refusal returns NO_MEMORY, unsupported extent LIMIT, and backend
 * failure UNAVAILABLE. Completed files have ordinary FILE reference lifetimes. */
struct screen_capture_reply {
  handle_t file;
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

_Static_assert(sizeof(struct screen_capture_reply) == 64, "screen capture reply layout");

#endif
