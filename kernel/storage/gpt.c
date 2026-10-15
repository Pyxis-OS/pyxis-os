#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "block_registry.h"

#define GPT_ARRAY_LIMIT 65536u
#define GPT_ARRAY_RESERVE 16384u
#define GPT_BLOCK_LIMIT 4096u
#define GPT_HEADER_BYTES 92u
#define GPT_ENTRY_BYTES 128u
#define GPT_REVISION UINT32_C(0x00010000)
#define GPT_SCAN_TIMEOUT_MS 30000u
#define GPT_DISCOVERY_POLL_MS 10u
#define GPT_CRC_POLYNOMIAL UINT32_C(0xedb88320)
#define GPT_RESERVED_ATTRIBUTES UINT64_C(0x0000fffffffffff8)
#define MBR_SIGNATURE UINT16_C(0xaa55)
#define MBR_SIGNATURE_OFFSET 510u
#define MBR_ENTRIES_OFFSET 446u
#define MBR_ENTRY_BYTES 16u
#define MBR_ENTRY_COUNT 4u
#define MBR_PROTECTIVE_TYPE 0xeeu

enum mbr_status { MBR_VALID, MBR_SHORT, MBR_ABSENT, MBR_INVALID, MBR_UNSUPPORTED };

struct gpt_header {
  struct gpt_guid disk_guid;
  uint64_t current_lba, alternate_lba, first_usable, last_usable, array_lba;
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
  uint32_t protective_blocks;
  bool published, shared_scratch, relocatable;
};

struct gpt_image {
  struct gpt_device scan;
  struct gpt_scratch scratch;
  uint8_t mbr[GPT_BLOCK_LIMIT];
  uint8_t headers[2][GPT_BLOCK_LIMIT];
  uint64_t array_lba[2];
  uint32_t array_blocks[2];
};

const struct gpt_guid gpt_native_pool_type = {.bytes = {
  0xa3, 0x94, 0x81, 0x1a, 0x07, 0x8a, 0xff, 0x4d,
  0x83, 0x0e, 0x4c, 0xb4, 0xed, 0x7a, 0xac, 0x00,
}};

static struct gpt_device *devices;
static size_t device_count;
static size_t initial_count;
static struct gpt_scratch *usb_scratch;
static bool usb_scan_failed;

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

static enum gpt_copy_status read_device_blocks(block_device_id device,
    const struct block_info *info, uint64_t first, uint32_t blocks,
    uint8_t *destination, uint64_t deadline)
{
  while (blocks) {
    if (task_deadline_expired(deadline)) {
      return GPT_COPY_TIMED_OUT;
    }
    uint32_t count = info->max_transfer / info->block_size;
    if (count > blocks) {
      count = blocks;
    }
    struct block_ticket ticket;
    uint64_t flags = cpu_save_interrupts();
    enum block_result result = block_submit(device, BLOCK_READ, first, count, NULL, &ticket);
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
    size_t bytes = (size_t)count * info->block_size;
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

static enum gpt_copy_status read_blocks(struct gpt_device *scan, uint64_t first, uint32_t blocks,
    uint8_t *destination, uint64_t deadline)
{
  return read_device_blocks(scan->id, &scan->device, first, blocks, destination, deadline);
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
      scan->protective_blocks = read_le32(entry + 12);
      if (read_le32(entry + 8) != 1 || !scan->protective_blocks ||
          scan->protective_blocks > expected) {
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
  if (!protective) {
    return MBR_ABSENT;
  }
  return scan->protective_blocks == expected ? MBR_VALID : MBR_SHORT;
}

static enum gpt_copy_status validate_header(struct gpt_device *scan, unsigned copy, uint64_t location)
{
  uint8_t *block = scan->scratch->block;
  struct gpt_header *header = &scan->scratch->headers[copy];
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
  uint64_t alternate = read_le64(block + 32);
  uint64_t last = copy == 0 ? alternate : location;
  if (last <= 1 || last >= scan->device.block_count || read_le32(block + 20) ||
      !zero_bytes(block + GPT_HEADER_BYTES, scan->device.block_size - GPT_HEADER_BYTES) ||
      read_le64(block + 24) != location || (copy == 1 && alternate != 1)) {
    return GPT_COPY_INVALID;
  }
  *header = (struct gpt_header){
    .current_lba = location, .alternate_lba = alternate,
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

static enum gpt_copy_status scan_array(struct gpt_device *scan, unsigned copy, uint64_t deadline)
{
  const struct gpt_header *header = &scan->scratch->headers[copy];
  uint32_t blocks = (header->array_bytes + scan->device.block_size - 1) / scan->device.block_size;
  enum gpt_copy_status status = read_blocks(scan, header->array_lba, blocks,
      scan->scratch->arrays[copy], deadline);
  return status == GPT_COPY_VALID ? validate_entries(scan, copy) : status;
}

static enum gpt_copy_status scan_copy(struct gpt_device *scan, unsigned copy, uint64_t location,
    uint64_t deadline)
{
  enum gpt_copy_status status = read_blocks(scan, location, 1, scan->scratch->block, deadline);
  if (status != GPT_COPY_VALID) {
    return status;
  }
  status = validate_header(scan, copy, location);
  return status == GPT_COPY_VALID ? scan_array(scan, copy, deadline) : status;
}

static bool copies_agree(struct gpt_device *scan)
{
  const struct gpt_header *primary = &scan->scratch->headers[0], *backup = &scan->scratch->headers[1];
  return primary->alternate_lba == backup->current_lba &&
      backup->alternate_lba == primary->current_lba &&
      primary->header_bytes == backup->header_bytes &&
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

static bool protective_extent_covers_map(struct gpt_device *scan)
{
  const struct gpt_header *primary = &scan->scratch->headers[0];
  uint32_t array_blocks = (primary->array_bytes + scan->device.block_size - 1) /
      scan->device.block_size;
  if (primary->array_lba + array_blocks - 1 > scan->protective_blocks) {
    return false;
  }
  for (uint32_t i = 0; i < primary->entry_count; ++i) {
    const uint8_t *entry = scan->scratch->arrays[0] + i * primary->entry_bytes;
    if (!zero_bytes(entry, 16) && read_le64(entry + 40) > scan->protective_blocks) {
      return false;
    }
  }
  return true;
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
  if (mbr != MBR_VALID && mbr != MBR_SHORT) {
    return GPT_INVALID;
  }
  if (primary == GPT_COPY_VALID && backup == GPT_COPY_VALID) {
    if (!copies_agree(scan)) {
      return GPT_AMBIGUOUS;
    }
    if (mbr == MBR_SHORT && !protective_extent_covers_map(scan)) {
      return GPT_INVALID;
    }
    select_copy(scan, 0);
    scan->relocatable = mbr == MBR_SHORT ||
        scan->scratch->headers[0].alternate_lba < scan->device.block_count - 1;
    return scan->relocatable ? GPT_DEGRADED : GPT_HEALTHY;
  }
  /* The larger-device exception requires two validated, agreeing copies. */
  if (mbr == MBR_SHORT || (primary == GPT_COPY_VALID &&
      scan->scratch->headers[0].alternate_lba < scan->device.block_count - 1)) {
    return GPT_INVALID;
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
  if (!scan->shared_scratch) {
    kfree(scan->scratch);
  }
  scan->scratch = NULL;
  scan->published = true;
  cpu_restore_interrupts(flags);
  if (scan->relocatable) {
    kwarn("GPT: device %u warning: metadata describes a smaller disk; degraded read-only map; repair with sgdisk --move-second-header under explicit write authority\n",
        (unsigned)scan->id);
  } else {
    enum log_level level = status == GPT_HEALTHY || status == GPT_ABSENT ?
        LOG_LEVEL_INFO : LOG_LEVEL_WARN;
    klog(level, "GPT: device %u %s; primary=%u backup=%u partitions=%u\n", (unsigned)scan->id,
         names[status], (unsigned)scan->snapshot.primary, (unsigned)scan->snapshot.backup,
         scan->snapshot.partition_count);
  }
  for (uint32_t i = 0; i < scan->snapshot.partition_count; ++i) {
    const struct gpt_partition *partition = &scan->snapshot.partitions[i];
    kinfo("GPT: entry %u first=%lu blocks=%lu\n", partition->entry_number,
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
  scan->snapshot.primary = scan_copy(scan, 0, 1, deadline);
  uint64_t device_end = scan->device.block_count - 1;
  uint64_t alternate = scan->snapshot.primary == GPT_COPY_VALID ?
      scan->scratch->headers[0].alternate_lba : device_end;
  scan->snapshot.backup = scan_copy(scan, 1, alternate, deadline);
  if (alternate < device_end) {
    status = read_blocks(scan, device_end, 1, scan->scratch->block, deadline);
    if (status != GPT_COPY_VALID) {
      publish(scan, status == GPT_COPY_TIMED_OUT ? GPT_TIMED_OUT : GPT_IO_ERROR);
      return;
    }
    if (!memcmp(scan->scratch->block, "EFI PART", 8)) {
      /* An additional header is not evidence that either older layout wins.
       * This branch never selects a map, so slot 1 can validate the extra copy. */
      status = validate_header(scan, 1, device_end);
      if (status == GPT_COPY_VALID) {
        status = scan_array(scan, 1, deadline);
      }
      kdebug("GPT: device %u additional header at physical end; refusing copied-image recovery\n",
          (unsigned)scan->id);
      enum gpt_status result = GPT_INVALID;
      if (status == GPT_COPY_VALID) {
        result = GPT_AMBIGUOUS;
      } else if (status == GPT_COPY_UNSUPPORTED) {
        result = GPT_UNSUPPORTED;
      } else if (status == GPT_COPY_IO_ERROR) {
        result = GPT_IO_ERROR;
      } else if (status == GPT_COPY_TIMED_OUT) {
        result = GPT_TIMED_OUT;
      }
      publish(scan, result);
      return;
    }
  }
  publish(scan, choose_map(scan, mbr));
}

void gpt_prepare(void)
{
  initial_count = block_device_count();
  device_count = block_registry_capacity();
  if (!device_count) {
    return;
  }
  if (device_count <= SIZE_MAX / sizeof(*devices)) {
    devices = kmalloc(device_count * sizeof(*devices));
  }
  if (!devices) {
    device_count = 0;
    return;
  }
  memset(devices, 0, device_count * sizeof(*devices));
  for (size_t i = 0; i < device_count; ++i) {
    devices[i].id = (block_device_id)i + 1;
    if (i < initial_count) {
      devices[i].scratch = kmalloc(sizeof(*devices[i].scratch));
    } else {
      devices[i].shared_scratch = true;
    }
  }
  if (device_count > initial_count) {
    usb_scratch = kmalloc(sizeof(*usb_scratch));
  }
}

static bool prepare_scan(struct gpt_device *scan)
{
  scan->snapshot = (struct gpt_snapshot){0};
  scan->protective_blocks = 0;
  scan->relocatable = false;
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

static void scan_usb_disks(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    bool finished = block_discovery_finished();
    cpu_restore_interrupts(flags);
    if (finished) {
      break;
    }
    kernel_task_sleep_until(task_deadline_after_ms(GPT_DISCOVERY_POLL_MS));
  }
  uint64_t flags = cpu_save_interrupts();
  size_t count = block_device_count();
  KASSERT(count <= device_count);
  cpu_restore_interrupts(flags);
  for (size_t i = initial_count; i < count; ++i) {
    struct gpt_device *scan = &devices[i];
    flags = cpu_save_interrupts();
    scan->scratch = usb_scratch;
    bool ready = prepare_scan(scan);
    cpu_restore_interrupts(flags);
    if (ready) {
      scan_disk(scan);
    }
  }
  flags = cpu_save_interrupts();
  kfree(usb_scratch);
  usb_scratch = NULL;
  cpu_restore_interrupts(flags);
}

void gpt_start(void)
{
  for (size_t i = 0; devices && i < initial_count; ++i) {
    struct gpt_device *scan = &devices[i];
    if (prepare_scan(scan) && kernel_task_create(scan_disk, scan) != MM_OK) {
      publish(scan, GPT_NO_MEMORY);
    }
  }
  if (devices && device_count > initial_count &&
      kernel_task_create(scan_usb_disks, NULL) != MM_OK) {
    usb_scan_failed = true;
    kfree(usb_scratch);
    usb_scratch = NULL;
  }
}

const struct gpt_snapshot *gpt_get_snapshot(block_device_id id)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct gpt_device *scan = find_device(id);
  static const struct gpt_snapshot no_memory = {.status = GPT_NO_MEMORY};
  if (block_preparation_result(id) == BLOCK_DEVICE_INVALID) {
    return NULL;
  }
  if (!devices || (id > initial_count && usb_scan_failed)) {
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
  scan->shared_scratch = false;
  bool ready = prepare_scan(scan);
  cpu_restore_interrupts(flags);
  if (ready) {
    scan_disk(scan);
  }
  return scan->snapshot.status;
}

static enum call_status image_read_status(enum gpt_copy_status status)
{
  return status == GPT_COPY_VALID ? CALL_OK :
      status == GPT_COPY_TIMED_OUT ? CALL_TIMED_OUT : CALL_IO;
}

static enum call_status image_block_status(enum block_result result)
{
  switch (result) {
  case BLOCK_OK: return CALL_OK;
  case BLOCK_FULL:
  case BLOCK_BUSY: return CALL_BUSY;
  case BLOCK_INVALID: return CALL_BAD_REQUEST;
  case BLOCK_READ_ONLY: return CALL_READ_ONLY;
  case BLOCK_TIMED_OUT: return CALL_TIMED_OUT;
  case BLOCK_UNSUPPORTED:
  case BLOCK_UNAVAILABLE: return CALL_UNAVAILABLE;
  default: return CALL_IO;
  }
}

static void image_require_task(void)
{
  KASSERT(cpu_current() == cpu_bsp());
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

static enum call_status image_geometry(const struct gpt_image *image, struct block_info *info)
{
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(image->scan.id, info);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    return image_block_status(result);
  }
  if (info->block_count != image->scan.device.block_count ||
      info->block_size != image->scan.device.block_size) {
    return CALL_NOT_FOUND;
  }
  return info->max_transfer < info->block_size ? CALL_UNAVAILABLE : CALL_OK;
}

static enum call_status image_frame_array(struct gpt_image *image, unsigned copy)
{
  const uint8_t *header = image->headers[copy];
  if (memcmp(header, "EFI PART", 8)) {
    return CALL_OK;
  }
  uint32_t count = read_le32(header + 80), entry_bytes = read_le32(header + 84);
  uint64_t array_bytes = (uint64_t)count * entry_bytes;
  if (!count || count > GPT_PARTITION_LIMIT || entry_bytes < GPT_ENTRY_BYTES ||
      (entry_bytes & (entry_bytes - 1)) || array_bytes > GPT_ARRAY_LIMIT) {
    return CALL_UNAVAILABLE;
  }
  uint64_t first = read_le64(header + 72);
  uint32_t blocks = (array_bytes + image->scan.device.block_size - 1) /
      image->scan.device.block_size;
  if (first >= image->scan.device.block_count ||
      blocks > image->scan.device.block_count - first) {
    return CALL_UNAVAILABLE;
  }
  image->array_lba[copy] = first;
  image->array_blocks[copy] = blocks;
  return CALL_OK;
}

static void image_validate(struct gpt_image *image)
{
  struct gpt_device *scan = &image->scan;
  scan->snapshot = (struct gpt_snapshot){
    .disk_blocks = scan->device.block_count,
    .block_size = scan->device.block_size,
  };
  scan->protective_blocks = 0;
  scan->relocatable = false;
  memcpy(image->scratch.block, image->mbr, scan->device.block_size);
  enum mbr_status mbr = validate_mbr(scan);
  for (unsigned copy = 0; copy < 2; ++copy) {
    memcpy(image->scratch.block, image->headers[copy], scan->device.block_size);
    enum gpt_copy_status status = validate_header(scan, copy,
        copy ? scan->device.block_count - 1 : 1);
    if (status == GPT_COPY_VALID) {
      status = validate_entries(scan, copy);
    }
    if (copy) {
      scan->snapshot.backup = status;
    } else {
      scan->snapshot.primary = status;
    }
  }
  scan->snapshot.status = choose_map(scan, mbr);
}

enum call_status gpt_image_capture(block_device_id device, uint64_t deadline,
    struct gpt_image **out)
{
  image_require_task();
  *out = NULL;
  struct block_info info;
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(device, &info);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    return image_block_status(result);
  }
  if (info.block_count < 3 || (info.block_size != 512 && info.block_size != GPT_BLOCK_LIMIT) ||
      info.block_count > UINT64_MAX / info.block_size || info.max_transfer < info.block_size) {
    return CALL_UNAVAILABLE;
  }
  flags = cpu_save_interrupts();
  struct gpt_image *image = kmalloc(sizeof(*image));
  cpu_restore_interrupts(flags);
  if (!image) {
    return CALL_NO_MEMORY;
  }
  memset(image, 0, sizeof(*image));
  image->scan.id = device;
  image->scan.device = info;
  image->scan.scratch = &image->scratch;
  image->scan.shared_scratch = true;
  enum call_status status = image_read_status(read_blocks(&image->scan, 0, 1,
      image->mbr, deadline));
  for (unsigned copy = 0; status == CALL_OK && copy < 2; ++copy) {
    status = image_read_status(read_blocks(&image->scan,
        copy ? info.block_count - 1 : 1, 1, image->headers[copy], deadline));
  }
  if (status == CALL_OK && !memcmp(image->headers[0], "EFI PART", 8)) {
    uint64_t alternate = read_le64(image->headers[0] + 32);
    /* A relocated map also needs its old backup and a physical-end guard.
     * This bounded image profile captures canonical two-copy layouts only. */
    if (alternate > 1 && alternate < info.block_count - 1) {
      status = CALL_UNAVAILABLE;
    }
  }
  for (unsigned copy = 0; status == CALL_OK && copy < 2; ++copy) {
    status = image_frame_array(image, copy);
    if (status == CALL_OK && image->array_blocks[copy]) {
      status = image_read_status(read_blocks(&image->scan, image->array_lba[copy],
          image->array_blocks[copy], image->scratch.arrays[copy], deadline));
    }
  }
  if (status == CALL_OK) {
    image_validate(image);
    if (task_deadline_expired(deadline)) {
      status = CALL_TIMED_OUT;
    }
  }
  if (status != CALL_OK) {
    flags = cpu_save_interrupts();
    gpt_image_destroy(image);
    cpu_restore_interrupts(flags);
    return status;
  }
  *out = image;
  return CALL_OK;
}

void gpt_image_destroy(struct gpt_image *image)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  kfree(image);
}

block_device_id gpt_image_device(const struct gpt_image *image)
{
  return image->scan.id;
}

const struct block_info *gpt_image_geometry(const struct gpt_image *image)
{
  return &image->scan.device;
}

const struct gpt_snapshot *gpt_image_snapshot(const struct gpt_image *image)
{
  return &image->scan.snapshot;
}

static enum call_status image_match_range(const struct gpt_image *image,
    uint64_t first, uint32_t blocks, const uint8_t *expected, uint64_t deadline)
{
  uint8_t bytes[GPT_BLOCK_LIMIT];
  while (blocks) {
    uint32_t count = sizeof(bytes) / image->scan.device.block_size;
    if (count > blocks) {
      count = blocks;
    }
    enum call_status status = image_read_status(read_device_blocks(image->scan.id,
        &image->scan.device, first, count, bytes, deadline));
    if (status != CALL_OK) {
      return status;
    }
    size_t length = (size_t)count * image->scan.device.block_size;
    if (memcmp(bytes, expected, length)) {
      return CALL_NOT_FOUND;
    }
    expected += length;
    first += count;
    blocks -= count;
  }
  return CALL_OK;
}

enum call_status gpt_image_match(const struct gpt_image *image, uint64_t deadline)
{
  image_require_task();
  struct block_info info;
  enum call_status status = image_geometry(image, &info);
  if (status == CALL_OK) {
    status = image_match_range(image, 0, 1, image->mbr, deadline);
  }
  for (unsigned copy = 0; status == CALL_OK && copy < 2; ++copy) {
    status = image_match_range(image, copy ? info.block_count - 1 : 1,
        1, image->headers[copy], deadline);
    if (status == CALL_OK && image->array_blocks[copy]) {
      status = image_match_range(image, image->array_lba[copy], image->array_blocks[copy],
          image->scratch.arrays[copy], deadline);
    }
  }
  return status == CALL_OK && task_deadline_expired(deadline) ? CALL_TIMED_OUT : status;
}

static const uint8_t *image_sector(const struct gpt_image *image, uint64_t block)
{
  if (!block) {
    return image->mbr;
  }
  if (block == 1) {
    return image->headers[0];
  }
  if (block == image->scan.device.block_count - 1) {
    return image->headers[1];
  }
  for (unsigned copy = 0; copy < 2; ++copy) {
    if (block >= image->array_lba[copy] &&
        block - image->array_lba[copy] < image->array_blocks[copy]) {
      return image->scratch.arrays[copy] +
          (size_t)(block - image->array_lba[copy]) * image->scan.device.block_size;
    }
  }
  return NULL;
}

enum call_status gpt_image_read(const struct gpt_image *image, uint64_t offset,
    size_t length, void *bytes, uint64_t deadline)
{
  image_require_task();
  struct block_info info;
  enum call_status status = image_geometry(image, &info);
  if (status != CALL_OK) {
    return status;
  }
  if (!length || length > GPT_BLOCK_LIMIT || offset % info.block_size ||
      length % info.block_size) {
    return CALL_BAD_REQUEST;
  }
  uint64_t first = offset / info.block_size;
  uint32_t blocks = length / info.block_size;
  if (first >= info.block_count || blocks > info.block_count - first) {
    return CALL_BAD_REQUEST;
  }
  uint8_t *destination = bytes;
  while (blocks) {
    if (task_deadline_expired(deadline)) {
      return CALL_TIMED_OUT;
    }
    const uint8_t *retained = image_sector(image, first);
    uint32_t count = 1;
    if (retained) {
      memcpy(destination, retained, info.block_size);
    } else {
      while (count < blocks && !image_sector(image, first + count)) {
        ++count;
      }
      status = image_read_status(read_device_blocks(image->scan.id, &info,
          first, count, destination, deadline));
      if (status != CALL_OK) {
        return status;
      }
    }
    destination += (size_t)count * info.block_size;
    first += count;
    blocks -= count;
  }
  return task_deadline_expired(deadline) ? CALL_TIMED_OUT : CALL_OK;
}

static void write_le16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void write_le32(uint8_t *bytes, uint32_t value)
{
  write_le16(bytes, (uint16_t)value);
  write_le16(bytes + 2, (uint16_t)(value >> 16));
}

static void write_le64(uint8_t *bytes, uint64_t value)
{
  write_le32(bytes, (uint32_t)value);
  write_le32(bytes + 4, (uint32_t)(value >> 32));
}

static enum call_status image_prepare_append(struct gpt_image *image,
    const struct gpt_partition *proposed)
{
  const struct gpt_snapshot *snapshot = &image->scan.snapshot;
  if (snapshot->status != GPT_HEALTHY) {
    return CALL_UNAVAILABLE;
  }
  const struct gpt_header *primary = &image->scratch.headers[0];
  if (!proposed->entry_number || proposed->entry_number > primary->entry_count ||
      memcmp(proposed->type.bytes, gpt_native_pool_type.bytes, 16) || proposed->attributes ||
      zero_bytes(proposed->guid.bytes, 16) || !proposed->block_count ||
      proposed->first_block < snapshot->first_usable ||
      proposed->first_block > snapshot->last_usable ||
      proposed->block_count > snapshot->last_usable - proposed->first_block + 1) {
    return CALL_BAD_REQUEST;
  }
  uint64_t last = proposed->first_block + proposed->block_count - 1;
  for (uint32_t i = 0; i < snapshot->partition_count; ++i) {
    const struct gpt_partition *existing = &snapshot->partitions[i];
    if (!memcmp(proposed->guid.bytes, existing->guid.bytes, 16) ||
        (proposed->first_block < existing->first_block + existing->block_count &&
         existing->first_block <= last)) {
      return CALL_BAD_REQUEST;
    }
  }
  for (unsigned copy = 0; copy < 2; ++copy) {
    const struct gpt_header *header = &image->scratch.headers[copy];
    uint8_t *entry = image->scratch.arrays[copy] +
        (size_t)(proposed->entry_number - 1) * header->entry_bytes;
    if (!zero_bytes(entry, 16)) {
      return CALL_ALREADY_EXISTS;
    }
    memset(entry, 0, header->entry_bytes);
    memcpy(entry, gpt_native_pool_type.bytes, 16);
    memcpy(entry + 16, proposed->guid.bytes, 16);
    write_le64(entry + 32, proposed->first_block);
    write_le64(entry + 40, last);
    for (unsigned unit = 0; unit < GPT_NAME_UNITS; ++unit) {
      write_le16(entry + 56 + unit * sizeof(uint16_t), proposed->name[unit]);
    }
    uint8_t *bytes = image->headers[copy];
    write_le32(bytes + 88, crc32(image->scratch.arrays[copy], header->array_bytes));
    write_le32(bytes + 16, 0);
    write_le32(bytes + 16, crc32(bytes, header->header_bytes));
  }
  image_validate(image);
  return image->scan.snapshot.status == GPT_HEALTHY ? CALL_OK : CALL_BAD_REQUEST;
}

static enum call_status image_submit_mutation(const struct gpt_image *image,
    enum block_operation operation, uint64_t first, uint32_t count,
    const uint8_t *bytes, uint64_t deadline, struct gpt_append_result *progress)
{
  struct block_ticket ticket;
  for (;;) {
    if (task_deadline_expired(deadline)) {
      return CALL_TIMED_OUT;
    }
    uint64_t flags = cpu_save_interrupts();
    enum block_result result = block_submit(image->scan.id, operation, first,
        count, bytes, &ticket);
    cpu_restore_interrupts(flags);
    if (result == BLOCK_FULL) {
      kernel_task_sleep_until(task_deadline_after_ms(1));
      continue;
    }
    if (result != BLOCK_OK) {
      return image_block_status(result);
    }
    break;
  }
  enum block_result result = block_wait(&ticket, deadline);
  uint64_t flags = cpu_save_interrupts();
  if (result != BLOCK_OK) {
    KASSERT(block_abandon(&ticket) == BLOCK_OK);
    cpu_restore_interrupts(flags);
    progress->effects_possible = true;
    return image_block_status(result);
  }
  struct block_completion completion;
  result = block_collect(&ticket, NULL, 0, &completion);
  KASSERT(result == BLOCK_OK);
  cpu_restore_interrupts(flags);
  progress->effects_possible |= completion.submitted;
  if (completion.result != BLOCK_OK) {
    return image_block_status(completion.result);
  }
  size_t length = (size_t)count * image->scan.device.block_size;
  return completion.bytes == length ? CALL_OK : CALL_IO;
}

static enum call_status image_write_blocks(const struct gpt_image *image,
    uint64_t first, uint32_t blocks, const uint8_t *bytes, uint64_t deadline,
    struct gpt_append_result *progress)
{
  while (blocks) {
    uint32_t count = image->scan.device.max_transfer / image->scan.device.block_size;
    if (count > blocks) {
      count = blocks;
    }
    progress->failed_block = first;
    enum call_status status = image_submit_mutation(image, BLOCK_WRITE, first, count,
        bytes, deadline, progress);
    if (status != CALL_OK) {
      return status;
    }
    bytes += (size_t)count * image->scan.device.block_size;
    first += count;
    blocks -= count;
  }
  return CALL_OK;
}

enum call_status gpt_image_append(const struct gpt_image *expected,
    const struct gpt_partition *proposed, uint64_t deadline,
    struct gpt_image **out, struct gpt_append_result *progress)
{
  image_require_task();
  *out = NULL;
  *progress = (struct gpt_append_result){
    .phase = GPT_APPEND_PREPARE, .failed_block = UINT64_MAX,
  };
  struct block_info info;
  enum call_status status = image_geometry(expected, &info);
  if (status != CALL_OK) {
    return status;
  }
  if (info.write_failed) {
    return CALL_IO;
  }
  if (!info.writable || !info.flush_supported) {
    return CALL_READ_ONLY;
  }
  uint64_t flags = cpu_save_interrupts();
  struct gpt_image *image = kmalloc(sizeof(*image));
  cpu_restore_interrupts(flags);
  if (!image) {
    return CALL_NO_MEMORY;
  }
  *image = *expected;
  image->scan.scratch = &image->scratch;
  image->scan.device = info;
  status = image_prepare_append(image, proposed);
  if (status == CALL_OK) {
    progress->phase = GPT_APPEND_MATCH;
    status = gpt_image_match(expected, deadline);
  }
  for (unsigned step = 0; status == CALL_OK && step < 2; ++step) {
    unsigned copy = 1 - step;
    const struct gpt_header *header = &image->scratch.headers[copy];
    uint64_t offset = (uint64_t)(proposed->entry_number - 1) * header->entry_bytes;
    uint32_t first = offset / info.block_size;
    uint32_t last = (offset + header->entry_bytes - 1) / info.block_size;
    progress->phase = copy ? GPT_APPEND_BACKUP_ARRAY : GPT_APPEND_PRIMARY_ARRAY;
    status = image_write_blocks(image, image->array_lba[copy] + first,
        last - first + 1, image->scratch.arrays[copy] + (size_t)first * info.block_size,
        deadline, progress);
    if (status == CALL_OK) {
      progress->phase = copy ? GPT_APPEND_BACKUP_HEADER : GPT_APPEND_PRIMARY_HEADER;
      status = image_write_blocks(image, copy ? info.block_count - 1 : 1,
          1, image->headers[copy], deadline, progress);
    }
    if (status == CALL_OK) {
      progress->phase = copy ? GPT_APPEND_BACKUP_FLUSH : GPT_APPEND_FINAL_FLUSH;
      progress->failed_block = UINT64_MAX;
      status = image_submit_mutation(image, BLOCK_FLUSH, 0, 0, NULL, deadline, progress);
    }
  }
  if (status == CALL_OK) {
    progress->phase = GPT_APPEND_VERIFY;
    progress->failed_block = UINT64_MAX;
    status = gpt_image_match(image, deadline);
    if (status == CALL_NOT_FOUND) {
      status = CALL_IO;
    }
  }
  if (status != CALL_OK) {
    flags = cpu_save_interrupts();
    gpt_image_destroy(image);
    cpu_restore_interrupts(flags);
    return status;
  }
  progress->phase = GPT_APPEND_COMPLETE;
  *out = image;
  return CALL_OK;
}

void gpt_image_publish(const struct gpt_image *image)
{
  KASSERT(cpu_current() == cpu_bsp());
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(image->scan.snapshot.status == GPT_HEALTHY);
  struct gpt_device *scan = find_device(image->scan.id);
  KASSERT(scan && scan->published && !scan->scratch);
  scan->device = image->scan.device;
  scan->snapshot = image->scan.snapshot;
  scan->relocatable = false;
}
