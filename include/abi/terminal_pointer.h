#ifndef ABI_TERMINAL_POINTER_H
#define ABI_TERMINAL_POINTER_H

#include <abi/pointer.h>

#define TERMINAL_POINTER_RIGHT_CONTROL (UINT64_C(1) << 0)
/* The separate terminal protocol shares image and spatial record layouts. */
#define TERMINAL_POINTER_ACQUIRE POINTER_ACQUIRE
#define TERMINAL_POINTER_READ POINTER_READ
#define TERMINAL_POINTER_RELEASE POINTER_RELEASE
#define TERMINAL_POINTER_GEOMETRY POINTER_GEOMETRY
#define TERMINAL_POINTER_SET_IMAGE POINTER_SET_IMAGE
#define TERMINAL_POINTER_VISIBILITY POINTER_VISIBILITY
#define TERMINAL_POINTER_DEFAULT_IMAGE POINTER_DEFAULT_IMAGE
#define TERMINAL_POINTER_STATE POINTER_STATE
#define TERMINAL_POINTER_VIEW_CHANGED UINT64_C(12)
#define TERMINAL_POINTER_CLIPBOARD_REFUSE UINT64_C(13)

/* CONTROL may discard its native action even when the selected layer grant
 * was withheld. This operation confers no store access. */
struct terminal_pointer_clipboard_refuse_request {
  struct message_header header;
  uint64_t action_id, generation, mapping_identity, operation;
};

/* Exclusive process ownership of the local outer terminal. Handle closure or
 * copies never release or transfer it; release/exit restores kernel handling.
 * Control confers no graphics ownership, lock, warp or terminal creation. */
struct terminal_pointer_geometry {
  struct pointer_geometry surface;
  uint64_t columns, rows, cell_width, cell_height;
};

/* Before changing a pane layout or history view, validate both identities and
 * advance the terminal view identity. Discard pending spatial input/drag and
 * require fresh physical presses. BUSY preserves the view; re-query geometry.
 * Success replies with terminal_pointer_geometry for the new view. */
struct terminal_pointer_view_request {
  struct message_header header;
  uint64_t generation, mapping_identity;
};

_Static_assert(sizeof(struct terminal_pointer_geometry) == 80, "terminal pointer geometry layout");
_Static_assert(sizeof(struct terminal_pointer_view_request) == 32, "terminal pointer view layout");

#endif
