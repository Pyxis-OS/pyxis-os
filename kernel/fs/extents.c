#include <arch/cpu.h>
#include <kernel/fs/extents.h>
#include <kernel/fs/npfs.h>
#include <kernel/panic.h>

static struct storage_extent *extents;

static bool overlaps(const struct storage_extent *extent, uint64_t first_block,
    uint64_t block_count)
{
  return first_block < extent->first_block + extent->block_count &&
      extent->first_block < first_block + block_count;
}

enum call_status storage_extent_reserve(struct storage_extent *extent,
    block_device_id device, uint64_t first_block, uint64_t block_count,
    enum storage_extent_kind kind)
{
  npfs_require_worker();
  KASSERT(extent && (kind == STORAGE_EXTENT_PENDING_NPFS ||
      kind == STORAGE_EXTENT_PENDING_FAT || kind == STORAGE_EXTENT_RAW));
  struct block_info info;
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(device, &info);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    return result == BLOCK_TIMED_OUT ? CALL_TIMED_OUT : CALL_UNAVAILABLE;
  }
  if (!block_count || first_block >= info.block_count ||
      block_count > info.block_count - first_block) {
    return CALL_BAD_REQUEST;
  }
  for (struct storage_extent *existing = extents; existing; existing = existing->next) {
    KASSERT(existing != extent);
    if (existing->device == device && (overlaps(existing, first_block, block_count) ||
        (kind == STORAGE_EXTENT_RAW && existing->kind == STORAGE_EXTENT_RAW))) {
      return CALL_BUSY;
    }
  }
  *extent = (struct storage_extent){
    .next = extents,
    .device = device,
    .first_block = first_block,
    .block_count = block_count,
    .kind = kind,
  };
  extents = extent;
  return CALL_OK;
}

void storage_extent_publish(struct storage_extent *extent, enum storage_extent_kind kind)
{
  npfs_require_worker();
  KASSERT((extent->kind == STORAGE_EXTENT_PENDING_NPFS && kind == STORAGE_EXTENT_NPFS) ||
      (extent->kind == STORAGE_EXTENT_PENDING_FAT && kind == STORAGE_EXTENT_FAT));
  struct storage_extent *existing = extents;
  while (existing != extent) {
    KASSERT(existing);
    existing = existing->next;
  }
  extent->kind = kind;
}

void storage_extent_release(struct storage_extent *extent)
{
  npfs_require_worker();
  struct storage_extent **link = &extents;
  while (*link != extent) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = extent->next;
  *extent = (struct storage_extent){0};
}

void storage_extent_transfer_raw(struct storage_extent *source,
    struct storage_extent *destination, uint64_t first_block, uint64_t block_count)
{
  npfs_require_worker();
  KASSERT(source != destination && source->kind == STORAGE_EXTENT_RAW && block_count &&
      first_block >= source->first_block &&
      first_block - source->first_block < source->block_count &&
      block_count <= source->block_count - (first_block - source->first_block));
  struct storage_extent **link = &extents;
  while (*link != source) {
    KASSERT(*link && *link != destination);
    link = &(*link)->next;
  }
  *destination = *source;
  destination->first_block = first_block;
  destination->block_count = block_count;
  *link = destination;
  *source = (struct storage_extent){0};
}

bool storage_extent_device_mounted(block_device_id device)
{
  npfs_require_worker();
  for (struct storage_extent *extent = extents; extent; extent = extent->next) {
    if (extent->device == device && extent->kind != STORAGE_EXTENT_RAW) {
      return true;
    }
  }
  return false;
}

bool storage_extent_range_mounted(block_device_id device, uint64_t first_block,
    uint64_t block_count)
{
  npfs_require_worker();
  KASSERT(block_count && block_count <= UINT64_MAX - first_block);
  for (struct storage_extent *extent = extents; extent; extent = extent->next) {
    if (extent->device == device && extent->kind != STORAGE_EXTENT_RAW &&
        overlaps(extent, first_block, block_count)) {
      return true;
    }
  }
  return false;
}

bool storage_extent_device_claimed(block_device_id device)
{
  return storage_extent_device_claim(device) != NULL;
}

const struct storage_extent *storage_extent_device_claim(block_device_id device)
{
  npfs_require_worker();
  for (struct storage_extent *extent = extents; extent; extent = extent->next) {
    if (extent->device == device && extent->kind == STORAGE_EXTENT_RAW) {
      return extent;
    }
  }
  return NULL;
}
