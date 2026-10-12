#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/npfs.h>
#include <kernel/gpt.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/disk.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include "../fs/npfs_store.h"

#define DISK_BLANK_SCAN_MAX_BYTES 65536u

static struct disk_object *retired;

static void destroy_disk(struct kernel_object *object)
{
  disk_retire((struct disk_object *)object);
}

bool disk_device_claimed(block_device_id device)
{
  return storage_extent_device_claimed(device);
}

bool disk_partition_claimed(block_device_id device, uint32_t partition)
{
  npfs_require_worker();
  const struct storage_extent *claim = storage_extent_device_claim(device);
  if (!claim) {
    return false;
  }
  struct block_info info;
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(device, &info);
  bool overlap = result == BLOCK_OK && !claim->first_block &&
      claim->block_count == info.block_count;
  const struct gpt_snapshot *snapshot = gpt_get_snapshot(device);
  for (size_t i = 0; !overlap && partition && snapshot &&
      i < snapshot->partition_count; ++i) {
    const struct gpt_partition *selected = &snapshot->partitions[i];
    if (selected->entry_number == partition) {
      overlap = selected->first_block < claim->first_block + claim->block_count &&
          claim->first_block < selected->first_block + selected->block_count;
    }
  }
  cpu_restore_interrupts(flags);
  return overlap;
}

void disk_retire(struct disk_object *disk)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  disk->cleanup_group = object_cleanup_defer();
  disk->retired_next = retired;
  retired = disk;
  npfs_notify();
}

bool disk_retired_pending(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  return retired != NULL;
}

struct disk_object *disk_take_retired(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct disk_object *result = retired;
  retired = NULL;
  return result;
}

static enum call_status block_status(enum block_result result)
{
  switch (result) {
  case BLOCK_OK: return CALL_OK;
  case BLOCK_FULL:
  case BLOCK_BUSY: return CALL_BUSY;
  case BLOCK_INVALID: return CALL_BAD_REQUEST;
  case BLOCK_READ_ONLY: return CALL_READ_ONLY;
  case BLOCK_TIMED_OUT: return CALL_TIMED_OUT;
  case BLOCK_UNAVAILABLE:
  case BLOCK_UNSUPPORTED: return CALL_UNAVAILABLE;
  default: return CALL_IO;
  }
}

static enum call_status submit_transfer(struct npfs_store_context *context,
    block_device_id device, enum block_operation operation, uint64_t first,
    uint32_t count, size_t length, void *bytes)
{
  struct block_ticket ticket;
  enum block_result result;
  for (;;) {
    if (task_deadline_expired(context->deadline)) {
      return CALL_TIMED_OUT;
    }
    uint64_t flags = cpu_save_interrupts();
    result = block_submit(device, operation, first, count,
        operation == BLOCK_WRITE ? bytes : NULL, &ticket);
    cpu_restore_interrupts(flags);
    if (result != BLOCK_FULL) {
      break;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  if (result != BLOCK_OK) {
    return block_status(result);
  }
  result = block_wait(&ticket, context->deadline);
  uint64_t flags = cpu_save_interrupts();
  if (result != BLOCK_OK) {
    KASSERT(block_abandon(&ticket) == BLOCK_OK);
    cpu_restore_interrupts(flags);
    return block_status(result);
  }
  struct block_completion completion;
  result = block_collect(&ticket, operation == BLOCK_READ ? bytes : NULL,
      operation == BLOCK_READ ? length : 0, &completion);
  KASSERT(result == BLOCK_OK);
  cpu_restore_interrupts(flags);
  if (completion.result != BLOCK_OK) {
    return block_status(completion.result);
  }
  return completion.bytes == length ? CALL_OK : CALL_IO;
}

static enum call_status raw_transfer(struct npfs_store_context *context,
    block_device_id device, enum block_operation operation, uint64_t offset,
    size_t length, void *bytes)
{
  struct block_info info;
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(device, &info);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    return block_status(result);
  }
  uint64_t first = 0;
  uint32_t count = 0;
  if (operation != BLOCK_FLUSH) {
    if (!length || length > DISK_IO_MAX_BYTES || offset % info.block_size ||
        length % info.block_size) {
      return CALL_BAD_REQUEST;
    }
    first = offset / info.block_size;
    count = length / info.block_size;
    if (first >= info.block_count || count > info.block_count - first) {
      return CALL_BAD_REQUEST;
    }
  }
  if (operation == BLOCK_FLUSH) {
    return submit_transfer(context, device, operation, 0, 0, 0, NULL);
  }
  uint8_t *cursor = bytes;
  while (count) {
    uint32_t transfer = info.max_transfer / info.block_size;
    if (transfer > count) {
      transfer = count;
    }
    size_t transfer_bytes = (size_t)transfer * info.block_size;
    enum call_status status = submit_transfer(context, device, operation, first,
        transfer, transfer_bytes, cursor);
    if (status != CALL_OK) {
      return status;
    }
    cursor += transfer_bytes;
    first += transfer;
    count -= transfer;
  }
  return CALL_OK;
}

static enum call_status await_gpt(struct npfs_store_context *context,
    block_device_id device)
{
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    bool ready = gpt_get_snapshot(device) != NULL;
    cpu_restore_interrupts(flags);
    if (ready) {
      return CALL_OK;
    }
    if (task_deadline_expired(context->deadline)) {
      return CALL_TIMED_OUT;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
}

static enum call_status await_discovery(struct npfs_store_context *context)
{
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    bool finished = block_discovery_finished();
    cpu_restore_interrupts(flags);
    if (finished) {
      return CALL_OK;
    }
    if (task_deadline_expired(context->deadline)) {
      return CALL_TIMED_OUT;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
}

static uint64_t public_gpt_status(enum gpt_status status)
{
  switch (status) {
  case GPT_HEALTHY: return DISK_GPT_HEALTHY;
  case GPT_DEGRADED: return DISK_GPT_DEGRADED;
  case GPT_AMBIGUOUS: return DISK_GPT_AMBIGUOUS;
  case GPT_ABSENT: return DISK_GPT_ABSENT;
  case GPT_INVALID: return DISK_GPT_INVALID;
  case GPT_UNSUPPORTED: return DISK_GPT_UNSUPPORTED;
  case GPT_UNAVAILABLE: return DISK_GPT_UNAVAILABLE;
  case GPT_NO_MEMORY: return DISK_GPT_NO_MEMORY;
  case GPT_IO_ERROR: return DISK_GPT_IO_ERROR;
  case GPT_TIMED_OUT: return DISK_GPT_TIMED_OUT;
  }
  return DISK_GPT_UNAVAILABLE;
}

static void disk_info(block_device_id device, struct disk_info *reply)
{
  *reply = (struct disk_info){.id = device, .gpt_status = DISK_GPT_PENDING};
  uint64_t flags = cpu_save_interrupts();
  enum block_preparation preparation = block_preparation_result(device);
  reply->preparation = preparation == BLOCK_DEVICE_READY ? DISK_READY :
      preparation == BLOCK_DEVICE_UNSUPPORTED ? DISK_UNSUPPORTED : DISK_SETUP_FAILED;
  struct block_info info;
  if (block_get_info(device, &info) == BLOCK_OK) {
    reply->block_count = info.block_count;
    reply->block_size = info.block_size;
    reply->flags = (info.writable ? DISK_FLAG_WRITABLE : 0) |
        (info.flush_supported ? DISK_FLAG_FLUSH_SUPPORTED : 0) |
        (info.write_failed ? DISK_FLAG_WRITE_FAILED : 0);
  }
  const struct gpt_snapshot *snapshot = gpt_get_snapshot(device);
  if (snapshot) {
    reply->gpt_status = public_gpt_status(snapshot->status);
    if (snapshot->status == GPT_HEALTHY || snapshot->status == GPT_DEGRADED) {
      memcpy(reply->gpt_guid, snapshot->disk_guid.bytes, sizeof(reply->gpt_guid));
    }
  }
  cpu_restore_interrupts(flags);
  if (storage_extent_device_mounted(device)) {
    reply->flags |= DISK_FLAG_MOUNTED;
  }
  if (disk_device_claimed(device)) {
    reply->flags |= DISK_FLAG_CLAIMED;
  }
}

static enum call_status verify_raw_claim(struct npfs_store_context *context,
    block_device_id device, uint32_t partition)
{
  struct block_info info;
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(device, &info);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    return block_status(result);
  }
  if (!info.raw_claim_requires_blank) {
    return CALL_OK;
  }
  if (partition) {
    return CALL_DENIED;
  }
  size_t capacity = info.max_transfer;
  if (capacity > DISK_BLANK_SCAN_MAX_BYTES) {
    capacity = DISK_BLANK_SCAN_MAX_BYTES;
  }
  if (!info.block_count || !info.block_size || info.block_size > capacity) {
    return CALL_UNAVAILABLE;
  }
  uint32_t transfer_blocks = capacity / info.block_size;
  capacity = (size_t)transfer_blocks * info.block_size;
  flags = cpu_save_interrupts();
  uint8_t *bytes = kmalloc(capacity);
  cpu_restore_interrupts(flags);
  if (!bytes) {
    return CALL_NO_MEMORY;
  }
  /* The sole filesystem worker excludes mounts and other raw writers until
   * admission. The zero proof is not cached after admission. */
  enum call_status status = CALL_OK;
  uint64_t first = 0;
  while (first < info.block_count) {
    uint32_t count = transfer_blocks;
    if (count > info.block_count - first) {
      count = info.block_count - first;
    }
    size_t length = (size_t)count * info.block_size;
    status = submit_transfer(context, device, BLOCK_READ, first, count, length, bytes);
    if (status != CALL_OK) {
      break;
    }
    for (size_t i = 0; i < length; ++i) {
      if (bytes[i]) {
        status = CALL_DENIED;
        break;
      }
    }
    if (status != CALL_OK) {
      break;
    }
    first += count;
  }
  flags = cpu_save_interrupts();
  kfree(bytes);
  cpu_restore_interrupts(flags);
  return status == CALL_OK && task_deadline_expired(context->deadline) ? CALL_TIMED_OUT : status;
}

static enum call_status reserve_claim(struct npfs_store_context *context,
    struct disk_object *disk, uint32_t partition, uint64_t first_block,
    uint64_t block_count)
{
  enum call_status status = storage_extent_reserve(&disk->extent, disk->device,
      first_block, block_count, STORAGE_EXTENT_RAW);
  if (status != CALL_OK) {
    return status;
  }
  status = verify_raw_claim(context, disk->device, partition);
  if (status != CALL_OK) {
    storage_extent_release(&disk->extent);
    return status;
  }
  disk->claimed = true;
  disk->partition = partition;
  return CALL_OK;
}

/* Claims a partition of an opened disk whose own claim was released. */
static enum call_status claim_partition(struct npfs_store_context *context,
    struct disk_object *disk, uint32_t partition)
{
  if (disk->claimed) {
    return CALL_BAD_REQUEST;
  }
  struct disk_info info;
  disk_info(disk->device, &info);
  if (info.preparation != DISK_READY) {
    return CALL_UNAVAILABLE;
  }
  if (!(info.flags & DISK_FLAG_WRITABLE) || !(info.flags & DISK_FLAG_FLUSH_SUPPORTED)) {
    return CALL_READ_ONLY;
  }
  if (info.flags & DISK_FLAG_WRITE_FAILED) {
    return CALL_IO;
  }
  if (!partition) {
    if (info.flags & (DISK_FLAG_MOUNTED | DISK_FLAG_CLAIMED)) {
      return CALL_BUSY;
    }
    return reserve_claim(context, disk, 0, 0, info.block_count);
  }
  enum call_status status = verify_raw_claim(context, disk->device, partition);
  if (status != CALL_OK) {
    return status;
  }
  if (info.flags & DISK_FLAG_CLAIMED) {
    return CALL_BUSY;
  }
  status = await_gpt(context, disk->device);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  const struct gpt_snapshot *snapshot = gpt_get_snapshot(disk->device);
  bool usable = snapshot->status == GPT_HEALTHY || snapshot->status == GPT_DEGRADED;
  bool found = false;
  struct gpt_partition selected;
  for (size_t i = 0; usable && i < snapshot->partition_count; ++i) {
    if (snapshot->partitions[i].entry_number == partition) {
      selected = snapshot->partitions[i];
      found = true;
    }
  }
  cpu_restore_interrupts(flags);
  if (!usable) {
    return CALL_UNAVAILABLE;
  }
  if (!found) {
    return CALL_NOT_FOUND;
  }
  if (!selected.block_count || selected.first_block >= info.block_count ||
      selected.block_count > info.block_count - selected.first_block) {
    return CALL_IO;
  }
  return reserve_claim(context, disk, partition, selected.first_block, selected.block_count);
}

static enum call_status release_claim(struct npfs_store_context *context,
    struct disk_object *disk)
{
  if (!disk->claimed) {
    return CALL_BAD_REQUEST;
  }
  enum call_status status = raw_transfer(context, disk->device, BLOCK_FLUSH, 0, 0, NULL);
  /* A partition claim cannot have written the table, and a pool elsewhere on
   * the device may still rely on the current scan. */
  enum gpt_status scanned = disk->partition ? GPT_HEALTHY : gpt_rescan(disk->device);
  if (status == CALL_OK) {
    if (scanned == GPT_IO_ERROR) {
      status = CALL_IO;
    } else if (scanned == GPT_TIMED_OUT) {
      status = CALL_TIMED_OUT;
    } else if (scanned == GPT_NO_MEMORY) {
      status = CALL_NO_MEMORY;
    } else if (scanned == GPT_UNAVAILABLE) {
      status = CALL_UNAVAILABLE;
    }
  }
  storage_extent_release(&disk->extent);
  disk->claimed = false;
  return status;
}

enum call_status disk_perform(struct npfs_store_context *context, struct npfs_job *job)
{
  npfs_require_worker();
  enum call_status discovery = await_discovery(context);
  if (discovery != CALL_OK) {
    return discovery;
  }
  uint64_t flags = cpu_save_interrupts();
  bool available = block_installer_inventory_available();
  if (job->operation == NPFS_RAW_INFO && !job->device) {
    job->device = block_installer_device_at(job->offset);
  }
  enum block_preparation preparation = block_preparation_result(job->device);
  bool installer = block_installer_device(job->device);
  cpu_restore_interrupts(flags);
  if (!available) {
    return CALL_UNAVAILABLE;
  }
  if (preparation == BLOCK_DEVICE_INVALID || !installer) {
    return CALL_NOT_FOUND;
  }
  if (job->operation == NPFS_RAW_INFO) {
    disk_info(job->device, &job->disk_info);
    return CALL_OK;
  }
  if (job->operation == NPFS_RAW_OPEN) {
    struct disk_info info;
    disk_info(job->device, &info);
    bool writable = job->kind == DISK_ACCESS_READ_WRITE;
    if (writable && (info.flags & (DISK_FLAG_MOUNTED | DISK_FLAG_CLAIMED))) {
      return CALL_BUSY;
    }
    if (info.preparation != DISK_READY) {
      return CALL_UNAVAILABLE;
    }
    if (writable && (!(info.flags & DISK_FLAG_WRITABLE) ||
        !(info.flags & DISK_FLAG_FLUSH_SUPPORTED))) {
      return CALL_READ_ONLY;
    }
    if (writable && (info.flags & DISK_FLAG_WRITE_FAILED)) {
      return CALL_IO;
    }
    flags = cpu_save_interrupts();
    struct disk_object *disk = kmalloc(sizeof(*disk));
    if (disk) {
      *disk = (struct disk_object){.device = job->device};
    }
    cpu_restore_interrupts(flags);
    if (!disk) {
      return CALL_NO_MEMORY;
    }
    if (writable) {
      enum call_status status = storage_extent_reserve(&disk->extent, disk->device,
          0, info.block_count, STORAGE_EXTENT_RAW);
      if (status == CALL_OK) {
        status = await_gpt(context, job->device);
        if (status == CALL_OK) {
          status = verify_raw_claim(context, job->device, 0);
        }
        if (status != CALL_OK) {
          storage_extent_release(&disk->extent);
        }
      }
      if (status != CALL_OK) {
        flags = cpu_save_interrupts();
        kfree(disk);
        cpu_restore_interrupts(flags);
        return status;
      }
      disk->claimed = true;
      disk->partition = 0;
    }
    flags = cpu_save_interrupts();
    object_init(&disk->object, OBJECT_DISK, destroy_disk);
    cpu_restore_interrupts(flags);
    job->object = &disk->object;
    return CALL_OK;
  }
  if (!job->raw || job->raw->device != job->device) {
    return CALL_BAD_REQUEST;
  }
  if (job->operation == NPFS_RAW_CLAIM) {
    return claim_partition(context, job->raw, (uint32_t)job->offset);
  }
  if (job->operation != NPFS_RAW_READ && !job->raw->claimed) {
    return CALL_DENIED;
  }
  switch (job->operation) {
  case NPFS_RAW_READ:
    return raw_transfer(context, job->device, BLOCK_READ, job->offset, job->count, job->data);
  case NPFS_RAW_WRITE: {
    struct disk_object *disk = job->raw;
    if (disk->partition) {
      /* The block size was checked when the partition was claimed. */
      struct block_info info;
      uint64_t irq_flags = cpu_save_interrupts();
      enum block_result result = block_get_info(job->device, &info);
      cpu_restore_interrupts(irq_flags);
      if (result != BLOCK_OK) {
        return block_status(result);
      }
      uint64_t first = disk->extent.first_block * info.block_size;
      uint64_t bytes = disk->extent.block_count * info.block_size;
      if (job->offset < first || job->offset - first > bytes ||
          job->count > bytes - (job->offset - first)) {
        return CALL_DENIED;
      }
    }
    return raw_transfer(context, job->device, BLOCK_WRITE, job->offset, job->count, job->data);
  }
  case NPFS_RAW_FLUSH:
    return raw_transfer(context, job->device, BLOCK_FLUSH, 0, 0, NULL);
  case NPFS_RAW_RELEASE:
    return release_claim(context, job->raw);
  default: return CALL_BAD_OPERATION;
  }
}

void disk_cleanup_retired(struct disk_object *disks)
{
  npfs_require_worker();
  while (disks) {
    struct disk_object *disk = disks;
    disks = disk->retired_next;
    uint64_t flags = cpu_save_interrupts();
    struct execution_group *previous = object_cleanup_enter(disk->cleanup_group);
    cpu_restore_interrupts(flags);
    if (disk->claimed) {
      struct npfs_store_context context = {.deadline = task_deadline_after_ms(NPFS_TIMEOUT_MS)};
      enum call_status status = release_claim(&context, disk);
      if (status != CALL_OK) {
        klog("disk: device %u close cleanup failed (status %u)\n",
            (unsigned)disk->device, (unsigned)status);
      }
    }
    flags = cpu_save_interrupts();
    struct execution_group *group = disk->cleanup_group;
    kfree(disk);
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    cpu_restore_interrupts(flags);
  }
}
