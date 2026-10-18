#ifndef ABI_SCREENSHOT_SAVER_H
#define ABI_SCREENSHOT_SAVER_H

#include <abi/message.h>
#include <abi/space.h>

#define SCREENSHOT_SAVER_RIGHT_SERVE (UINT64_C(1) << 0)
#define SCREENSHOT_SAVER_NEXT UINT64_C(1)
#define SCREENSHOT_SAVER_CAPTURE UINT64_C(2)
#define SCREENSHOT_SAVER_COMPLETE UINT64_C(3)
#define SCREENSHOT_SAVER_FILENAME_MAX 127

/* Private to the space's session-started saver. NEXT is nonblocking and returns
 * WOULD_BLOCK without a pending physical request; WAIT_MANY READABLE waits for
 * one. Success returns this request identity and the fixed space name.
 * CAPTURE takes screenshot_saver_capture_request and returns screen_capture_reply.
 * It requires the separately supplied CAPTURE grant, and cancels if the shown
 * space changed since the key press. COMPLETE accepts only the matching request;
 * status is an ordinary call_status. A successful filename is a basename only.
 * Neither operation implies directory or capture authority. A zero-right copy
 * keeps storage only; the last SERVE close makes the registration unavailable.
 * READABLE requires SERVE and the caller's own space; no queued key is reserved
 * by readiness. NEXT consumes delivery, CAPTURE permits one attempt, and a
 * completion consumes that request. Stale or switched requests return DENIED. */
struct screenshot_saver_pending {
  uint64_t request;
  char space[SPACE_NAME_MAX + 1];
};

struct screenshot_saver_capture_request {
  struct message_header header;
  uint64_t request;
  uint64_t capture;
};

struct screenshot_saver_complete_request {
  struct message_header header;
  uint64_t request;
  uint64_t status;
  uint64_t filename;
  uint64_t filename_length;
};

_Static_assert(sizeof(struct screenshot_saver_pending) == 40, "saver pending layout");
_Static_assert(sizeof(struct screenshot_saver_capture_request) == 32, "saver capture layout");
_Static_assert(sizeof(struct screenshot_saver_complete_request) == 48, "saver completion layout");

#endif
