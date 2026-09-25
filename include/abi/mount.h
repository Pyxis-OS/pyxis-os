#ifndef ABI_MOUNT_H
#define ABI_MOUNT_H

#include <abi/handle.h>
#include <abi/message.h>

#define MOUNT_RIGHT_OPEN_ROOT (UINT64_C(1) << 0)
#define MOUNT_OPEN_ROOT UINT64_C(1)

#define MOUNT_ACCESS_READ_ONLY UINT64_C(0)
#define MOUNT_ACCESS_READ_WRITE UINT64_C(1)

/* Selects the returned grant, never a global backend mode. READ_ONLY grants
 * LOOKUP | ENUMERATE | READ_FILES; READ_WRITE grants all DIRECTORY_RIGHTS.
 * Write authority permits attempts, not a promise of backend/host writability.
 * No device/tag/path selector: this authority selects one host export.
 * Closing it does not revoke returned handles. Initialization may block up to
 * the transport startup deadline; failure returns a call error. */
struct mount_open_request {
  uint64_t access;
};

struct mount_message {
  struct message_header header;
  struct mount_open_request body;
};

struct mount_reply {
  handle_t root;
};

_Static_assert(sizeof(struct mount_open_request) == 8, "mount request layout");
_Static_assert(sizeof(struct mount_message) == 24, "mount message layout");
_Static_assert(sizeof(struct mount_reply) == 8, "mount reply layout");

#endif
