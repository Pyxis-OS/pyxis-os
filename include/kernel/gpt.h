#ifndef KERNEL_GPT_H
#define KERNEL_GPT_H

#include <stdint.h>

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

/* BSP/IF=0, outside IRQ/fault entry. NULL while scanning; otherwise immutable
 * for the boot, including failure results. Only HEALTHY/DEGRADED carry a map.
 * DEGRADED is read-only; HEALTHY is a prerequisite, not sufficient authority,
 * for future partition writes. Check current device state separately.
 * Trusted raw-block clients must preserve GPT metadata throughout the boot.
 * No rescan, repair, partition I/O wrapper or userspace ABI is provided. */
const struct gpt_snapshot *gpt_get_snapshot(void);

#endif
