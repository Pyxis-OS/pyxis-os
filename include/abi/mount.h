#ifndef ABI_MOUNT_H
#define ABI_MOUNT_H

#include <abi/handle.h>
#include <abi/message.h>

#define MOUNT_RIGHT_OPEN_ROOT (UINT64_C(1) << 0)
#define MOUNT_RIGHT_OBSERVE (UINT64_C(1) << 1)
#define MOUNT_RIGHT_WRITE (UINT64_C(1) << 2)
#define MOUNT_RIGHT_RETIRE (UINT64_C(1) << 3)
#define MOUNT_RIGHT_INTERVAL (UINT64_C(1) << 4)
#define MOUNT_OPEN_ROOT UINT64_C(1)
#define MOUNT_OPEN_VOLUME UINT64_C(2)
#define MOUNT_SYNC UINT64_C(3)
#define MOUNT_OPEN_FAT32 UINT64_C(4)
#define MOUNT_OBSERVE UINT64_C(5)
#define MOUNT_RETIRE UINT64_C(6)
#define MOUNT_BEGIN UINT64_C(7)
#define MOUNT_END UINT64_C(8)
#define MOUNT_VOLUME_NAME_MAX 255u

#define MOUNT_ACCESS_READ_ONLY UINT64_C(0)
#define MOUNT_ACCESS_READ_WRITE UINT64_C(1)

/* Writable native pools need room for the largest namespace transaction.
 * Raw installers use this same requirement when checking pool compatibility. */
#define MOUNT_NPFS_MIN_JOURNAL_IMAGES 18u

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

/* Boot ESP remains RO. Bound FAT OPEN_ROOT requests exact ceiling rights;
 * mutation-capable roots additionally require control WRITE. RO admission
 * returns OPEN_ROOT|OBSERVE|RETIRE; RW also returns WRITE|INTERVAL. Final control
 * close does not retire. RETIRE is BUSY until data/operations/intervals drain.
 * Retirement never revokes. SYNC is volume-wide and does not clear dirty.
 * BEGIN/END need INTERVAL. Ownership is per process, not the control/child;
 * a non-extending 30-second lease excludes other mutation/sync, not reads.
 * Expired owners must acknowledge END or BEGIN anew before further mutation.
 * OBSERVE remains available after failure/retirement; no implicit replay. */
struct mount_fat32_root_request { uint64_t rights; };
struct mount_fat32_reply { handle_t root, control; };
struct mount_interval_reply { uint64_t id, deadline_ns; };
struct mount_interval_request { uint64_t id; };
#define MOUNT_STATE_RETIRED (UINT64_C(1) << 0)
#define MOUNT_STATE_CLEAN_SHUTDOWN (UINT64_C(1) << 1)
#define MOUNT_STATE_NO_HARD_ERROR (UINT64_C(1) << 2)
#define MOUNT_STATE_READ_WRITE (UINT64_C(1) << 3)
#define MOUNT_STATE_DIRTY_OWNED (UINT64_C(1) << 4)
#define MOUNT_STATE_FAILED (UINT64_C(1) << 5)
#define MOUNT_STATE_INTERVAL (UINT64_C(1) << 6)
#define MOUNT_STATE_CLEANUP_PENDING (UINT64_C(1) << 7)
/* failure_phase is the shared FAT engine's fat32_mounted_phase when FAILED;
 * failed_sector is the absolute backing sector, or UINT64_MAX for a flush.
 * Names are diagnostic components,
 * never authority or paths to automatically replay. */
struct mount_state_reply {
  uint64_t domain, device, partition, first_block, block_count, flags;
  uint64_t failure_status, failure_phase, failed_sector;
  uint64_t interval_id, interval_deadline_ns, detached_objects;
  char source_name[256], destination_name[256];
};
_Static_assert(sizeof(struct mount_fat32_root_request) == 8, "FAT root request");
_Static_assert(sizeof(struct mount_fat32_reply) == 16, "FAT mount pair");
_Static_assert(sizeof(struct mount_interval_reply) == 16, "mount interval reply");
_Static_assert(sizeof(struct mount_interval_request) == 8, "mount interval request");
_Static_assert(sizeof(struct mount_state_reply) == 608, "mount state");

_Static_assert(sizeof(struct mount_open_request) == 8, "mount request layout");
_Static_assert(sizeof(struct mount_message) == 24, "mount message layout");
_Static_assert(sizeof(struct mount_volume_request) == 32, "mount volume request layout");
_Static_assert(sizeof(struct mount_volume_message) == 48, "mount volume message layout");
_Static_assert(sizeof(struct mount_reply) == 8, "mount reply layout");

#endif
