#ifndef ABI_MOUNT_H
#define ABI_MOUNT_H

#include <abi/handle.h>
#include <abi/message.h>

#define MOUNT_RIGHT_OPEN_ROOT (UINT64_C(1) << 0)
#define MOUNT_OPEN_ROOT UINT64_C(1)

/* Header-only request, replies with one owned directory handle. The authority
 * selects a single host export; there is no device/tag/path selector.
 * The directory grants LOOKUP | ENUMERATE | READ_FILES. Closing this authority
 * does not revoke returned directories or files. Initialization may block,
 * bounded by the transport's startup deadline; failures return a call error. */
struct mount_reply {
  handle_t root;
};

_Static_assert(sizeof(struct mount_reply) == 8, "mount reply layout");

#endif
