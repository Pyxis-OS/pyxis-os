#ifndef KERNEL_GPT_H
#define KERNEL_GPT_H

#include <stdint.h>
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
 * block worker is created and task_init() has completed. No disk writes. */
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

#endif
