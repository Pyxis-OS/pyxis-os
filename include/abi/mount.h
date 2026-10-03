#ifndef ABI_MOUNT_H
#define ABI_MOUNT_H

#include <abi/handle.h>
#include <abi/message.h>

#define MOUNT_RIGHT_OPEN_ROOT (UINT64_C(1) << 0)
#define MOUNT_RIGHT_OBSERVE (UINT64_C(1) << 1)
#define MOUNT_RIGHT_WRITE (UINT64_C(1) << 2)
#define MOUNT_OPEN_ROOT UINT64_C(1)
#define MOUNT_OPEN_VOLUME UINT64_C(2)
#define MOUNT_SYNC UINT64_C(3)
#define MOUNT_VOLUME_NAME_MAX 255u

#define MOUNT_ACCESS_READ_ONLY UINT64_C(0)
#define MOUNT_ACCESS_READ_WRITE UINT64_C(1)

/* Selects the returned grant, never a global backend mode. READ_ONLY grants
 * LOOKUP | ENUMERATE | READ_FILES; READ_WRITE grants all DIRECTORY_CONTENT_RIGHTS.
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

/* Native authority selects one configured disk. Partition is a
 * one-based GPT entry; name is counted UTF-8, captured before work is queued.
 * Rights are exact DIRECTORY rights, including LOOKUP. FILESYSTEM_INFO also
 * requires OBSERVE on the mount authority; it grants no content rights.
 * Mutation rights also require WRITE on the mount authority; unknown bits fail
 * BAD_REQUEST. Success owns an independent root. No device, binding name or
 * raw-block address is caller supplied. WRITE permits attempts; device and
 * filesystem feature restrictions can still return READ_ONLY. */
struct mount_volume_request {
  uint64_t partition;
  uint64_t name;
  uint64_t name_length;
  uint64_t rights;
};

struct mount_volume_message {
  struct message_header header;
  struct mount_volume_request body;
};

/* Native-only SYNC requires WRITE, an empty request and no reply. It commits
 * dirty data and metadata in every mounted pool on this authority's configured
 * disk. Closing a handle does not synchronize it. A failure can follow durable
 * commits in other pools; callers must inspect the error, not assume rollback. */

struct mount_reply {
  handle_t root;
};

_Static_assert(sizeof(struct mount_open_request) == 8, "mount request layout");
_Static_assert(sizeof(struct mount_message) == 24, "mount message layout");
_Static_assert(sizeof(struct mount_volume_request) == 32, "mount volume request layout");
_Static_assert(sizeof(struct mount_volume_message) == 48, "mount volume message layout");
_Static_assert(sizeof(struct mount_reply) == 8, "mount reply layout");

#endif
