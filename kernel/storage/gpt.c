#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/task.h>

#define GPT_ARRAY_LIMIT 65536u
#define GPT_ARRAY_RESERVE 16384u
#define GPT_BLOCK_LIMIT 4096u
#define GPT_HEADER_BYTES 92u
#define GPT_ENTRY_BYTES 128u
#define GPT_REVISION UINT32_C(0x00010000)
#define GPT_SCAN_TIMEOUT_MS 30000u
#define GPT_CRC_POLYNOMIAL UINT32_C(0xedb88320)
#define GPT_RESERVED_ATTRIBUTES UINT64_C(0x0000fffffffffff8)
#define MBR_SIGNATURE UINT16_C(0xaa55)
#define MBR_SIGNATURE_OFFSET 510u
#define MBR_ENTRIES_OFFSET 446u
#define MBR_ENTRY_BYTES 16u
#define MBR_ENTRY_COUNT 4u
#define MBR_PROTECTIVE_TYPE 0xeeu

enum mbr_status { MBR_VALID, MBR_ABSENT, MBR_INVALID, MBR_UNSUPPORTED };

struct gpt_header {
  struct gpt_guid disk_guid;
  uint64_t first_usable, last_usable, array_lba;
  uint32_t header_bytes, entry_count, entry_bytes, array_bytes, array_crc;
};

struct gpt_scratch {
  uint8_t block[GPT_BLOCK_LIMIT];
  uint8_t arrays[2][GPT_ARRAY_LIMIT];
  struct gpt_header headers[2];
};

struct gpt_device {
  block_device_id id;
  struct block_info device;
  struct gpt_snapshot snapshot;
  struct gpt_scratch *scratch;
  bool published;
};

static struct gpt_device *devices;
static size_t device_count;

static struct gpt_device *find_device(block_device_id id)
{
  for (size_t i = 0; i < device_count; ++i) {
    if (devices[i].id == id) {
      return &devices[i];
    }
  }
  return NULL;
}

static uint16_t read_le16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static uint32_t read_le32(const uint8_t *bytes)
{
  return (uint32_t)read_le16(bytes) | (uint32_t)read_le16(bytes + 2) << 16;
}

static uint64_t read_le64(const uint8_t *bytes)
{
  return (uint64_t)read_le32(bytes) | (uint64_t)read_le32(bytes + 4) << 32;
}

static bool zero_bytes(const uint8_t *bytes, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    if (bytes[i]) {
      return false;
    }
  }
  return true;
}

static uint32_t crc32(const uint8_t *bytes, size_t count)
{
  uint32_t crc = UINT32_MAX;
  for (size_t i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1) ? GPT_CRC_POLYNOMIAL : 0);
    }
  }
  return ~crc;
}

static enum gpt_copy_status read_blocks(struct gpt_device *scan, uint64_t first, uint32_t blocks,
    uint8_t *destination, uint64_t deadline)
{
  while (blocks) {
    if (task_deadline_expired(deadline)) {
      return GPT_COPY_TIMED_OUT;
    }
    uint32_t count = scan->device.max_transfer / scan->device.block_size;
    if (count > blocks) {
      count = blocks;
    }
    struct block_ticket ticket;
    uint64_t flags = cpu_save_interrupts();
    enum block_result result = block_submit(scan->id, BLOCK_READ, first, count, NULL, &ticket);
    cpu_restore_interrupts(flags);
    if (result == BLOCK_FULL) {
      kernel_task_sleep_until(task_deadline_after_ms(1));
      continue;
    }
    if (result != BLOCK_OK) {
      return GPT_COPY_IO_ERROR;
    }
    result = block_wait(&ticket, deadline);
    flags = cpu_save_interrupts();
    if (result != BLOCK_OK) {
      KASSERT(block_abandon(&ticket) == BLOCK_OK);
      cpu_restore_interrupts(flags);
      return result == BLOCK_TIMED_OUT ? GPT_COPY_TIMED_OUT : GPT_COPY_IO_ERROR;
    }
    struct block_completion completion;
    size_t bytes = (size_t)count * scan->device.block_size;
    result = block_collect(&ticket, destination, bytes, &completion);
    KASSERT(result == BLOCK_OK);
    cpu_restore_interrupts(flags);
    if (completion.result != BLOCK_OK || completion.bytes != bytes) {
      return GPT_COPY_IO_ERROR;
    }
    destination += bytes;
    first += count;
    blocks -= count;
  }
  return GPT_COPY_VALID;
}

static enum mbr_status validate_mbr(struct gpt_device *scan)
{
  const uint8_t *block = scan->scratch->block;
  if (read_le16(block + MBR_SIGNATURE_OFFSET) != MBR_SIGNATURE) {
    return MBR_ABSENT;
  }
  unsigned protective = 0;
  bool invalid = false;
  uint32_t expected = scan->device.block_count - 1 > UINT32_MAX ? UINT32_MAX :
      (uint32_t)(scan->device.block_count - 1);
  for (unsigned i = 0; i < MBR_ENTRY_COUNT; ++i) {
    const uint8_t *entry = block + MBR_ENTRIES_OFFSET + i * MBR_ENTRY_BYTES;
    if (entry[4] == MBR_PROTECTIVE_TYPE) {
      ++protective;
      if (read_le32(entry + 8) != 1 || read_le32(entry + 12) != expected) {
        invalid = true;
      }
    } else if (entry[4]) {
      return MBR_UNSUPPORTED;
    } else if (!zero_bytes(entry, MBR_ENTRY_BYTES)) {
      invalid = true;
    }
  }
  if (invalid || protective > 1 || !zero_bytes(block + 512, scan->device.block_size - 512)) {
    return MBR_INVALID;
  }
  return protective == 1 ? MBR_VALID : MBR_ABSENT;
}

static enum gpt_copy_status validate_header(struct gpt_device *scan, unsigned copy)
{
  uint8_t *block = scan->scratch->block;
  struct gpt_header *header = &scan->scratch->headers[copy];
  uint64_t last = scan->device.block_count - 1;
  uint64_t location = copy == 0 ? 1 : last;
  if (memcmp(block, "EFI PART", 8)) {
    return GPT_COPY_ABSENT;
  }
  uint32_t bytes = read_le32(block + 12);
  if (bytes < GPT_HEADER_BYTES || bytes > scan->device.block_size) {
    return GPT_COPY_INVALID;
  }
  uint32_t expected_crc = read_le32(block + 16);
  memset(block + 16, 0, sizeof(uint32_t));
  if (crc32(block, bytes) != expected_crc) {
    return GPT_COPY_INVALID;
  }
  if (read_le32(block + 8) != GPT_REVISION) {
    return GPT_COPY_UNSUPPORTED;
  }
  if (read_le32(block + 20) ||
      !zero_bytes(block + GPT_HEADER_BYTES, scan->device.block_size - GPT_HEADER_BYTES) ||
      read_le64(block + 24) != location || read_le64(block + 32) != (copy == 0 ? last : 1)) {
    return GPT_COPY_INVALID;
  }
  *header = (struct gpt_header){
    .header_bytes = bytes,
    .first_usable = read_le64(block + 40), .last_usable = read_le64(block + 48),
    .array_lba = read_le64(block + 72), .entry_count = read_le32(block + 80),
    .entry_bytes = read_le32(block + 84), .array_crc = read_le32(block + 88),
  };
  memcpy(header->disk_guid.bytes, block + 56, sizeof(header->disk_guid.bytes));
  if (zero_bytes(header->disk_guid.bytes, sizeof(header->disk_guid.bytes)) ||
      !header->entry_count || header->entry_bytes < GPT_ENTRY_BYTES ||
      (header->entry_bytes & (header->entry_bytes - 1))) {
    return GPT_COPY_INVALID;
  }
  uint64_t array_bytes = (uint64_t)header->entry_count * header->entry_bytes;
  uint64_t array_blocks = (array_bytes + scan->device.block_size - 1) / scan->device.block_size;
  uint64_t reserved = array_blocks;
  if (reserved < GPT_ARRAY_RESERVE / scan->device.block_size) {
    reserved = GPT_ARRAY_RESERVE / scan->device.block_size;
  }
  if (reserved >= last || header->first_usable < 2 + reserved ||
      header->first_usable > header->last_usable || header->last_usable >= last - reserved ||
      header->array_lba >= last || array_blocks > last - header->array_lba ||
      (copy == 0 ? (header->array_lba < 2 ||
                    header->array_lba + array_blocks > header->first_usable) :
                   header->array_lba <= header->last_usable)) {
    return GPT_COPY_INVALID;
  }
  if (header->entry_count > GPT_PARTITION_LIMIT || array_bytes > GPT_ARRAY_LIMIT) {
    return GPT_COPY_UNSUPPORTED;
  }
  header->array_bytes = (uint32_t)array_bytes;
  return GPT_COPY_VALID;
}

static enum gpt_copy_status validate_entries(struct gpt_device *scan, unsigned copy)
{
  const struct gpt_header *header = &scan->scratch->headers[copy];
  const uint8_t *array = scan->scratch->arrays[copy];
  if (crc32(array, header->array_bytes) != header->array_crc) {
    return GPT_COPY_INVALID;
  }
  for (uint32_t i = 0; i < header->entry_count; ++i) {
    const uint8_t *entry = array + i * header->entry_bytes;
    if (!zero_bytes(entry + GPT_ENTRY_BYTES, header->entry_bytes - GPT_ENTRY_BYTES)) {
      return GPT_COPY_INVALID;
    }
    if (zero_bytes(entry, 16)) {
      continue;
    }
    uint64_t first = read_le64(entry + 32), last = read_le64(entry + 40);
    if (zero_bytes(entry + 16, 16) || first > last || first < header->first_usable ||
        last > header->last_usable) {
      return GPT_COPY_INVALID;
    }
    if (read_le64(entry + 48) & GPT_RESERVED_ATTRIBUTES) {
      return GPT_COPY_UNSUPPORTED;
    }
    for (uint32_t j = 0; j < i; ++j) {
      const uint8_t *other = array + j * header->entry_bytes;
      if (!zero_bytes(other, 16) &&
          (!memcmp(entry + 16, other + 16, 16) ||
           (first <= read_le64(other + 40) && read_le64(other + 32) <= last))) {
        return GPT_COPY_INVALID;
      }
    }
  }
  return GPT_COPY_VALID;
}

static enum gpt_copy_status scan_copy(struct gpt_device *scan, unsigned copy, uint64_t deadline)
{
  enum gpt_copy_status status = read_blocks(scan, copy == 0 ? 1 : scan->device.block_count - 1,
      1, scan->scratch->block, deadline);
  if (status != GPT_COPY_VALID) {
    return status;
  }
  status = validate_header(scan, copy);
  if (status != GPT_COPY_VALID) {
    return status;
  }
  const struct gpt_header *header = &scan->scratch->headers[copy];
  uint32_t blocks = (header->array_bytes + scan->device.block_size - 1) / scan->device.block_size;
  status = read_blocks(scan, header->array_lba, blocks, scan->scratch->arrays[copy], deadline);
  return status == GPT_COPY_VALID ? validate_entries(scan, copy) : status;
}

static bool copies_agree(struct gpt_device *scan)
{
  const struct gpt_header *primary = &scan->scratch->headers[0], *backup = &scan->scratch->headers[1];
  return primary->header_bytes == backup->header_bytes &&
      primary->first_usable == backup->first_usable && primary->last_usable == backup->last_usable &&
      primary->entry_count == backup->entry_count && primary->entry_bytes == backup->entry_bytes &&
      !memcmp(primary->disk_guid.bytes, backup->disk_guid.bytes, sizeof(primary->disk_guid.bytes)) &&
      !memcmp(scan->scratch->arrays[0], scan->scratch->arrays[1], primary->array_bytes);
}

static void select_copy(struct gpt_device *scan, unsigned copy)
{
  const struct gpt_header *header = &scan->scratch->headers[copy];
  scan->snapshot.disk_guid = header->disk_guid;
  scan->snapshot.first_usable = header->first_usable;
  scan->snapshot.last_usable = header->last_usable;
  scan->snapshot.selected_copy = copy + 1;
  for (uint32_t i = 0; i < header->entry_count; ++i) {
    const uint8_t *entry = scan->scratch->arrays[copy] + i * header->entry_bytes;
    if (zero_bytes(entry, 16)) {
      continue;
    }
    struct gpt_partition *partition = &scan->snapshot.partitions[scan->snapshot.partition_count++];
    memcpy(partition->type.bytes, entry, sizeof(partition->type.bytes));
    memcpy(partition->guid.bytes, entry + 16, sizeof(partition->guid.bytes));
    partition->first_block = read_le64(entry + 32);
    partition->block_count = read_le64(entry + 40) - partition->first_block + 1;
    partition->attributes = read_le64(entry + 48);
    partition->entry_number = i + 1;
    for (unsigned unit = 0; unit < GPT_NAME_UNITS; ++unit) {
      partition->name[unit] = read_le16(entry + 56 + unit * sizeof(uint16_t));
    }
  }
}

static enum gpt_status choose_map(struct gpt_device *scan, enum mbr_status mbr)
{
  enum gpt_copy_status primary = scan->snapshot.primary, backup = scan->snapshot.backup;
  if (primary == GPT_COPY_TIMED_OUT || backup == GPT_COPY_TIMED_OUT) {
    return GPT_TIMED_OUT;
  }
  if (primary == GPT_COPY_IO_ERROR || backup == GPT_COPY_IO_ERROR) {
    return GPT_IO_ERROR;
  }
  if (mbr == MBR_UNSUPPORTED || primary == GPT_COPY_UNSUPPORTED || backup == GPT_COPY_UNSUPPORTED) {
    return GPT_UNSUPPORTED;
  }
  if (mbr == MBR_ABSENT && primary == GPT_COPY_ABSENT && backup == GPT_COPY_ABSENT) {
    return GPT_ABSENT;
  }
  if (mbr != MBR_VALID) {
    return GPT_INVALID;
  }
  if (primary == GPT_COPY_VALID && backup == GPT_COPY_VALID) {
    if (!copies_agree(scan)) {
      return GPT_AMBIGUOUS;
    }
    select_copy(scan, 0);
    return GPT_HEALTHY;
  }
  if (primary == GPT_COPY_VALID || backup == GPT_COPY_VALID) {
    select_copy(scan, primary == GPT_COPY_VALID ? 0 : 1);
    return GPT_DEGRADED;
  }
  return GPT_INVALID;
}

static void publish(struct gpt_device *scan, enum gpt_status status)
{
  static const char *const names[] = {
    [GPT_HEALTHY] = "healthy", [GPT_DEGRADED] = "degraded (read-only)",
    [GPT_AMBIGUOUS] = "ambiguous", [GPT_ABSENT] = "absent", [GPT_INVALID] = "invalid",
    [GPT_UNSUPPORTED] = "unsupported", [GPT_UNAVAILABLE] = "unavailable",
    [GPT_NO_MEMORY] = "no memory", [GPT_IO_ERROR] = "I/O error", [GPT_TIMED_OUT] = "timed out",
  };
  uint64_t flags = cpu_save_interrupts();
  scan->snapshot.status = status;
  kfree(scan->scratch);
  scan->scratch = NULL;
  scan->published = true;
  cpu_restore_interrupts(flags);
  klog("GPT: device %u %s; primary=%u backup=%u partitions=%u\n", (unsigned)scan->id, names[status],
       (unsigned)scan->snapshot.primary, (unsigned)scan->snapshot.backup, scan->snapshot.partition_count);
  for (uint32_t i = 0; i < scan->snapshot.partition_count; ++i) {
    const struct gpt_partition *partition = &scan->snapshot.partitions[i];
    klog("GPT: entry %u first=%lu blocks=%lu\n", partition->entry_number,
         partition->first_block, partition->block_count);
  }
}

static void scan_disk(void *argument)
{
  struct gpt_device *scan = argument;
  if (scan->device.block_count < 3) {
    publish(scan, GPT_INVALID);
    return;
  }
  uint64_t deadline = task_deadline_after_ms(GPT_SCAN_TIMEOUT_MS);
  enum gpt_copy_status status = read_blocks(scan, 0, 1, scan->scratch->block, deadline);
  if (status != GPT_COPY_VALID) {
    publish(scan, status == GPT_COPY_TIMED_OUT ? GPT_TIMED_OUT : GPT_IO_ERROR);
    return;
  }
  enum mbr_status mbr = validate_mbr(scan);
  scan->snapshot.primary = scan_copy(scan, 0, deadline);
  scan->snapshot.backup = scan_copy(scan, 1, deadline);
  publish(scan, choose_map(scan, mbr));
}

void gpt_prepare(void)
{
  device_count = block_device_count();
  if (!device_count) {
    return;
  }
  devices = kmalloc(device_count * sizeof(*devices));
  if (!devices) {
    device_count = 0;
    return;
  }
  memset(devices, 0, device_count * sizeof(*devices));
  for (size_t i = 0; i < device_count; ++i) {
    devices[i].id = block_device_at(i);
    devices[i].scratch = kmalloc(sizeof(*devices[i].scratch));
  }
}

static bool prepare_scan(struct gpt_device *scan)
{
  scan->snapshot = (struct gpt_snapshot){0};
  scan->published = false;
  if (block_get_info(scan->id, &scan->device) != BLOCK_OK) {
    publish(scan, GPT_UNAVAILABLE);
    return false;
  }
  scan->snapshot.disk_blocks = scan->device.block_count;
  scan->snapshot.block_size = scan->device.block_size;
  if (!scan->scratch) {
    publish(scan, GPT_NO_MEMORY);
    return false;
  }
  if ((scan->device.block_size != 512 && scan->device.block_size != GPT_BLOCK_LIMIT) ||
      scan->device.max_transfer < scan->device.block_size) {
    publish(scan, GPT_UNSUPPORTED);
    return false;
  }
  return true;
}

void gpt_start(void)
{
  for (size_t i = 0; i < device_count; ++i) {
    struct gpt_device *scan = &devices[i];
    if (prepare_scan(scan) && kernel_task_create(scan_disk, scan) != MM_OK) {
      publish(scan, GPT_NO_MEMORY);
    }
  }
}

const struct gpt_snapshot *gpt_get_snapshot(block_device_id id)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct gpt_device *scan = find_device(id);
  static const struct gpt_snapshot no_memory = {.status = GPT_NO_MEMORY};
  if (!devices && block_device_count() && block_preparation_result(id) != BLOCK_DEVICE_INVALID) {
    return &no_memory;
  }
  return scan && scan->published ? &scan->snapshot : NULL;
}

enum gpt_status gpt_rescan(block_device_id id)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  struct gpt_device *scan = find_device(id);
  if (!scan || !scan->published) {
    cpu_restore_interrupts(flags);
    return devices ? GPT_UNAVAILABLE : GPT_NO_MEMORY;
  }
  KASSERT(!scan->scratch);
  scan->scratch = kmalloc(sizeof(*scan->scratch));
  bool ready = prepare_scan(scan);
  cpu_restore_interrupts(flags);
  if (ready) {
    scan_disk(scan);
  }
  return scan->snapshot.status;
}
