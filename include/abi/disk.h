#ifndef ABI_DISK_H
#define ABI_DISK_H

#include <abi/handle.h>
#include <abi/message.h>

#define DISKS_RIGHT_ENUMERATE (UINT64_C(1) << 0)
#define DISKS_RIGHT_OPEN (UINT64_C(1) << 1)
#define DISKS_ENUMERATE UINT64_C(1)
#define DISKS_OPEN UINT64_C(2)

#define DISK_RIGHT_INFO (UINT64_C(1) << 0)
#define DISK_RIGHT_READ (UINT64_C(1) << 1)
#define DISK_RIGHT_WRITE (UINT64_C(1) << 2)
#define DISK_RIGHT_MOUNT (UINT64_C(1) << 3)
#define DISK_RIGHT_RELEASE (UINT64_C(1) << 4)
#define DISK_RIGHTS (DISK_RIGHT_INFO | DISK_RIGHT_READ | DISK_RIGHT_WRITE | \
    DISK_RIGHT_MOUNT | DISK_RIGHT_RELEASE)
#define DISK_INFO UINT64_C(1)
#define DISK_READ UINT64_C(2)
#define DISK_WRITE UINT64_C(3)
#define DISK_FLUSH UINT64_C(4)
#define DISK_RELEASE UINT64_C(5)
#define DISK_OPEN_VOLUME UINT64_C(6)
#define DISK_CREATE_VOLUME UINT64_C(7)
#define DISK_CLAIM UINT64_C(8)
#define DISK_IO_MAX_BYTES 4096u

#define DISK_ACCESS_READ_ONLY UINT64_C(0)
#define DISK_ACCESS_READ_WRITE UINT64_C(1)
#define DISK_FLAG_WRITABLE (UINT64_C(1) << 0)
#define DISK_FLAG_FLUSH_SUPPORTED (UINT64_C(1) << 1)
#define DISK_FLAG_WRITE_FAILED (UINT64_C(1) << 2)
#define DISK_FLAG_MOUNTED (UINT64_C(1) << 3)
#define DISK_FLAG_CLAIMED (UINT64_C(1) << 4)

enum disk_preparation {
  DISK_READY, DISK_UNSUPPORTED, DISK_SETUP_FAILED,
};

enum disk_gpt_status {
  DISK_GPT_PENDING, DISK_GPT_HEALTHY, DISK_GPT_DEGRADED, DISK_GPT_AMBIGUOUS,
  DISK_GPT_ABSENT, DISK_GPT_INVALID, DISK_GPT_UNSUPPORTED, DISK_GPT_UNAVAILABLE,
  DISK_GPT_NO_MEMORY, DISK_GPT_IO_ERROR, DISK_GPT_TIMED_OUT,
};

/* ENUMERATE waits for boot disk discovery, takes a zero-based observed inventory
 * index and returns NOT_FOUND at the end. Lost registry records or incomplete
 * VirtIO bookkeeping refuse inventory; partial USB topology alone does not.
 * Unseen devices cannot be listed. IDs identify physical devices for this boot,
 * independent of mutable GPT GUIDs. Unsupported devices remain listed.
 * INFO uses the same reply. Unknown geometry is zero; GPT GUID is nonzero only
 * when a healthy/degraded GPT is available. GPT fields describe the last
 * completed scan; raw writes refresh them on release, not after each write. */
struct disks_enumerate_request { uint64_t index; };
struct disk_info {
  uint64_t id, preparation, block_count, block_size, flags;
  uint64_t gpt_status;
  uint8_t gpt_guid[16];
};

/* READ_WRITE acquires exclusive raw access: any retained mounted pool on the
 * device or existing claim returns BUSY. READ_ONLY permits inspection only.
 * Success returns INFO/READ/MOUNT; READ_WRITE also grants WRITE/RELEASE.
 * A claim blocks all mounts on this device. WRITE and FLUSH require a live
 * claim. WRITE captures the input before queuing; success does not imply
 * durability. A failed WRITE may have changed a prefix. FLUSH is the durability
 * point. Published uncertain failures
 * latch the device's existing write-failed state; no automatic retry. */
struct disks_open_request { uint64_t id, access; };
struct disk_open_reply { handle_t disk; };
struct disk_read_request { uint64_t offset, length; };
struct disk_write_request { uint64_t offset, length, data; };

/* READ/WRITE lengths are nonzero, at most DISK_IO_MAX_BYTES, and both offset
 * and length must align to the device's logical block size. READ returns the
 * requested bytes only on success. WRITE/FLUSH/RELEASE return no payload.
 * RELEASE flushes then rescans GPT before relinquishing exclusivity. It ends
 * raw mutation authority on all aliases of this object; failure is reported,
 * not rollback. Last-object close also releases through worker cleanup.
 * OPEN_VOLUME uses mount_volume_request/reply and the normal npfs mount path,
 * outside any raw claim on that partition. Read-only directory rights need
 * MOUNT; mutation rights also need WRITE, which only READ_WRITE opening grants.
 * CREATE_VOLUME needs MOUNT and WRITE: it appends a live volume with an empty
 * root to the npfs pool in PARTITION, in one committed transaction, with an
 * identity from the kernel's entropy source. A taken name is ALREADY_EXISTS
 * and a full catalog LIMIT; no reply. Closing the disk does not revoke a
 * returned root. The raw API never authorizes a target by
 * checking its contents; marker consent belongs to the trusted installer. */

struct disk_create_volume_request { uint64_t partition, name, name_length; };

/* CLAIM needs WRITE and re-establishes raw mutation authority on an object
 * whose claim was released. PARTITION zero claims the whole disk, under
 * READ_WRITE opening's rules. A one-based GPT entry claims only that
 * partition: no other claim may exist on the device and that partition must
 * not hold a mounted pool, though other partitions' pools may stay mounted.
 * WRITE is then confined to the partition's blocks, mounts of it are refused,
 * and RELEASE flushes without a GPT rescan, since the table cannot change. */
struct disk_claim_request { uint64_t partition; };

_Static_assert(sizeof(struct disk_info) == 64, "disk info layout");
_Static_assert(sizeof(struct disk_create_volume_request) == 24, "disk create volume layout");
_Static_assert(sizeof(struct disk_claim_request) == 8, "disk claim layout");
_Static_assert(sizeof(struct disks_open_request) == 16, "disk open layout");
_Static_assert(sizeof(struct disk_read_request) == 16, "disk read layout");
_Static_assert(sizeof(struct disk_write_request) == 24, "disk write layout");

#endif
