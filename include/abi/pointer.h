#ifndef ABI_POINTER_H
#define ABI_POINTER_H

#include <abi/message.h>

#define POINTER_RIGHT_INPUT (UINT64_C(1) << 0)
#define POINTER_ACQUIRE UINT64_C(1)
#define POINTER_READ UINT64_C(2)
#define POINTER_RELEASE UINT64_C(3)
#define POINTER_GEOMETRY UINT64_C(4)
#define POINTER_SET_IMAGE UINT64_C(5)
#define POINTER_VISIBILITY UINT64_C(6)
#define POINTER_WARP UINT64_C(7)
#define POINTER_DEFAULT_IMAGE UINT64_C(8)
#define POINTER_LOCK UINT64_C(9)
#define POINTER_UNLOCK UINT64_C(10)
#define POINTER_STATE UINT64_C(11)
#define POINTER_READ_POLL (UINT64_C(1) << 0)
#define POINTER_EVENT_FOCUSED (UINT32_C(1) << 0)
#define POINTER_EVENT_LOCKED (UINT32_C(1) << 1)
#define POINTER_BUTTON_LEFT (UINT32_C(1) << 0)
#define POINTER_BUTTON_RIGHT (UINT32_C(1) << 1)
#define POINTER_BUTTON_MIDDLE (UINT32_C(1) << 2)
#define POINTER_IMAGE_MAX 64

enum pointer_event_type {
  POINTER_INPUT,
  POINTER_STATE_RESET,
  POINTER_FOCUS_GAINED,
  POINTER_FOCUS_LOST,
  POINTER_ENTER,
  POINTER_LEAVE,
  POINTER_GEOMETRY_CHANGED,
  POINTER_LOCK_CHANGED,
  POINTER_ACTIVATED,
};

/* INPUT authority plus ownership of this space's graphics session is required.
 * Acquisition is process-owned and exclusive, even for repeat acquisition.
 * Handle copies/closure do not transfer/release ownership. Display release or
 * owner exit ends the subscription and its cursor preference. Acquisition does
 * not capture keyboard input, change the selected layer or lock the pointer. */
struct pointer_read_request {
  struct message_header header;
  uint64_t flags; /* Zero blocks; POLL returns TIMED_OUT when empty. */
};

/* Signed surface-local pixels, below navigation. During an anchored drag they
 * may be outside the surface. Wheel is positive toward the user. State events
 * clear held buttons except locked geometry changes, which retain them. Ordinary
 * geometry changes discard queued spatial events. INPUT
 * coalesces only within identical geometry/button state, keeping the latest
 * position and saturating wheel/relative counts. Lost transitions cause RESET.
 * A held device button is accepted only after release and a fresh press. */
struct pointer_event {
  int64_t x, y;
  uint64_t generation, mapping_identity;
  int32_t wheel;
  int32_t dx, dy; /* Relative device counts only for INPUT with LOCKED set. */
  uint32_t buttons, type, flags;
};

/* Header-only LOCK/UNLOCK require the owning ordinary subscription. LOCK may
 * be refused; it never selects a space/layer. STATE replies with current FOCUSED
 * and LOCKED flags in a uint64_t. Lock changes discard queued input and clear
 * accepted buttons. LOCKED INPUT carries dx/dy; x/y remains the parked position.
 * Super+Esc and focus/device loss revoke lock. A consumed fresh surface click
 * queues ACTIVATED when it permits one subsequent request; polling does not
 * recreate permission across session or process lifetimes. */

/* Destination and fixed mapping extents are distinct. Spatial requests use both
 * identities; same-size REPLACE still changes mapping_identity. */
struct pointer_geometry {
  uint64_t width, height;
  uint64_t generation, mapping_identity;
  uint64_t mapping_width, mapping_height;
};

/* Tightly packed BGRA8, straight alpha, copied before return. Dimensions 1..64,
 * hotspot inside the image. Failure preserves the previous image. DEFAULT_IMAGE
 * is header-only and restores the default without changing saved visibility. */
struct pointer_image_request {
  struct message_header header;
  uint64_t address;
  uint32_t width, height, hotspot_x, hotspot_y;
};

struct pointer_visibility_request {
  struct message_header header;
  uint64_t visible; /* Only zero or one. Hiding retains the selected image. */
};

/* Only shown, focused graphics may warp within mapping/destination intersection.
 * Stale identities return BUSY; other ineligible or out-of-range requests fail.
 * Success queues ordinary position, never a device press or user activation. */
struct pointer_warp_request {
  struct message_header header;
  int64_t x, y;
  uint64_t generation, mapping_identity;
};

_Static_assert(sizeof(struct pointer_read_request) == 24, "pointer read layout");
_Static_assert(sizeof(struct pointer_event) == 56, "pointer event layout");
_Static_assert(sizeof(struct pointer_geometry) == 48, "pointer geometry layout");
_Static_assert(sizeof(struct pointer_image_request) == 40, "pointer image layout");
_Static_assert(sizeof(struct pointer_visibility_request) == 24, "pointer visibility layout");
_Static_assert(sizeof(struct pointer_warp_request) == 48, "pointer warp layout");

#endif
