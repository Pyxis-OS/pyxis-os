#ifndef KERNEL_GPT_H
#define KERNEL_GPT_H

#include <stdint.h>
#include <abi/syscall.h>
#include <kernel/block.h>

#define GPT_PARTITION_LIMIT 256u
#define GPT_NAME_UNITS 36u

enum gpt_status {
  GPT_HEALTHY, GPT_DEGRADED, GPT_AMBIGUOUS, GPT_ABSENT, GPT_INVALID,
  GPT_UNSUPPORTED, GPT_UNAVAILABLE, GPT_NO_MEMORY, GPT_IO_ERROR, GPT_TIMED_OUT,
};
enum gpt_copy_status {
  GPT_COPY_UNCHECKED, GPT_COPY_VALID, GPT_COPY_ABSENT, GPT_COPY_INVALID,
  GPT_COPY_UNSUPPORTED, GPT_COPY_IO_ERROR, GPT_COPY_TIMED_OUT,
};

/* GUID bytes retain the GPT/EFI on-disk encoding (the first three fields are
 * little-endian). Names retain all 36 UTF-16 code units, without an implied
 * terminator or namespace meaning. Neither is an authority grant. */
struct gpt_guid { uint8_t bytes[16]; };

extern const struct gpt_guid gpt_native_pool_type;

struct gpt_partition {
  struct gpt_guid type, guid;
  uint64_t first_block, block_count, attributes;
  uint32_t entry_number;
  uint16_t name[GPT_NAME_UNITS];
};

struct gpt_snapshot {
  enum gpt_status status;
  enum gpt_copy_status primary, backup;
  struct gpt_guid disk_guid;
  uint64_t disk_blocks, first_usable, last_usable;
  uint32_t block_size, partition_count;
  /* Nonzero only for a healthy/degraded map: 1 primary, 2 backup. */
  unsigned selected_copy;
  struct gpt_partition partitions[GPT_PARTITION_LIMIT];
};

/* BSP/IF=0. Prepare bounded scratch before AP startup; start once after the
 * block workers are created and task_init() has completed. VirtIO scans start
 * immediately; USB scans follow terminal boot discovery, including partial
 * inventories. No disk writes. */
void gpt_prepare(void);
void gpt_start(void);

/* BSP/IF=0. NULL while the initial scan or an exclusive raw writer's rescan
 * is pending. Views are borrowed until the next scheduling point; callers copy
 * needed fields before sleeping. Only HEALTHY/DEGRADED carry a map. */
const struct gpt_snapshot *gpt_get_snapshot(block_device_id device);

/* BSP kernel task, IF=1. Initial discovery must have completed. The caller
 * excludes every mount and raw mutation on this device until publication.
 * Replaces the snapshot after rereading both copies; performs no disk writes. */
enum gpt_status gpt_rescan(block_device_id device);

struct gpt_image;

enum gpt_append_phase {
  GPT_APPEND_PREPARE, GPT_APPEND_MATCH,
  GPT_APPEND_BACKUP_ARRAY, GPT_APPEND_BACKUP_HEADER, GPT_APPEND_BACKUP_FLUSH,
  GPT_APPEND_PRIMARY_ARRAY, GPT_APPEND_PRIMARY_HEADER, GPT_APPEND_FINAL_FLUSH,
  GPT_APPEND_VERIFY, GPT_APPEND_COMPLETE,
};

struct gpt_append_result {
  enum gpt_append_phase phase;
  bool effects_possible;
  /* UINT64_MAX for a flush or failure without one specific block. */
  uint64_t failed_block;
};

/* BSP task, IF=1, on the serial filesystem worker. Capture requires exclusion
 * of GPT/other raw mutation; a selected-partition RAW reservation may coexist
 * with disjoint filesystem mounts. Match and append require whole-device RAW
 * exclusion. The caller supplies the unchanged operation deadline.
 * Images own exact MBR/header sectors and each copy's rounded array, including
 * padding. Capture accepts bounded framing with damaged CRCs or inconsistent
 * maps; it does not grant mutation or establish healthy metadata. An absent
 * header has no array. Unbounded framing and relocated backup layouts refuse
 * capture, without changing the discovery reader's policy.
 * Allocation is bounded and occurs with IF=0. Failure leaves *out NULL. */
enum call_status gpt_image_capture(block_device_id device, uint64_t deadline,
    struct gpt_image **out);
/* BSP/IF=0, no I/O. */
void gpt_image_destroy(struct gpt_image *image);
/* Immutable borrowed views remain valid for image lifetime. */
block_device_id gpt_image_device(const struct gpt_image *image);
const struct block_info *gpt_image_geometry(const struct gpt_image *image);
const struct gpt_snapshot *gpt_image_snapshot(const struct gpt_image *image);
/* Exact geometry/metadata mismatch returns NOT_FOUND. No writes. */
enum call_status gpt_image_match(const struct gpt_image *image, uint64_t deadline);
/* BSP task, IF=1. Aligned device-relative reads of at most 4096 bytes overlay
 * retained metadata on live other sectors. The kernel output buffer may change
 * on failure; callers publish it only on success. No additional allocation. */
enum call_status gpt_image_read(const struct gpt_image *image, uint64_t offset,
    size_t length, void *bytes, uint64_t deadline);
/* Healthy GPT only. proposed selects one unused slot and a disjoint usable
 * native-pool extent, with a unique nonzero GUID and zero attributes. Only that
 * entry and required CRC fields change. All resources precede the first write.
 * Success returns a verified immutable new image; expected remains unchanged.
 * Failure fills result: effects_possible requires caller-owned quarantine even
 * when no image/handle was delivered. There is no retry or rollback. */
enum call_status gpt_image_append(const struct gpt_image *expected,
    const struct gpt_partition *proposed, uint64_t deadline,
    struct gpt_image **out, struct gpt_append_result *result);
/* BSP/IF=0, allocation-free; image must be a verified healthy map for a device
 * whose initial discovery completed. Caller still holds whole-device exclusion. */
void gpt_image_publish(const struct gpt_image *image);

#endif
