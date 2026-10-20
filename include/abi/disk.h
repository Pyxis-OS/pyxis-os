#ifndef ABI_DISK_H
#define ABI_DISK_H

#include <abi/handle.h>
#include <abi/message.h>

#define DISKS_RIGHT_ENUMERATE (UINT64_C(1) << 0)
#define DISKS_RIGHT_OPEN (UINT64_C(1) << 1)
#define DISKS_RIGHT_MOUNT_READ_ONLY (UINT64_C(1) << 2)
#define DISKS_RIGHT_MOUNT_READ_WRITE (UINT64_C(1) << 3)
#define DISKS_RIGHT_OPEN_READ_ONLY (UINT64_C(1) << 4)
#define DISKS_RIGHT_OPEN_POOL (UINT64_C(1) << 5)
#define DISKS_RIGHT_APPEND_POOL (UINT64_C(1) << 6)
#define DISKS_RIGHT_ERASE (UINT64_C(1) << 7)
#define DISKS_RIGHT_OPEN_PARTITION (UINT64_C(1) << 8)
#define DISKS_RIGHT_REBUILD_ESP (UINT64_C(1) << 9)
#define DISKS_RIGHT_OPEN_OWNED_ESP (UINT64_C(1) << 10)
#define DISKS_RIGHTS (DISKS_RIGHT_ENUMERATE | DISKS_RIGHT_OPEN | \
    DISKS_RIGHT_MOUNT_READ_ONLY | DISKS_RIGHT_MOUNT_READ_WRITE | \
    DISKS_RIGHT_OPEN_READ_ONLY | DISKS_RIGHT_OPEN_POOL | \
    DISKS_RIGHT_APPEND_POOL | DISKS_RIGHT_ERASE | DISKS_RIGHT_OPEN_PARTITION | \
    DISKS_RIGHT_REBUILD_ESP | DISKS_RIGHT_OPEN_OWNED_ESP)
#define DISKS_ENUMERATE UINT64_C(1)
#define DISKS_OPEN UINT64_C(2)
#define DISKS_OPEN_FAT32 UINT64_C(3)
#define DISKS_REVIEW_ERASE UINT64_C(4)
#define DISKS_REVIEW_APPEND UINT64_C(5)
#define DISKS_OPEN_POOL UINT64_C(6)
#define DISKS_OPEN_PARTITION UINT64_C(7)
#define DISKS_REVIEW_ESP UINT64_C(8)
#define DISKS_OPEN_OWNED_ESP UINT64_C(9)

#define DISK_RIGHT_INFO (UINT64_C(1) << 0)
#define DISK_RIGHT_READ (UINT64_C(1) << 1)
#define DISK_RIGHT_WRITE (UINT64_C(1) << 2)
#define DISK_RIGHT_MOUNT (UINT64_C(1) << 3)
#define DISK_RIGHT_RELEASE (UINT64_C(1) << 4)
#define DISK_RIGHT_BEGIN_ERASE (UINT64_C(1) << 5)
#define DISK_RIGHT_APPEND_POOL (UINT64_C(1) << 6)
#define DISK_RIGHT_REBUILD_ESP (UINT64_C(1) << 7)
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
#define DISK_OPEN_FAT32 UINT64_C(9)
#define DISK_PARTITION_INFO UINT64_C(10)
#define DISK_BEGIN_ERASE UINT64_C(11)
#define DISK_APPEND_POOL UINT64_C(12)
#define DISK_BEGIN_ESP_REBUILD UINT64_C(13)
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

/* READ_WRITE acquires exclusive raw access: any pending or active mounted extent on the
 * device or existing claim returns BUSY. READ_ONLY permits inspection only.
 * Success returns INFO/READ/MOUNT; READ_WRITE also grants WRITE/RELEASE.
 * A claim blocks overlapping mount admissions. WRITE and FLUSH require a live
 * claim. WRITE captures the input before queuing; success does not imply
 * durability. A failed WRITE may have changed a prefix. FLUSH is the durability
 * point. Published uncertain failures
 * latch the device's existing write-failed state; no automatic retry. */
struct disks_open_request { uint64_t id, access; };
struct disk_open_reply { handle_t disk; };

/* A reviewed selection names logical media, not authenticated hardware. Both
 * GPT copies must agree for partition-first opening. OPEN_POOL requires its
 * own right and the native pool type; it cannot open an ESP for raw writes.
 * OPEN_PARTITION is separate raw qualification authority. Both return an
 * already claimed DISK; its immutable binding survives RELEASE and aliases.
 * Raw offsets remain device-relative, but all access is confined to the selected
 * extent. READ requires a live claim, including after RELEASE; successful
 * reclaim revalidates the immutable binding before restoring raw data access.
 * The separate raw-qualification form also permits read-only access to
 * its retained GPT metadata, so the strict FAT inspector can validate admission;
 * it grants no other outside read. Pool volume operations cannot select another
 * partition, and pool handles grant no raw FAT opening. */
struct disk_partition_selection {
  uint64_t id, partition, first_block, block_count;
  uint64_t device_block_size, device_block_count, attributes;
  uint8_t disk_guid[16], partition_guid[16], partition_type[16];
};
struct disk_partition_info_request { uint64_t partition; };

/* Review captures geometry, MBR, both complete GPT header sectors and bounded
 * rounded arrays before the installer displays the plan. No claim survives
 * preparation. READ on the returned review object serves those retained bytes;
 * other sectors remain live reads. INFO reports its retained geometry/GPT facts.
 * The expected GUID must match when a comparable GPT identity is available.
 * Absent/semantically damaged GPT is capturable only with bounded framing.
 * The explicit issuer right permits preparation; ordinary READ does not.
 * Returned review rights are permanently INFO/READ plus BEGIN_ERASE or APPEND.
 * No mount or raw-write authority is conferred by a review object. */
struct disks_review_request {
  uint64_t id, block_size, block_count;
  uint8_t disk_guid[16];
};

/* Trusted UI is responsible for Review and obtaining consent; the kernel checks
 * this exact word and the explicitly delegated, single-use review authority.
 * BEGIN_ERASE consumes that review across aliases, takes whole-device exclusion
 * and compares every retained byte/geometry before returning a separate claimed
 * writer. Any difference refuses; prepare/review again rather than retry.
 * It writes no data itself. The writer can never CLAIM again after RELEASE and
 * has no MOUNT right. Coexistence profiles receive no erase issuer right.
 * The grant authorizes destructive classic installation, including foreign OS
 * partitions; ordinary whole-disk opening does not bypass its GPT refusal. */
#define DISK_ERASE_CONFIRMATION "wipe"
struct disk_begin_erase_request { uint64_t confirmation, confirmation_length; };

/* REVIEW_ESP requires REBUILD_ESP and takes disk_partition_selection, with the
 * ESP type and an exact healthy GPT selection. It reserves only that extent
 * while capturing geometry and both complete GPT copies; disjoint native pools
 * may stay mounted. No claim survives review preparation. Its permanent rights
 * are INFO/READ/REBUILD_ESP. Review READ permits only selected ESP contents and
 * retained GPT metadata; other live extents are denied. It does not infer
 * ownership from files or names.
 * BEGIN_ESP_REBUILD checks this exact confirmation, consumes the review across
 * aliases, reserves its ESP and compares every retained GPT byte and geometry.
 * Success returns disk_open_reply with INFO/READ/WRITE/RELEASE only. Raw offsets
 * remain device-relative; READ and WRITE require its live claim and stay within
 * the immutable ESP extent. The writer cannot MOUNT or CLAIM again after
 * RELEASE. Neither operation formats or writes filesystem data itself. */
#define DISK_ESP_REBUILD_CONFIRMATION "update"
struct disk_begin_esp_rebuild_request { uint64_t confirmation, confirmation_length; };

enum disk_append_phase {
  DISK_APPEND_PREPARE, DISK_APPEND_MATCH, DISK_APPEND_BACKUP_ARRAY,
  DISK_APPEND_BACKUP_HEADER, DISK_APPEND_BACKUP_FLUSH, DISK_APPEND_PRIMARY_ARRAY,
  DISK_APPEND_PRIMARY_HEADER, DISK_APPEND_FINAL_FLUSH, DISK_APPEND_VERIFY,
  DISK_APPEND_COMPLETE,
};
#define DISK_APPEND_EFFECTS_POSSIBLE UINT64_C(1)

/* APPEND consumes a reviewed healthy GPT and adds only this native-pool entry.
 * Only its slot and required CRCs may change. Success returns an already claimed,
 * partition-bound pool DISK. The reply carries phase/selection even on a worker
 * failure; disk is then invalid. Uncertain effects retain device exclusion until
 * reboot, regardless of handle closure. No rollback, retry or hidden second
 * append. Bad reply delivery is outcome unknown to the caller. */
struct disk_append_reply {
  handle_t disk;
  uint64_t phase, flags, failed_block;
  struct disk_partition_selection partition;
};
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

/* Select one GPT entry and fixed RO/RW instance mode. MOUNT is required;
 * READ_WRITE also requires WRITE even when returning an RO root. Exact rights
 * are attenuated DIRECTORY_CONTENT_RIGHTS. Existing RO instances cannot upgrade.
 * Both GPT copies/full structure validate under extent exclusion. */
struct disk_fat32_request { uint64_t partition, access, rights; };
_Static_assert(sizeof(struct disk_fat32_request) == 24, "FAT disk request");

/* Mount-only inventory authority returns mount_fat32_reply without a raw DISK
 * handle or claim. READ_ONLY accepts either mount right; READ_WRITE requires
 * MOUNT_READ_WRITE even for an attenuated RO root. Selection uses a boot-scoped
 * device ID and one-based GPT entry. A nonzero expected_disk_guid must match
 * the current healthy GPT at worker admission, or the call returns NOT_FOUND;
 * zero selects by ID alone. Root rights, instance mode, exclusion and
 * publication match DISK_OPEN_FAT32 above.
 * These rights confer no raw access, formatting or repair authority. */
struct disks_fat32_request {
  uint64_t id, partition, access, rights;
  uint8_t expected_disk_guid[16];
};
_Static_assert(sizeof(struct disks_fat32_request) == 48, "FAT inventory request");

/* Separate mounted authority for installer/updater publication, independent of
 * raw review/rebuild and ordinary configured FAT mounts. The complete reviewed
 * selection must still match healthy GPT, device geometry and the ESP type.
 * Returns mount_fat32_reply. Rights/mode and control lifetime follow ordinary
 * FAT mounts, but all returned roots and descendants retain mutation confinement:
 * EFI/pyxis and boot/limine trees plus the exact regular file boot/limine.conf.
 * Traversal parents admit only creation of the named directory parents. Foreign
 * entries remain readable; requested foreign file WRITE is DENIED. Canonical
 * names determine scope, so an 8.3 alias cannot confer ownership. Renaming an
 * owned file outside this scope or replacing a foreign entry is DENIED.
 * Scoped and ordinary instances cannot share wrappers: a live instance of the
 * other policy on the selected extent returns BUSY. No raw access is issued. */
struct disks_owned_esp_request {
  struct disk_partition_selection selection;
  uint64_t access, rights;
};
_Static_assert(sizeof(struct disks_owned_esp_request) == 120, "owned ESP request");

/* CLAIM needs WRITE and re-establishes raw mutation authority on an object
 * whose claim was released. PARTITION zero claims the whole disk, under
 * READ_WRITE opening's rules. A one-based GPT entry claims only that
 * partition: no other claim may exist on the device and that partition must
 * not overlap any pending or active mount, though disjoint partitions may stay mounted.
 * WRITE is then confined to the partition's blocks, mounts of it are refused,
 * and RELEASE flushes without a GPT rescan, since the table cannot change. */
struct disk_claim_request { uint64_t partition; };

_Static_assert(sizeof(struct disk_info) == 64, "disk info layout");
_Static_assert(sizeof(struct disk_partition_selection) == 104, "partition selection layout");
_Static_assert(sizeof(struct disk_partition_info_request) == 8, "partition info request");
_Static_assert(sizeof(struct disks_review_request) == 40, "disk review request");
_Static_assert(sizeof(struct disk_begin_erase_request) == 16, "erase consent request");
_Static_assert(sizeof(struct disk_begin_esp_rebuild_request) == 16, "ESP rebuild consent request");
_Static_assert(sizeof(struct disk_append_reply) == 136, "append result layout");
_Static_assert(sizeof(struct disk_create_volume_request) == 24, "disk create volume layout");
_Static_assert(sizeof(struct disk_claim_request) == 8, "disk claim layout");
_Static_assert(sizeof(struct disks_open_request) == 16, "disk open layout");
_Static_assert(sizeof(struct disk_read_request) == 16, "disk read layout");
_Static_assert(sizeof(struct disk_write_request) == 24, "disk write layout");

#endif
