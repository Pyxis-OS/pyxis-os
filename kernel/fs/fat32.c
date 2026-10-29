#include <abi/mount.h>
#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/fs/extents.h>
#include <kernel/fs/fat32.h>
#include <kernel/fs/metadata.h>
#include <kernel/fs/npfs.h>
#include <kernel/memory.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/object/directory.h>
#include <kernel/object/disk.h>
#include <kernel/object/file.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/process.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>
#include <pyxis_fs/fat32.h>
#include <pyxis_fs/fat32_mounted.h>
#include <kernel-config.h>
#include "npfs_store.h"

#define FAT32_MOUNT_LIMIT 4u
#define FAT32_STEP_LIMIT UINT64_C(67108864)
#define FAT32_SECTOR_LIMIT UINT64_C(16777216)
/* Internal block transfers do not use the public DISK message payload. */
#define FAT32_READ_MAX_BYTES 65536u
#define FAT32_WRITE_MAX_BYTES 65536u
#define FAT32_DEPTH_LIMIT 64u
#define FAT32_INTERVAL_LIMIT 16u
/* What an upgrade writes on the booted ESP: the publisher's owned files and
 * retention of older owned attempts. */
#define BOOT_UPGRADE_ROOT_RIGHTS (FAT32_ROOT_RIGHTS | DIRECTORY_RIGHT_CREATE | \
    DIRECTORY_RIGHT_WRITE_FILES | DIRECTORY_RIGHT_REMOVE)
#define FAT32_BATCH_IMAGE_BYTES ((size_t)CONFIG_FAT32_BATCH_MIB * 1024u * 1024u)

struct fat32_interval {
  struct fat32_interval *next;
  struct process_control *actor;
  uint64_t id, deadline, previous_id, previous_deadline;
  bool active, pending;
};

struct fat32_mount {
  struct fat32_mount *next;
  struct storage_extent extent;
  block_device_id device;
  uint32_t sector_bytes; /* Device geometry retained before volume probing. */
  uint32_t max_write_sectors;
  struct fat32_volume volume;
  struct fat32_mounted writer;
  struct fat32_mirror_state mirrors;
  struct fat32_interval *intervals;
  struct fat32_workspace workspace;
  uint8_t *allocation;
  size_t allocation_bytes;
  uint8_t *mirror_map;
  size_t mirror_bytes;
  void *batch_memory;
  size_t batch_bytes;
  size_t backing_used, backing_budget;
  struct npfs_store_context *context; /* Borrowed only while this worker executes. */
  struct fat32_node *nodes;
  uint64_t domain;
  uint64_t read_only_free_clusters;
  size_t data_nodes, controls, interval_count;
  enum call_status backing_status, first_failure, maintenance_error;
  atomic_bool proofs_invalidated;
  bool probing, published, retired, writable, mirror_reported, read_only_capacity_valid;
  bool owned_esp;
};

enum fat32_scope {
  FAT_SCOPE_FOREIGN, FAT_SCOPE_ROOT, FAT_SCOPE_EFI, FAT_SCOPE_BOOT, FAT_SCOPE_OWNED,
};

struct fat32_node {
  struct fat32_node *next, *retired_next;
  struct fat32_mount *mount;
  struct fat32_locator locator;
  struct fat32_mounted_object record;
  size_t chain_bytes;
  uint64_t id, kind, ceiling, generation;
  atomic_size_t admitted;
  atomic_bool returned;
  struct execution_group *cleanup_group;
  enum call_status cleanup_status;
  bool cleanup_pending;
  /* Directory moves are unsupported. File renames must keep owned scope, so
   * held aliases and detached records retain their original authority. */
  enum fat32_scope scope;
  union {
    struct directory_object directory;
    struct file_object file;
    struct mount_object control;
  } wrapper;
};

static struct fat32_mount *mounts;
static struct fat32_node *retired_nodes;
static size_t instance_count, workspace_used;

static struct kernel_object *node_object(struct fat32_node *node)
{
  if (node->kind == OBJECT_MOUNT) {
    return &node->wrapper.control.object;
  }
  return node->kind == OBJECT_DIRECTORY ? &node->wrapper.directory.object :
      &node->wrapper.file.object;
}

void fat32_note_call_failure(struct kernel_object *object)
{
  struct fat32_node *node = NULL;
  if (!object) {
    return;
  }
  switch (object->type) {
  case OBJECT_FILE: {
    struct file_object *file = (struct file_object *)object;
    node = file->backing == FILE_FAT32 ? file->fat32 : NULL;
    break;
  }
  case OBJECT_DIRECTORY: {
    struct directory_object *directory = (struct directory_object *)object;
    node = directory->backing == DIRECTORY_FAT32 ? directory->fat32 : NULL;
    break;
  }
  case OBJECT_MOUNT: {
    struct mount_object *mount = (struct mount_object *)object;
    node = mount->backend == MOUNT_FAT32 ? mount->fat32 : NULL;
    break;
  }
  default:
    break;
  }
  if (node && node->mount->writable) {
    /* The held object keeps the mount alive; only the worker changes epochs. */
    atomic_store_explicit(&node->mount->proofs_invalidated, true, memory_order_release);
  }
}

void fat32_note_job_failure(struct npfs_job *job)
{
  npfs_require_worker();
  if (job->fat_node && job->fat_node->mount->writable) {
    fat32_mounted_invalidate_proofs(&job->fat_node->mount->writer);
  }
}

static enum call_status block_status(enum block_result result)
{
  switch (result) {
  case BLOCK_OK: return CALL_OK;
  case BLOCK_TIMED_OUT: return CALL_TIMED_OUT;
  case BLOCK_UNAVAILABLE:
  case BLOCK_UNSUPPORTED: return CALL_UNAVAILABLE;
  default: return CALL_IO;
  }
}

static bool read_sectors(void *argument, uint64_t first, uint32_t count, void *bytes)
{
  struct fat32_mount *mount = argument;
  struct npfs_store_context *context = mount->context;
  KASSERT(context);
  if (!count || count > FAT32_READ_MAX_BYTES / mount->sector_bytes ||
      (!mount->probing && (first < mount->extent.first_block ||
       first - mount->extent.first_block >= mount->extent.block_count ||
       count > mount->extent.block_count - (first - mount->extent.first_block)))) {
    mount->backing_status = CALL_IO;
    return false;
  }
  struct block_ticket ticket;
  enum block_result result;
  for (;;) {
    if (task_deadline_expired(context->deadline)) {
      mount->backing_status = CALL_TIMED_OUT;
      return false;
    }
    uint64_t flags = cpu_save_interrupts();
    result = block_submit(mount->extent.device, BLOCK_READ, first, count, NULL, &ticket);
    cpu_restore_interrupts(flags);
    if (result != BLOCK_FULL) {
      break;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  if (result == BLOCK_OK) {
    result = block_wait(&ticket, context->deadline);
    uint64_t flags = cpu_save_interrupts();
    if (result != BLOCK_OK) {
      KASSERT(block_abandon(&ticket) == BLOCK_OK);
    } else {
      struct block_completion completion;
      KASSERT(block_collect(&ticket, bytes, count * mount->sector_bytes, &completion) == BLOCK_OK);
      result = completion.result;
      if (result == BLOCK_OK && completion.bytes != count * mount->sector_bytes) {
        result = BLOCK_IO_ERROR;
      }
    }
    cpu_restore_interrupts(flags);
  }
  if (result != BLOCK_OK) {
    context->backing_error = result;
    mount->backing_status = block_status(result);
    return false;
  }
  return true;
}

static bool write_backing(struct fat32_mount *mount, enum block_operation operation,
    uint64_t first, uint32_t count, const void *bytes)
{
  struct npfs_store_context *context = mount->context;
  KASSERT(context && mount->writable);
  if (operation == BLOCK_WRITE && (!count || count > mount->max_write_sectors ||
      first < mount->extent.first_block || first - mount->extent.first_block >=
      mount->extent.block_count || count > mount->extent.block_count -
      (first - mount->extent.first_block))) {
    mount->backing_status = CALL_IO;
    return false;
  }
  struct block_ticket ticket;
  enum block_result result;
  for (;;) {
    if (task_deadline_expired(context->deadline)) {
      mount->backing_status = CALL_TIMED_OUT;
      return false;
    }
    uint64_t flags = cpu_save_interrupts();
    result = block_submit(mount->device, operation, first, count, bytes, &ticket);
    cpu_restore_interrupts(flags);
    if (result != BLOCK_FULL) {
      break;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  if (result == BLOCK_OK) {
    result = block_wait(&ticket, context->deadline);
    uint64_t flags = cpu_save_interrupts();
    if (result != BLOCK_OK) {
      KASSERT(block_abandon(&ticket) == BLOCK_OK);
    } else {
      struct block_completion completion;
      KASSERT(block_collect(&ticket, NULL, 0, &completion) == BLOCK_OK);
      result = completion.result;
      if (result == BLOCK_OK && completion.bytes != count * mount->sector_bytes) {
        result = BLOCK_IO_ERROR;
      }
    }
    cpu_restore_interrupts(flags);
  }
  if (result != BLOCK_OK) {
    context->backing_error = result;
    mount->backing_status = block_status(result);
    return false;
  }
  return true;
}

static bool write_sectors(void *context, uint64_t first, uint32_t count, const void *bytes)
{
  return write_backing(context, BLOCK_WRITE, first, count, bytes);
}

static bool flush_backing(void *context)
{
  return write_backing(context, BLOCK_FLUSH, 0, 0, NULL);
}

static void report_mirror(struct fat32_mount *mount)
{
  if (!mount->mirrors.mismatch_seen || mount->mirror_reported) {
    return;
  }
  mount->mirror_reported = true;
  kwarn("fat32: device %u GPT %u FAT2 differs; using FAT1 (%u sectors pending, %s)\n",
      mount->device, mount->volume.gpt_entry, fat32_mirror_pending(&mount->volume),
      mount->writable ? "RW" : "read-only, no repair");
}

static enum call_status format_status(struct fat32_mount *mount, enum fat32_status status)
{
  if (status == FAT32_IO || status == FAT32_CORRUPT || status == FAT32_STALE) {
    mount->read_only_capacity_valid = false;
  }
  if (mount->writable && mount->writer.stopped) {
    if (mount->first_failure == CALL_OK) {
      mount->first_failure = mount->backing_status == CALL_OK ? CALL_IO : mount->backing_status;
    }
    return CALL_OUTCOME_UNKNOWN;
  }
  switch (status) {
  case FAT32_OK: return CALL_OK;
  case FAT32_INVALID: return CALL_BAD_REQUEST;
  case FAT32_UNSUPPORTED: return CALL_UNAVAILABLE;
  case FAT32_IO: return mount->backing_status == CALL_OK ? CALL_IO : mount->backing_status;
  case FAT32_NO_MEMORY: return CALL_NO_MEMORY;
  case FAT32_LIMIT: return CALL_LIMIT;
  case FAT32_NAME_TOO_LONG: return CALL_NAME_TOO_LONG;
  case FAT32_NOT_FOUND: return CALL_NOT_FOUND;
  case FAT32_EXISTS: return CALL_ALREADY_EXISTS;
  case FAT32_NO_SPACE: return CALL_NO_SPACE;
  case FAT32_READ_ONLY: return CALL_READ_ONLY;
  case FAT32_NOT_EMPTY: return CALL_NOT_EMPTY;
  case FAT32_STOPPED: return CALL_OUTCOME_UNKNOWN;
  case FAT32_NOT_DIRECTORY:
  case FAT32_IS_DIRECTORY: return CALL_WRONG_TYPE;
  default: return CALL_IO;
  }
}

static enum call_status charge_backing(struct fat32_mount *mount, size_t bytes)
{
  KASSERT(mount->backing_used <= mount->backing_budget);
  if (bytes > mount->backing_budget - mount->backing_used ||
      bytes > SIZE_MAX - workspace_used) {
    return CALL_LIMIT;
  }
  mount->backing_used += bytes;
  workspace_used += bytes;
  return CALL_OK;
}

static void release_backing(struct fat32_mount *mount, size_t bytes)
{
  KASSERT(bytes <= mount->backing_used && bytes <= workspace_used);
  mount->backing_used -= bytes;
  workspace_used -= bytes;
}

static uint64_t chain_capacity_limit(const struct fat32_volume *volume)
{
  uint64_t cluster_bytes = (uint64_t)volume->sectors_per_cluster * volume->source.sector_bytes;
  uint64_t limit = (UINT32_MAX + cluster_bytes - 1) / cluster_bytes;
  return limit > (uint64_t)volume->cluster_count + 2 ? limit :
      (uint64_t)volume->cluster_count + 2;
}

static enum call_status configure_budget(struct fat32_mount *mount)
{
  const struct fat32_volume *volume = &mount->volume;
  size_t map, reader, operation, mirror;
  enum call_status status = format_status(mount, fat32_mounted_allocation_size(volume, &map));
  if (status == CALL_OK) {
    status = format_status(mount, fat32_workspace_size(volume, FAT32_DEPTH_LIMIT, &reader));
  }
  if (status == CALL_OK) {
    status = format_status(mount, fat32_mirror_size(volume, &mirror));
  }
  if (status == CALL_OK && mount->writable) {
    status = format_status(mount, fat32_mounted_batch_size(volume,
        FAT32_BATCH_IMAGE_BYTES, mount->max_write_sectors, &mount->batch_bytes));
  }
  uint64_t cluster_bytes = (uint64_t)volume->sectors_per_cluster * volume->source.sector_bytes;
  uint64_t changed = UINT32_MAX + 2 * cluster_bytes + FILE_TRANSFER_MAX_BYTES;
  if (status == CALL_OK) {
    status = format_status(mount, fat32_mounted_workspace_size(volume, changed,
        volume->cluster_count, volume->cluster_count, mount->max_write_sectors,
        &operation));
  }
  if (status != CALL_OK) {
    return status;
  }
  size_t maximum = operation > reader ? operation : reader;
  if (maximum < fat32_open_workspace_size()) {
    maximum = fat32_open_workspace_size();
  }
  uint64_t chain = chain_capacity_limit(volume);
  if (chain > SIZE_MAX / sizeof(uint32_t) || mirror > SIZE_MAX - map ||
      mount->batch_bytes > SIZE_MAX - map - mirror) {
    return CALL_LIMIT;
  }
  size_t chains = (size_t)chain * sizeof(uint32_t);
  size_t fixed = map + mirror + mount->batch_bytes;
  if (maximum > (SIZE_MAX - fixed) / 2) {
    return CALL_LIMIT;
  }
  fixed += 2 * maximum;
  if (chains > (SIZE_MAX - fixed) / (NPFS_WRAPPER_LIMIT + 1)) {
    return CALL_LIMIT;
  }
  /* A geometry/ABI sanity bound, not a reservation. Retained chains may keep
   * capacity after shrink; include all wrappers and one replacement overlap. */
  mount->backing_budget = fixed + (NPFS_WRAPPER_LIMIT + 1) * chains;
  return CALL_OK;
}

static enum call_status ensure_workspace(struct fat32_mount *mount, size_t bytes)
{
  if (mount->writer.stopped) {
    return CALL_OUTCOME_UNKNOWN;
  }
  if (bytes <= mount->workspace.bytes) {
    return CALL_OK;
  }
  /* Both buffers remain live until replacement succeeds. */
  enum call_status status = charge_backing(mount, bytes);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  void *memory = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!memory) {
    release_backing(mount, bytes);
    return CALL_NO_MEMORY;
  }
  flags = cpu_save_interrupts();
  kfree(mount->workspace.memory);
  cpu_restore_interrupts(flags);
  release_backing(mount, mount->workspace.bytes);
  mount->workspace.memory = memory;
  mount->workspace.bytes = bytes;
  return CALL_OK;
}

static enum call_status prepare_workspace(struct fat32_mount *mount, uint64_t changed,
    uint64_t allocated, uint64_t changed_clusters)
{
  if (allocated > mount->writer.volume.cluster_count ||
      changed_clusters > mount->writer.volume.cluster_count) {
    return CALL_LIMIT;
  }
  size_t bytes;
  enum call_status status = format_status(mount, fat32_mounted_workspace_size(
      &mount->writer.volume, changed, (uint32_t)allocated,
      (uint32_t)changed_clusters, mount->max_write_sectors, &bytes));
  return status == CALL_OK ? ensure_workspace(mount, bytes) : status;
}

static enum call_status prepare_forget(struct fat32_node *node)
{
  return node->record.attached || !node->record.chain_count ? CALL_OK :
      prepare_workspace(node->mount, 0, 0, node->record.chain_count);
}

static enum call_status allocate_map(struct fat32_mount *mount, uint8_t **map, size_t *bytes)
{
  enum call_status status = format_status(mount,
      fat32_mounted_allocation_size(&mount->volume, bytes));
  if (status != CALL_OK) {
    return status;
  }
  status = charge_backing(mount, *bytes);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  *map = kmalloc(*bytes);
  cpu_restore_interrupts(flags);
  if (!*map) {
    release_backing(mount, *bytes);
    return CALL_NO_MEMORY;
  }
  return CALL_OK;
}

static void free_map(struct fat32_mount *mount, uint8_t *map, size_t bytes)
{
  if (!map) {
    return;
  }
  release_backing(mount, bytes);
  uint64_t flags = cpu_save_interrupts();
  kfree(map);
  cpu_restore_interrupts(flags);
}

static enum call_status allocate_batch(struct fat32_mount *mount)
{
  enum call_status status = charge_backing(mount, mount->batch_bytes);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  mount->batch_memory = kmalloc(mount->batch_bytes);
  cpu_restore_interrupts(flags);
  if (!mount->batch_memory) {
    release_backing(mount, mount->batch_bytes);
    return CALL_NO_MEMORY;
  }
  return CALL_OK;
}

static void free_batch(struct fat32_mount *mount)
{
  free_map(mount, mount->batch_memory, mount->batch_bytes);
  mount->batch_memory = NULL;
  mount->batch_bytes = 0;
}

static enum call_status allocate_mirror(struct fat32_mount *mount)
{
  enum call_status status = format_status(mount,
      fat32_mirror_size(&mount->volume, &mount->mirror_bytes));
  if (status != CALL_OK) {
    return status;
  }
  if (mount->mirror_bytes) {
    status = charge_backing(mount, mount->mirror_bytes);
    if (status != CALL_OK) {
      return status;
    }
    uint64_t flags = cpu_save_interrupts();
    mount->mirror_map = kmalloc(mount->mirror_bytes);
    cpu_restore_interrupts(flags);
    if (!mount->mirror_map) {
      release_backing(mount, mount->mirror_bytes);
      return CALL_NO_MEMORY;
    }
    memset(mount->mirror_map, 0, mount->mirror_bytes);
  }
  return format_status(mount, fat32_mirror_bind(&mount->volume,
      mount->mirror_map, mount->mirror_bytes));
}

static void free_interval(struct fat32_mount *mount, struct fat32_interval *interval)
{
  KASSERT(mount->interval_count);
  --mount->interval_count;
  uint64_t flags = cpu_save_interrupts();
  object_release(&interval->actor->object);
  cpu_restore_interrupts(flags);
  npfs_adapter_free(interval, sizeof(*interval));
}

static void update_intervals(struct fat32_mount *mount)
{
  struct fat32_interval **link = &mount->intervals;
  while (*link) {
    struct fat32_interval *interval = *link;
    if (interval->pending) {
      link = &interval->next;
      continue;
    }
    uint64_t flags = cpu_save_interrupts();
    bool stopped = process_control_stopped(interval->actor);
    cpu_restore_interrupts(flags);
    if (stopped) {
      *link = interval->next;
      free_interval(mount, interval);
      continue;
    }
    if (task_deadline_expired(interval->deadline)) {
      interval->active = false;
    }
    link = &interval->next;
  }
}

static bool active_interval(struct fat32_mount *mount)
{
  for (struct fat32_interval *interval = mount->intervals; interval; interval = interval->next) {
    if (interval->active) {
      return true;
    }
  }
  return false;
}

static bool actor_stopped(struct process_control *actor)
{
  uint64_t flags = cpu_save_interrupts();
  bool stopped = process_control_stopped(actor);
  cpu_restore_interrupts(flags);
  return stopped;
}

static enum call_status mutation_admission(struct fat32_mount *mount, struct npfs_job *job)
{
  if (!mount->writable) {
    return CALL_READ_ONLY;
  }
  if (mount->writer.stopped) {
    return CALL_OUTCOME_UNKNOWN;
  }
  if (!job->fat_actor) {
    return CALL_DENIED;
  }
  if (actor_stopped(job->fat_actor)) {
    return CALL_ENDPOINT_CLOSED;
  }
  update_intervals(mount);
  bool other_owner = false;
  for (struct fat32_interval *interval = mount->intervals; interval; interval = interval->next) {
    if (interval->actor == job->fat_actor && !interval->active) {
      return CALL_TIMED_OUT;
    }
    if (interval->active && interval->actor != job->fat_actor) {
      other_owner = true;
    }
    if (interval->pending) {
      other_owner = true;
    }
  }
  return other_owner ? CALL_BUSY : CALL_OK;
}

static enum call_status interval_operation(struct fat32_mount *mount, struct npfs_job *job)
{
  if (!mount->writable || mount->retired) {
    return CALL_UNAVAILABLE;
  }
  if (!job->fat_actor) {
    return CALL_DENIED;
  }
  if (actor_stopped(job->fat_actor)) {
    return CALL_ENDPOINT_CLOSED;
  }
  update_intervals(mount);
  struct fat32_interval **owned = &mount->intervals;
  while (*owned && (*owned)->actor != job->fat_actor) {
    owned = &(*owned)->next;
  }
  if (job->operation == NPFS_FAT_END) {
    if (!*owned || (*owned)->id != job->interval_id) {
      return CALL_BAD_REQUEST;
    }
    if ((*owned)->pending) {
      return CALL_BUSY;
    }
    struct fat32_interval *interval = *owned;
    *owned = interval->next;
    free_interval(mount, interval);
    return CALL_OK;
  }
  if (mount->writer.stopped) {
    return CALL_OUTCOME_UNKNOWN;
  }
  if (active_interval(mount)) {
    return CALL_BUSY;
  }
  struct fat32_interval *interval = *owned;
  if (!interval) {
    if (mount->interval_count == FAT32_INTERVAL_LIMIT) {
      return CALL_LIMIT;
    }
    enum call_status status = npfs_adapter_allocate(sizeof(*interval), (void **)&interval);
    if (status != CALL_OK) {
      return status;
    }
  }
  uint64_t id;
  uint64_t flags = cpu_save_interrupts();
  bool identified = fs_metadata_allocate_id(&id);
  if (identified && !*owned) {
    object_retain(&job->fat_actor->object);
  }
  cpu_restore_interrupts(flags);
  if (!identified) {
    if (!*owned) {
      npfs_adapter_free(interval, sizeof(*interval));
    }
    return CALL_LIMIT;
  }
  if (!*owned) {
    interval->actor = job->fat_actor;
    interval->next = mount->intervals;
    mount->intervals = interval;
    ++mount->interval_count;
  } else {
    interval->previous_id = interval->id;
    interval->previous_deadline = interval->deadline;
  }
  interval->id = id;
  interval->deadline = task_deadline_after_ms(30000);
  interval->active = true;
  interval->pending = true;
  job->interval_id = interval->id;
  job->interval_deadline = interval->deadline;
  return CALL_OK;
}

static void finish_begin(struct fat32_mount *mount, struct npfs_job *job)
{
  struct fat32_interval **link = &mount->intervals;
  while (*link && (*link)->id != job->interval_id) {
    link = &(*link)->next;
  }
  struct fat32_interval *interval = *link;
  KASSERT(interval && interval->pending && interval->actor == job->fat_actor);
  if (!job->interval_delivered && !interval->previous_id) {
    *link = interval->next;
    free_interval(mount, interval);
  } else {
    if (!job->interval_delivered) {
      interval->id = interval->previous_id;
      interval->deadline = interval->previous_deadline;
      interval->active = false;
    }
    interval->previous_id = 0;
    interval->previous_deadline = 0;
    interval->pending = false;
  }
  update_intervals(mount);
}

static void free_workspace(struct fat32_mount *mount)
{
  if (!mount->workspace.memory) {
    return;
  }
  release_backing(mount, mount->workspace.bytes);
  uint64_t flags = cpu_save_interrupts();
  kfree(mount->workspace.memory);
  cpu_restore_interrupts(flags);
  mount->workspace.memory = NULL;
  mount->workspace.bytes = 0;
}

static void free_chain(struct fat32_node *node)
{
  if (!node->chain_bytes) {
    return;
  }
  release_backing(node->mount, node->chain_bytes);
  uint64_t flags = cpu_save_interrupts();
  kfree(node->record.chain);
  cpu_restore_interrupts(flags);
  node->record.chain = NULL;
  node->record.chain_capacity = 0;
  node->chain_bytes = 0;
}

static enum call_status reserve_chain(struct fat32_node *node, uint64_t capacity)
{
  if (capacity <= node->record.chain_capacity) {
    return CALL_OK;
  }
  uint64_t limit = chain_capacity_limit(&node->mount->volume);
  if (limit > UINT32_MAX || limit > SIZE_MAX / sizeof(uint32_t) || capacity > limit) {
    return CALL_LIMIT;
  }
  uint64_t grown = node->record.chain_capacity ? node->record.chain_capacity : 1;
  while (grown < capacity) {
    grown = grown > limit / 2 ? limit : 2 * grown;
  }
  capacity = grown;
  size_t bytes = capacity * sizeof(uint32_t);
  enum call_status status = charge_backing(node->mount, bytes);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  uint32_t *chain = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!chain) {
    release_backing(node->mount, bytes);
    return CALL_NO_MEMORY;
  }
  uint32_t *previous = node->record.chain;
  status = CALL_OK;
  if (node->record.mounted) {
    status = format_status(node->mount, fat32_mounted_object_reserve(
        &node->record, chain, capacity));
  } else {
    node->record.chain = chain;
    node->record.chain_capacity = capacity;
  }
  if (status != CALL_OK) {
    release_backing(node->mount, bytes);
    flags = cpu_save_interrupts();
    kfree(chain);
    cpu_restore_interrupts(flags);
    return status;
  }
  flags = cpu_save_interrupts();
  kfree(previous);
  cpu_restore_interrupts(flags);
  release_backing(node->mount, node->chain_bytes);
  node->chain_bytes = bytes;
  return CALL_OK;
}

static void remove_instance(struct fat32_mount *mount)
{
  KASSERT(!mount->data_nodes && !mount->controls && !mount->nodes && !mount->context);
  while (mount->intervals) {
    struct fat32_interval *interval = mount->intervals;
    mount->intervals = interval->next;
    free_interval(mount, interval);
  }
  if (!mount->retired) {
    free_workspace(mount);
    free_map(mount, mount->allocation, mount->allocation_bytes);
    free_map(mount, mount->mirror_map, mount->mirror_bytes);
    free_batch(mount);
    storage_extent_release(&mount->extent);
    KASSERT(instance_count);
    --instance_count;
  }
  KASSERT(!mount->backing_used);
  struct fat32_mount **link = &mounts;
  while (*link != mount) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = mount->next;
  npfs_adapter_free(mount, sizeof(*mount));
}

void fat32_mark_returned(struct fat32_node *node)
{
  atomic_store_explicit(&node->returned, true, memory_order_release);
}

void fat32_request_published(struct fat32_node *node)
{
  atomic_fetch_add_explicit(&node->admitted, 1, memory_order_relaxed);
}

void fat32_request_complete(struct fat32_node *node)
{
  KASSERT(atomic_fetch_sub_explicit(&node->admitted, 1, memory_order_relaxed));
}

static bool other_requests(struct fat32_mount *mount, struct fat32_node *current)
{
  for (struct fat32_node *node = mount->nodes; node; node = node->next) {
    size_t own = node == current ? 1 : 0;
    if (atomic_load_explicit(&node->admitted, memory_order_acquire) > own) {
      return true;
    }
  }
  return false;
}

void fat32_retire_node(struct fat32_node *node)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  node->cleanup_group = object_cleanup_defer();
  node->retired_next = retired_nodes;
  retired_nodes = node;
  npfs_notify();
}

struct fat32_node *fat32_take_retired(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct fat32_node *nodes = retired_nodes;
  retired_nodes = NULL;
  return nodes;
}

void fat32_cleanup_nodes(struct fat32_node *nodes)
{
  npfs_require_worker();
  while (nodes) {
    struct fat32_node *node = nodes;
    nodes = node->retired_next;
    struct fat32_mount *mount = node->mount;
    struct execution_group *group = node->cleanup_group;
    node->cleanup_group = NULL;
    if (node->kind != OBJECT_MOUNT && mount->writable && node->record.mounted) {
      fat32_mounted_invalidate_proofs(&mount->writer);
      struct npfs_store_context *previous = mount->context;
      struct npfs_store_context context = {.deadline = task_deadline_after_ms(NPFS_TIMEOUT_MS)};
      mount->context = previous ? previous : &context;
      enum call_status status = prepare_forget(node);
      if (status == CALL_OK) {
        status = format_status(mount, fat32_mounted_object_forget(&node->record,
            &mount->workspace));
      }
      mount->context = previous;
      if (status != CALL_OK) {
        node->cleanup_pending = true;
        node->cleanup_status = status;
        /* Retain the failed record/chain under mount accounting, not a dead
         * process or execution group's cleanup ownership. */
        if (group) {
          uint64_t flags = cpu_save_interrupts();
          execution_group_cleanup_end(group);
          cpu_restore_interrupts(flags);
        }
        continue;
      }
    }
    struct fat32_node **link = &mount->nodes;
    while (*link != node) {
      KASSERT(*link);
      link = &(*link)->next;
    }
    *link = node->next;
    if (node->kind == OBJECT_MOUNT) {
      if (atomic_load_explicit(&node->returned, memory_order_acquire)) {
        mount->published = true;
      }
      KASSERT(mount->controls);
      --mount->controls;
    } else {
      KASSERT(mount->data_nodes);
      --mount->data_nodes;
    }
    free_chain(node);
    npfs_adapter_free(node, sizeof(*node));
    if (!mount->data_nodes && !mount->controls && (mount->retired || !mount->published)) {
      remove_instance(mount);
    }
    if (group) {
      uint64_t flags = cpu_save_interrupts();
      execution_group_cleanup_end(group);
      cpu_restore_interrupts(flags);
    }
  }
}

static enum call_status retry_pending_cleanup(struct fat32_mount *mount)
{
  enum call_status status = CALL_OK;
  struct fat32_node *node = mount->nodes;
  while (node) {
    struct fat32_node *next = node->next;
    if (node->cleanup_pending && !mount->writer.stopped) {
      node->retired_next = NULL;
      fat32_cleanup_nodes(node);
    }
    node = next;
  }
  for (node = mount->nodes; node; node = node->next) {
    if (node->cleanup_pending && status == CALL_OK) {
      status = node->cleanup_status;
    }
  }
  return status;
}

bool fat32_retired_pending(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  return retired_nodes != NULL;
}

static bool same_locator(const struct fat32_locator *a, const struct fat32_locator *b)
{
  return a->parent_cluster == b->parent_cluster &&
      a->parent_parent_cluster == b->parent_parent_cluster &&
      a->short_entry_slot == b->short_entry_slot;
}

static enum call_status wrap(struct fat32_mount *mount, uint64_t kind,
    const struct fat32_locator *locator, uint64_t ceiling, struct kernel_object **out)
{
  if (kind != OBJECT_MOUNT && locator) {
    for (struct fat32_node *node = mount->nodes; node; node = node->next) {
      if (node->kind == kind && (!mount->writable || node->record.attached) &&
          same_locator(mount->writable ? &node->record.locator : &node->locator, locator)) {
        uint64_t flags = cpu_save_interrupts();
        /* A cached wrapper can reach zero on an AP while still awaiting this
         * worker's destruction. Try-retain never resurrects that object. */
        struct kernel_object *object = node_object(node);
        size_t references = atomic_load_explicit(&object->references, memory_order_relaxed);
        bool retained = false;
        while (references && references != SIZE_MAX) {
          if (atomic_compare_exchange_weak_explicit(&object->references, &references,
              references + 1, memory_order_relaxed, memory_order_relaxed)) {
            retained = true;
            break;
          }
        }
        cpu_restore_interrupts(flags);
        if (references == SIZE_MAX) {
          return CALL_LIMIT;
        }
        if (retained) {
          *out = node_object(node);
          return CALL_OK;
        }
        if (mount->writable && node->record.mounted && node->record.attached) {
          /* A zero-reference wrapper is never resurrected. Its attached record
           * can be unbound allocation-free before a new canonical wrapper. */
          enum call_status status = format_status(mount,
              fat32_mounted_object_forget(&node->record, &mount->workspace));
          if (status != CALL_OK) {
            return status;
          }
          free_chain(node);
        }
      }
    }
  }
  struct fat32_node *node;
  enum call_status status = npfs_adapter_allocate(sizeof(*node), (void **)&node);
  if (status != CALL_OK) {
    return status;
  }
  atomic_init(&node->admitted, 0);
  atomic_init(&node->returned, false);
  node->mount = mount;
  node->kind = kind;
  node->ceiling = ceiling;
  if (locator) {
    node->locator = *locator;
  }
  uint64_t flags = cpu_save_interrupts();
  bool identified = kind == OBJECT_MOUNT || fs_metadata_allocate_id(&node->id);
  if (identified) {
    if (kind == OBJECT_DIRECTORY) {
      directory_init_fat32(&node->wrapper.directory, node);
    } else if (kind == OBJECT_FILE) {
      file_init_fat32(&node->wrapper.file, node);
    } else {
      mount_init_fat32(&node->wrapper.control, node);
    }
  }
  cpu_restore_interrupts(flags);
  if (!identified) {
    npfs_adapter_free(node, sizeof(*node));
    return CALL_LIMIT;
  }
  node->generation = node->id;
  if (kind != OBJECT_MOUNT && locator && mount->writable) {
    uint32_t count;
    status = format_status(mount, fat32_mounted_object_chain_size(&mount->writer,
        locator, &mount->workspace, &count));
    if (status == CALL_OK) {
      status = reserve_chain(node, (uint64_t)count + (kind == OBJECT_DIRECTORY ? 2 : 0));
    }
    if (status == CALL_OK) {
      status = format_status(mount, fat32_mounted_object_bind(&mount->writer,
          locator, &mount->workspace, &node->record));
    }
    if (status != CALL_OK) {
      free_chain(node);
      npfs_adapter_free(node, sizeof(*node));
      return status;
    }
  }
  node->next = mount->nodes;
  mount->nodes = node;
  if (kind == OBJECT_MOUNT) {
    ++mount->controls;
  } else {
    ++mount->data_nodes;
  }
  *out = node_object(node);
  return CALL_OK;
}

static enum call_status select_partition(struct npfs_store_context *context,
    struct npfs_job *job, struct gpt_partition *partition, struct block_info *info)
{
  static const struct gpt_guid no_guid = {0};
  static const struct gpt_guid esp_type = {.bytes = {
    0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
    0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b,
  }};
  /* Owned scope with a caller-selected device carries reviewed geometry; the
   * boot-origin path is bound by the origin GUIDs and ESP type instead. */
  bool reviewed = job->fat_owned_esp && job->device;
  for (;;) {
    bool pending = false;
    size_t matches = 0;
    block_device_id selected = 0;
    struct gpt_partition found = {0};
    struct gpt_guid disk = {0};
    uint64_t flags = cpu_save_interrupts();
    if (job->device && (!block_installer_inventory_available() ||
        !block_installer_device(job->device))) {
      cpu_restore_interrupts(flags);
      return CALL_UNAVAILABLE;
    }
    pending = !job->device && !block_discovery_finished();
    for (size_t i = 0; i < block_device_count(); ++i) {
      block_device_id id = block_device_at(i);
      if (job->device && job->device != id) {
        continue;
      }
      const struct gpt_snapshot *snapshot = gpt_get_snapshot(id);
      if (!snapshot) {
        pending = true;
        continue;
      }
      if (snapshot->status != GPT_HEALTHY) {
        if (job->device) {
          enum call_status status;
          switch (snapshot->status) {
          case GPT_ABSENT: status = CALL_NOT_FOUND; break;
          case GPT_UNAVAILABLE:
          case GPT_UNSUPPORTED: status = CALL_UNAVAILABLE; break;
          case GPT_NO_MEMORY: status = CALL_NO_MEMORY; break;
          case GPT_TIMED_OUT: status = CALL_TIMED_OUT; break;
          default: status = CALL_IO; break;
          }
          cpu_restore_interrupts(flags);
          return status;
        }
        continue;
      }
      if (job->device && memcmp(job->disk.bytes, no_guid.bytes, 16) &&
          memcmp(snapshot->disk_guid.bytes, job->disk.bytes, 16)) {
        cpu_restore_interrupts(flags);
        return CALL_NOT_FOUND;
      }
      if (!job->device && memcmp(snapshot->disk_guid.bytes, job->origin.disk, 16)) {
        continue;
      }
      for (size_t p = 0; p < snapshot->partition_count; ++p) {
        const struct gpt_partition *entry = &snapshot->partitions[p];
        if ((job->device && entry->entry_number != job->partition) ||
            (!job->device && (memcmp(entry->guid.bytes, job->origin.partition, 16) ||
             memcmp(entry->type.bytes, esp_type.bytes, sizeof(esp_type.bytes))))) {
          continue;
        }
        if (reviewed) {
          const struct disk_partition_selection *selection = &job->disk_selection;
          if (memcmp(entry->type.bytes, esp_type.bytes, 16) ||
              memcmp(entry->guid.bytes, selection->partition_guid, 16) ||
              memcmp(entry->type.bytes, selection->partition_type, 16) ||
              entry->first_block != selection->first_block ||
              entry->block_count != selection->block_count ||
              entry->attributes != selection->attributes ||
              snapshot->block_size != selection->device_block_size ||
              snapshot->disk_blocks != selection->device_block_count) {
            cpu_restore_interrupts(flags);
            return CALL_NOT_FOUND;
          }
        }
        found = *entry;
        disk = snapshot->disk_guid;
        selected = id;
        ++matches;
      }
    }
    enum block_result result = matches == 1 ? block_get_info(selected, info) : BLOCK_UNAVAILABLE;
    cpu_restore_interrupts(flags);
    if (pending) {
      if (task_deadline_expired(context->deadline)) {
        return CALL_TIMED_OUT;
      }
      kernel_task_sleep_until(task_deadline_after_ms(1));
      continue;
    }
    if (matches != 1) {
      return matches ? CALL_IO : CALL_NOT_FOUND;
    }
    if (result != BLOCK_OK) {
      return block_status(result);
    }
    if ((info->block_size != FAT32_SECTOR_MIN_BYTES &&
        info->block_size != FAT32_SECTOR_MAX_BYTES) || info->max_transfer < info->block_size) {
      return CALL_UNAVAILABLE;
    }
    if (info->write_failed) {
      return CALL_IO;
    }
    if (reviewed &&
        (info->block_size != job->disk_selection.device_block_size ||
         info->block_count != job->disk_selection.device_block_count)) {
      return CALL_NOT_FOUND;
    }
    job->device = selected;
    job->disk = disk;
    job->partition = found.entry_number;
    *partition = found;
    return CALL_OK;
  }
}

static enum call_status allocate_workspace(struct fat32_mount *mount, size_t bytes)
{
  if (!bytes) {
    return CALL_LIMIT;
  }
  enum call_status status = charge_backing(mount, bytes);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  void *memory = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!memory) {
    release_backing(mount, bytes);
    return CALL_NO_MEMORY;
  }
  mount->workspace = (struct fat32_workspace){memory, bytes,
      FAT32_STEP_LIMIT + (uint64_t)mount->volume.cluster_count,
      FAT32_SECTOR_LIMIT, FAT32_DEPTH_LIMIT};
  return CALL_OK;
}

static enum call_status acquire_mount(struct npfs_store_context *context,
    struct npfs_job *job, struct fat32_mount **out)
{
  struct gpt_partition partition;
  struct block_info info;
  enum call_status status = select_partition(context, job, &partition, &info);
  if (status != CALL_OK) {
    return status;
  }
  for (struct fat32_mount *mount = mounts; mount; mount = mount->next) {
    if (!mount->retired && mount->extent.device == job->device &&
        mount->extent.first_block == partition.first_block &&
        mount->extent.block_count == partition.block_count) {
      if (mount->owned_esp != job->fat_owned_esp) {
        return CALL_BUSY;
      }
      if (mount->volume.gpt_entry != job->partition ||
          memcmp(mount->volume.partition_guid, partition.guid.bytes, 16)) {
        return CALL_BUSY;
      }
      if (mount->writable && !job->fat_write_authority) {
        return CALL_DENIED;
      }
      if (job->fat_access == MOUNT_ACCESS_READ_WRITE && !mount->writable) {
        return CALL_BUSY;
      }
      mount->context = context;
      mount->backing_status = CALL_OK;
      *out = mount;
      return CALL_OK;
    }
  }
  if (job->fat_access == MOUNT_ACCESS_READ_WRITE &&
      (!info.writable || !info.flush_supported)) {
    return CALL_READ_ONLY;
  }
  if (instance_count == FAT32_MOUNT_LIMIT) {
    return CALL_LIMIT;
  }
  struct fat32_mount *mount;
  status = npfs_adapter_allocate(sizeof(*mount), (void **)&mount);
  if (status != CALL_OK) {
    return status;
  }
  atomic_init(&mount->proofs_invalidated, false);
  status = storage_extent_reserve(&mount->extent, job->device, partition.first_block,
      partition.block_count, STORAGE_EXTENT_PENDING_FAT);
  if (status != CALL_OK) {
    npfs_adapter_free(mount, sizeof(*mount));
    return status;
  }
  ++instance_count;
  mount->device = job->device;
  mount->owned_esp = job->fat_owned_esp;
  mount->sector_bytes = info.block_size;
  uint32_t write_bytes = info.max_transfer < FAT32_WRITE_MAX_BYTES ?
      info.max_transfer : FAT32_WRITE_MAX_BYTES;
  mount->max_write_sectors = write_bytes / mount->sector_bytes;
  mount->writable = job->fat_access == MOUNT_ACCESS_READ_WRITE;
  mount->next = mounts;
  mounts = mount;
  mount->context = context;
  mount->probing = true;
  mount->backing_budget = fat32_open_workspace_size();
  status = allocate_workspace(mount, fat32_open_workspace_size());
  if (status == CALL_OK) {
    struct fat32_source source = {read_sectors, mount, info.block_count,
        info.block_size, info.max_transfer / info.block_size};
    if (source.max_read_sectors > FAT32_READ_MAX_BYTES / mount->sector_bytes) {
      source.max_read_sectors = FAT32_READ_MAX_BYTES / mount->sector_bytes;
    }
    status = format_status(mount, fat32_open_mounted_view(&source, job->partition,
        &mount->mirrors, &mount->workspace, &mount->volume));
  }
  mount->probing = false;
  if (status == CALL_OK && (mount->volume.partition_first != partition.first_block ||
      mount->volume.partition_sectors != partition.block_count ||
      memcmp(mount->volume.partition_guid, partition.guid.bytes, 16) ||
      memcmp(mount->volume.disk_guid, job->disk.bytes, 16) ||
      memcmp(mount->volume.partition_type, partition.type.bytes, 16) ||
      mount->volume.partition_attributes != partition.attributes)) {
    status = CALL_IO;
  }
  size_t needed = 0;
  if (status == CALL_OK) {
    status = configure_budget(mount);
  }
  if (status == CALL_OK) {
    status = format_status(mount, fat32_workspace_size(&mount->volume, FAT32_DEPTH_LIMIT, &needed));
  }
  if (status == CALL_OK) {
    free_workspace(mount);
    status = allocate_workspace(mount, needed);
  }
  if (status == CALL_OK) {
    status = allocate_mirror(mount);
  }
  if (status == CALL_OK && !mount->writable) {
    struct fat32_check_report report;
    status = format_status(mount, fat32_check(&mount->volume, &mount->workspace, &report));
    if (status == CALL_OK) {
      mount->read_only_free_clusters = report.free_clusters;
      mount->read_only_capacity_valid = true;
    }
  }
  if (status == CALL_OK && mount->writable) {
    size_t bytes;
    status = format_status(mount, fat32_mounted_workspace_size(&mount->volume,
        0, 0, 0, mount->max_write_sectors, &bytes));
    if (status == CALL_OK) {
      status = ensure_workspace(mount, bytes);
    }
    if (status == CALL_OK) {
      status = allocate_map(mount, &mount->allocation, &mount->allocation_bytes);
    }
    if (status == CALL_OK) {
      status = allocate_batch(mount);
    }
    if (status == CALL_OK) {
      struct fat32_write_io io = {.write = write_sectors, .flush = flush_backing, .context = mount,
        .max_write_sectors = mount->max_write_sectors};
      status = format_status(mount, fat32_mounted_open(&mount->writer,
          &mount->volume, &io, mount->allocation, mount->allocation_bytes,
          mount->batch_memory, mount->batch_bytes, FAT32_BATCH_IMAGE_BYTES,
          &mount->workspace));
    }
  }
  if (status == CALL_OK) {
    uint64_t flags = cpu_save_interrupts();
    bool identified = fs_metadata_allocate_id(&mount->domain);
    cpu_restore_interrupts(flags);
    status = identified ? CALL_OK : CALL_LIMIT;
  }
  if (status != CALL_OK) {
    mount->context = NULL;
    remove_instance(mount);
    return status;
  }
  report_mirror(mount);
  storage_extent_publish(&mount->extent, STORAGE_EXTENT_FAT);
  *out = mount;
  return CALL_OK;
}

static enum call_status root(struct fat32_mount *mount, uint64_t rights,
    struct kernel_object **out)
{
  struct fat32_entry entry;
  struct fat32_locator locator;
  enum call_status status = format_status(mount,
      mount->writable ? fat32_mounted_root(&mount->writer,
          &mount->workspace, &entry, &locator) :
          fat32_root(&mount->volume, &mount->workspace, &entry, &locator));
  if (status == CALL_OK) {
    status = wrap(mount, OBJECT_DIRECTORY, &locator, rights, out);
    if (status == CALL_OK) {
      ((struct directory_object *)*out)->fat32->scope = FAT_SCOPE_ROOT;
    }
  }
  return status;
}

struct byte_output { uint8_t *bytes; size_t used, capacity; };
static bool collect_bytes(void *argument, const void *bytes, size_t length)
{
  struct byte_output *output = argument;
  if (length > output->capacity - output->used) {
    return false;
  }
  memcpy(output->bytes + output->used, bytes, length);
  output->used += length;
  return true;
}

static struct fat32_node *object_node(struct kernel_object *object)
{
  return object->type == OBJECT_DIRECTORY ? ((struct directory_object *)object)->fat32 :
      ((struct file_object *)object)->fat32;
}

static void release_object(struct kernel_object *object)
{
  if (object) {
    uint64_t flags = cpu_save_interrupts();
    object_release(object);
    cpu_restore_interrupts(flags);
  }
}

static bool scope_name(const char *name, size_t length, const char *expected)
{
  size_t i = 0;
  for (; i < length && expected[i]; ++i) {
    unsigned char byte = (unsigned char)name[i];
    if (byte >= 'A' && byte <= 'Z') {
      byte += 'a' - 'A';
    }
    if (byte != (unsigned char)expected[i]) {
      return false;
    }
  }
  return i == length && !expected[i];
}

static enum fat32_scope child_scope(struct fat32_node *parent,
    const char *name, size_t length, bool directory)
{
  switch (parent->scope) {
  case FAT_SCOPE_OWNED:
    return FAT_SCOPE_OWNED;
  case FAT_SCOPE_ROOT:
    if (directory && scope_name(name, length, "efi")) {
      return FAT_SCOPE_EFI;
    }
    return directory && scope_name(name, length, "boot") ? FAT_SCOPE_BOOT : FAT_SCOPE_FOREIGN;
  case FAT_SCOPE_EFI:
    return directory && scope_name(name, length, "pyxis") ? FAT_SCOPE_OWNED : FAT_SCOPE_FOREIGN;
  case FAT_SCOPE_BOOT:
    return (directory && scope_name(name, length, "limine")) ||
        (!directory && scope_name(name, length, "limine.conf")) ?
        FAT_SCOPE_OWNED : FAT_SCOPE_FOREIGN;
  default:
    return FAT_SCOPE_FOREIGN;
  }
}

static enum call_status wrap_child(struct fat32_node *parent,
    const struct fat32_entry *entry, const struct fat32_locator *locator,
    uint64_t rights, struct kernel_object **out)
{
  bool directory = (entry->attributes & FAT32_ATTR_DIRECTORY) != 0;
  /* Use the canonical entry name, never the caller's spelling or short alias. */
  enum fat32_scope scope = child_scope(parent, entry->name, entry->name_bytes, directory);
  if (parent->mount->owned_esp && scope == FAT_SCOPE_FOREIGN &&
      (rights & ~(directory ? FAT32_ROOT_RIGHTS : FILE_RIGHT_READ))) {
    return CALL_DENIED;
  }
  enum call_status status = wrap(parent->mount,
      directory ? OBJECT_DIRECTORY : OBJECT_FILE, locator, rights, out);
  if (status == CALL_OK) {
    object_node(*out)->scope = scope;
  }
  return status;
}

static enum call_status lookup_object(struct fat32_node *parent, const char *name,
    size_t length, struct kernel_object **out)
{
  struct fat32_mount *mount = parent->mount;
  struct fat32_entry entry;
  struct fat32_locator locator;
  enum call_status status = format_status(mount, fat32_mounted_lookup(&parent->record,
      name, length, &mount->workspace, &entry, &locator));
  return status == CALL_OK ? wrap_child(parent, &entry, &locator, 0, out) : status;
}

static bool new_generation(uint64_t *generation)
{
  uint64_t flags = cpu_save_interrupts();
  bool identified = fs_metadata_allocate_id(generation);
  cpu_restore_interrupts(flags);
  return identified;
}

static enum call_status mutation_operation(struct npfs_job *job, struct fat32_mount *mount)
{
  struct fat32_node *node = job->fat_node;
  bool directory = node->kind == OBJECT_DIRECTORY;
  bool write = job->operation == NPFS_WRITE || job->operation == NPFS_APPEND;
  uint64_t required;
  switch (job->operation) {
  case NPFS_CREATE: required = DIRECTORY_RIGHT_CREATE; break;
  case NPFS_REMOVE:
  case NPFS_RENAME: required = DIRECTORY_RIGHT_REMOVE; break;
  case NPFS_WRITE:
  case NPFS_APPEND:
  case NPFS_RESIZE: required = FILE_RIGHT_WRITE; break;
  case NPFS_SYNC: required = directory ? DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_REMOVE :
      FILE_RIGHT_WRITE; break;
  default: return CALL_BAD_OPERATION;
  }
  if (!(job->rights & required)) {
    return CALL_DENIED;
  }
  if (((write || job->operation == NPFS_RESIZE) && directory) ||
      ((job->operation == NPFS_CREATE || job->operation == NPFS_REMOVE ||
        job->operation == NPFS_RENAME) && !directory)) {
    return CALL_WRONG_TYPE;
  }
  if (mount->owned_esp) {
    /* FILE_TRANSFER writes arrive as NPFS_WRITE through this same gate. */
    if ((write || job->operation == NPFS_RESIZE) &&
        node->scope != FAT_SCOPE_OWNED) {
      return CALL_DENIED;
    }
    if (job->operation == NPFS_CREATE) {
      enum fat32_scope scope = child_scope(node, job->name, job->count,
          job->kind == DIRECTORY_KIND_DIRECTORY);
      if (scope == FAT_SCOPE_FOREIGN) {
        return CALL_DENIED;
      }
    }
  }
  enum call_status status = mutation_admission(mount, job);
  if (status != CALL_OK) {
    return status;
  }
  if (job->operation == NPFS_SYNC) {
    return format_status(mount, fat32_mounted_sync(&mount->writer));
  }
  uint64_t cluster_bytes = (uint64_t)mount->writer.volume.sectors_per_cluster *
      mount->writer.volume.source.sector_bytes;
  if (write || job->operation == NPFS_RESIZE) {
    if (job->operation == NPFS_APPEND &&
        (job->count > FILE_APPEND_MAX_BYTES || job->transfer_data)) {
      return CALL_BAD_REQUEST;
    }
    if (write && (job->count > FILE_TRANSFER_MAX_BYTES ||
        (job->transfer_data ? job->count > job->transfer_capacity :
         job->count > FILE_WRITE_MAX_BYTES))) {
      return CALL_BAD_REQUEST;
    }
    if (job->operation == NPFS_APPEND) {
      job->offset = node->record.entry.file_bytes;
      if (!job->count) {
        return CALL_OK;
      }
    }
    uint64_t target = job->offset;
    if (write) {
      if (job->count && (target > UINT32_MAX || job->count > UINT32_MAX - target)) {
        return CALL_FILE_TOO_LARGE;
      }
      target = job->count ? target + job->count : node->record.entry.file_bytes;
      if (target < node->record.entry.file_bytes) {
        target = node->record.entry.file_bytes;
      }
    } else if (target > UINT32_MAX) {
      return CALL_FILE_TOO_LARGE;
    }
    uint64_t changed = (target > node->record.entry.file_bytes ?
        target - node->record.entry.file_bytes : 0) + 2 * cluster_bytes + job->count;
    uint64_t total = (target + cluster_bytes - 1) / cluster_bytes;
    uint64_t old = node->record.chain_count;
    uint64_t added = total > old ? total - old : 0;
    uint64_t removed = old > total ? old - total : 0;
    status = prepare_workspace(mount, changed, added, added + removed + 1);
    if (status == CALL_OK) {
      status = reserve_chain(node, (target + cluster_bytes - 1) / cluster_bytes);
    }
    if (status != CALL_OK) {
      return status;
    }
    status = format_status(mount, write ?
        fat32_mounted_write(&node->record, job->offset,
            job->transfer_data ? job->transfer_data : job->data, job->count,
            &mount->workspace) : fat32_mounted_resize(&node->record, job->offset,
            &mount->workspace));
    if (job->operation == NPFS_APPEND) {
      job->progress = status == CALL_OK ? job->count : 0;
      job->count = job->progress;
    }
    return status;
  }
  uint64_t allocated = job->operation == NPFS_CREATE ? 3 :
      job->operation == NPFS_RENAME ? 2 : 0;
  status = prepare_workspace(mount, 0, allocated, allocated ? allocated + 1 : 0);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t generation;
  if (!new_generation(&generation)) {
    return CALL_LIMIT;
  }
  if (job->operation == NPFS_CREATE) {
    if (!job->reservation.table ||
        (job->kind != DIRECTORY_KIND_FILE && job->kind != DIRECTORY_KIND_DIRECTORY)) {
      return CALL_BAD_REQUEST;
    }
    uint64_t allowed = job->kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
        (job->rights & DIRECTORY_RIGHT_READ_FILES ? FILE_RIGHT_READ : 0) |
        (job->rights & DIRECTORY_RIGHT_WRITE_FILES ? FILE_RIGHT_WRITE : 0);
    if (job->child_rights & ~allowed) {
      return CALL_DENIED;
    }
    struct kernel_object *object = NULL;
    status = wrap(mount, job->kind == DIRECTORY_KIND_DIRECTORY ? OBJECT_DIRECTORY :
        OBJECT_FILE, NULL, job->child_rights, &object);
    struct fat32_node *created = object ? object_node(object) : NULL;
    if (status == CALL_OK) {
      status = reserve_chain(node, (uint64_t)node->record.chain_count + 2);
    }
    if (status == CALL_OK && job->kind == DIRECTORY_KIND_DIRECTORY) {
      status = reserve_chain(created, 1);
    }
    if (status == CALL_OK) {
      uint64_t flags = cpu_save_interrupts();
      enum capability_result prepared = capability_grant_retain(object,
          job->child_rights, 0, &job->grant);
      if (prepared == CAP_OK) {
        prepared = capability_validate_grants(job->reservation.table, &job->grant, 1);
      }
      cpu_restore_interrupts(flags);
      status = prepared == CAP_OK ? CALL_OK : prepared == CAP_NO_MEMORY ?
          CALL_NO_MEMORY : prepared == CAP_LIMIT ? CALL_LIMIT : CALL_DENIED;
    }
    if (status == CALL_OK) {
      status = format_status(mount, fat32_mounted_create(&node->record, job->name,
          job->count, job->kind == DIRECTORY_KIND_DIRECTORY, &mount->workspace,
          &created->record));
    }
    if (status == CALL_OK) {
      created->scope = child_scope(node, created->record.entry.name,
          created->record.entry.name_bytes, job->kind == DIRECTORY_KIND_DIRECTORY);
      node->generation = generation;
      uint64_t flags = cpu_save_interrupts();
      capability_install_reserved(&job->reservation, &job->slot, &job->grant, 1,
          &job->handle);
      cpu_restore_interrupts(flags);
    }
    release_object(object);
    return status;
  }
  struct kernel_object *source = NULL, *victim = NULL;
  status = lookup_object(node, job->name, job->count, &source);
  if (status != CALL_OK) {
    return status;
  }
  struct fat32_node *selected = object_node(source);
  if (mount->owned_esp && selected->scope != FAT_SCOPE_OWNED) {
    release_object(source);
    return CALL_DENIED;
  }
  if (job->operation == NPFS_REMOVE) {
    if ((job->kind == DIRECTORY_KIND_FILE && selected->kind != OBJECT_FILE) ||
        (job->kind == DIRECTORY_KIND_DIRECTORY && selected->kind != OBJECT_DIRECTORY)) {
      status = CALL_WRONG_TYPE;
    } else {
      status = format_status(mount, fat32_mounted_remove(&node->record,
          &selected->record, &mount->workspace));
    }
    if (status == CALL_OK) {
      node->generation = generation;
    }
  } else {
    struct fat32_node *target = job->fat_destination;
    if (!target || target->kind != OBJECT_DIRECTORY || target->mount != mount) {
      status = CALL_BAD_OPERATION;
    } else if (mount->owned_esp && child_scope(target, job->destination_name,
        job->destination_length, false) != FAT_SCOPE_OWNED) {
      status = CALL_DENIED;
    } else if (!(job->destination_rights & DIRECTORY_RIGHT_CREATE) ||
        (job->replace && !(job->destination_rights & DIRECTORY_RIGHT_REMOVE))) {
      status = CALL_DENIED;
    } else if (job->kind != DIRECTORY_KIND_ANY && job->kind !=
        (selected->kind == OBJECT_DIRECTORY ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE)) {
      status = CALL_WRONG_TYPE;
    } else if (selected->kind == OBJECT_DIRECTORY) {
      status = CALL_BAD_OPERATION;
    } else if (selected->kind != OBJECT_FILE) {
      status = CALL_WRONG_TYPE;
    } else {
      uint64_t target_generation = generation;
      if (target != node && !new_generation(&target_generation)) {
        status = CALL_LIMIT;
      } else {
        status = lookup_object(target, job->destination_name, job->destination_length, &victim);
        if (status == CALL_NOT_FOUND) {
          status = CALL_OK;
        }
        if (status == CALL_OK && mount->owned_esp && victim &&
            object_node(victim)->scope != FAT_SCOPE_OWNED) {
          status = CALL_DENIED;
        }
        if (status == CALL_OK) {
          status = reserve_chain(target, (uint64_t)target->record.chain_count + 2);
        }
        if (status == CALL_OK) {
          status = format_status(mount, fat32_mounted_rename(&node->record,
              &selected->record, &target->record, job->destination_name,
              job->destination_length, job->replace, victim ? &object_node(victim)->record :
              NULL, &mount->workspace));
        }
        if (status == CALL_OK && source != victim) {
          node->generation = generation;
          target->generation = target_generation;
        }
      }
    }
  }
  release_object(victim);
  release_object(source);
  return status;
}

static enum call_status data_operation(struct npfs_job *job, struct fat32_mount *mount)
{
  struct fat32_node *node = job->fat_node;
  bool directory = node->kind == OBJECT_DIRECTORY;
  uint64_t all = directory ? (mount->writable ? DIRECTORY_CONTENT_RIGHTS : FAT32_ROOT_RIGHTS) :
      (mount->writable ? FILE_RIGHTS : FILE_RIGHT_READ);
  if (job->rights & ~all) {
    return CALL_DENIED;
  }
  if (mount->writable && mount->writer.stopped) {
    return CALL_OUTCOME_UNKNOWN;
  }
  switch (job->operation) {
  case NPFS_CREATE:
  case NPFS_REMOVE:
  case NPFS_RENAME:
  case NPFS_WRITE:
  case NPFS_APPEND:
  case NPFS_RESIZE:
  case NPFS_SYNC:
    return mutation_operation(job, mount);
  default: break;
  }
  struct fat32_entry entry;
  enum call_status status;
  switch (job->operation) {
  case NPFS_LOOKUP: {
    if (!directory || !(job->rights & DIRECTORY_RIGHT_LOOKUP)) {
      return CALL_DENIED;
    }
    bool typed = job->kind == DIRECTORY_KIND_ANY;
    uint64_t allowed = job->kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
        (job->rights & DIRECTORY_RIGHT_READ_FILES ? FILE_RIGHT_READ : 0) |
        (job->rights & DIRECTORY_RIGHT_WRITE_FILES ? FILE_RIGHT_WRITE : 0);
    if (typed && ((job->child_rights & ~FILE_RIGHTS) ||
        (job->directory_child_rights & ~DIRECTORY_RIGHTS))) {
      return CALL_BAD_REQUEST;
    }
    if (!typed && ((job->kind != DIRECTORY_KIND_FILE && job->kind != DIRECTORY_KIND_DIRECTORY) ||
        (job->child_rights & ~allowed))) {
      return CALL_DENIED;
    }
    struct fat32_locator child;
    status = format_status(mount, mount->writable ? fat32_mounted_lookup(&node->record, job->name, job->count,
        &mount->workspace, &entry, &child) : fat32_lookup(&mount->volume, &node->locator,
        job->name, job->count, &mount->workspace, &entry, &child));
    if (status != CALL_OK) {
      return status;
    }
    uint64_t kind = entry.attributes & FAT32_ATTR_DIRECTORY ? OBJECT_DIRECTORY : OBJECT_FILE;
    if (!typed && ((kind == OBJECT_DIRECTORY) != (job->kind == DIRECTORY_KIND_DIRECTORY))) {
      return CALL_WRONG_TYPE;
    }
    uint64_t child_rights = typed && kind == OBJECT_DIRECTORY ?
        job->directory_child_rights : job->child_rights;
    allowed = kind == OBJECT_DIRECTORY ? job->rights :
        ((job->rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0) |
        ((job->rights & DIRECTORY_RIGHT_WRITE_FILES) ? FILE_RIGHT_WRITE : 0);
    if (child_rights & ~allowed) {
      return CALL_DENIED;
    }
    return wrap_child(node, &entry, &child, child_rights, &job->object);
  }
  case NPFS_ENUMERATE: {
    if (!directory || !(job->rights & DIRECTORY_RIGHT_ENUMERATE)) {
      return CALL_DENIED;
    }
    if (!job->cursor.generation && job->cursor.position) {
      return CALL_BAD_REQUEST;
    }
    struct directory_enumerate_reply reply = {.cursor = job->cursor};
    if (job->cursor.generation && job->cursor.generation != node->generation) {
      reply.outcome = DIRECTORY_CHANGED;
      job->entry = reply;
      return CALL_OK;
    }
    uint64_t next;
    bool found;
    struct fat32_locator child;
    status = format_status(mount, mount->writable ? fat32_mounted_enumerate(&node->record, job->cursor.position,
        &mount->workspace, &entry, &child, &next, &found) : fat32_enumerate(&mount->volume,
        &node->locator, job->cursor.position, &mount->workspace, &entry, &child, &next, &found));
    if (status != CALL_OK) {
      return status;
    }
    if (!found) {
      reply.outcome = DIRECTORY_END;
      reply.cursor = (struct directory_cursor){node->generation, next};
    } else {
      reply.name_size = entry.name_bytes + 1;
      reply.kind = entry.attributes & FAT32_ATTR_DIRECTORY ? DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
      if (job->count < reply.name_size) {
        reply.outcome = DIRECTORY_BUFFER_TOO_SMALL;
      } else {
        reply.outcome = DIRECTORY_ENTRY;
        reply.cursor = (struct directory_cursor){node->generation, next};
        memcpy(job->name, entry.name, entry.name_bytes);
        job->name[entry.name_bytes] = 0;
      }
    }
    job->entry = reply;
    return CALL_OK;
  }
  case NPFS_READ: {
    if (directory || !(job->rights & FILE_RIGHT_READ) || job->count > FILE_TRANSFER_MAX_BYTES ||
        (job->transfer_data ? job->count > job->transfer_capacity :
         job->count > sizeof(job->data))) {
      return CALL_BAD_REQUEST;
    }
    if (job->transfer_data && mount->writable) {
      size_t read = 0;
      status = format_status(mount, fat32_mounted_read_into(&node->record, job->offset,
          job->count, &mount->workspace, job->transfer_data, &read));
      if (status == CALL_OK) {
        job->count = read;
      }
      return status;
    }
    struct byte_output output = {job->transfer_data ? job->transfer_data : job->data,
      0, job->count};
    status = format_status(mount, mount->writable ? fat32_mounted_read_range(&node->record, job->offset, job->count,
        &mount->workspace, collect_bytes, &output) : fat32_read_range(&mount->volume,
        &node->locator, job->offset, job->count, &mount->workspace, collect_bytes, &output));
    if (status == CALL_OK) {
      job->count = output.used;
    }
    return status;
  }
  case NPFS_CAPTURE: {
    if (directory || !(job->rights & FILE_RIGHT_READ)) {
      return CALL_DENIED;
    }
    status = format_status(mount, mount->writable ? fat32_mounted_locate(&node->record, &mount->workspace, &entry) :
        fat32_locate(&mount->volume, &node->locator, &mount->workspace, &entry));
    if (status != CALL_OK) {
      return status;
    }
    status = image_capture_allocate_wait(entry.file_bytes, &job->captured);
    if (status != CALL_OK) {
      return status;
    }
    struct byte_output output = {(void *)job->captured.address, 0, job->captured.size};
    status = format_status(mount, mount->writable ?
        fat32_mounted_read_range(&node->record, 0, job->captured.size,
            &mount->workspace, collect_bytes, &output) :
        fat32_read_range(&mount->volume, &node->locator, 0, job->captured.size,
            &mount->workspace, collect_bytes, &output));
    return status == CALL_OK && output.used != job->captured.size ? CALL_IO : status;
  }
  case NPFS_SIZE:
  case NPFS_INFO:
    status = format_status(mount, mount->writable ? fat32_mounted_locate(&node->record, &mount->workspace, &entry) :
        fat32_locate(&mount->volume, &node->locator, &mount->workspace, &entry));
    if (status == CALL_OK) {
      job->offset = directory ? 0 : entry.file_bytes;
      job->file_info = (struct file_info_reply){
        .valid = FILE_INFO_DOMAIN_VALID | FILE_INFO_OBJECT_VALID |
            (directory ? 0 : FILE_INFO_SIZE_VALID),
        .domain = mount->domain, .object = node->id, .size = job->offset,
      };
    }
    return status;
  default: return CALL_BAD_OPERATION;
  }
}

static void state(struct fat32_mount *mount, struct mount_state_reply *reply)
{
  const struct fat32_volume *admitted = &mount->volume;
  const struct fat32_volume *volume = mount->writable ? &mount->writer.volume : &mount->volume;
  *reply = (struct mount_state_reply){
    .domain = mount->domain, .device = mount->device,
    .partition = volume->gpt_entry, .first_block = volume->partition_first,
    .block_count = volume->partition_sectors,
    .flags = MOUNT_STATE_IDENTITY_VALID |
        (mount->retired ? MOUNT_STATE_RETIRED : 0) |
        (volume->clean_shutdown ? MOUNT_STATE_CLEAN_SHUTDOWN : 0) |
        (volume->no_hard_error ? MOUNT_STATE_NO_HARD_ERROR : 0) |
        (mount->writable ? MOUNT_STATE_READ_WRITE : 0) |
        (mount->writer.dirty_owned ? MOUNT_STATE_DIRTY_OWNED : 0) |
        (mount->writer.stopped ? MOUNT_STATE_FAILED : 0) |
        (mount->writable && !mount->writer.allocation_valid ? MOUNT_STATE_ALLOCATION_INVALID : 0),
    .partition_attributes = admitted->partition_attributes,
    .device_block_size = admitted->source.sector_bytes,
    .device_block_count = admitted->source.sectors,
  };
  memcpy(reply->disk_guid, admitted->disk_guid, sizeof(reply->disk_guid));
  memcpy(reply->partition_guid, admitted->partition_guid, sizeof(reply->partition_guid));
  memcpy(reply->partition_type, admitted->partition_type, sizeof(reply->partition_type));
  if (!mount->retired && !mount->writer.stopped &&
      (mount->writable ? mount->writer.allocation_valid : mount->read_only_capacity_valid)) {
    reply->flags |= MOUNT_STATE_CAPACITY_VALID;
    reply->cluster_bytes = (uint64_t)volume->source.sector_bytes * volume->sectors_per_cluster;
    reply->total_clusters = volume->cluster_count;
    reply->free_clusters = mount->writable ? mount->writer.free_count : mount->read_only_free_clusters;
  }
  if (mount->writer.stopped) {
    reply->failure_status = mount->first_failure;
    reply->failure_phase = mount->writer.recovery.phase;
    reply->failed_sector = mount->writer.recovery.failed_sector;
    memcpy(reply->source_name, mount->writer.recovery.source_name, sizeof(reply->source_name));
    memcpy(reply->destination_name, mount->writer.recovery.target_name,
        sizeof(reply->destination_name));
  }
  for (struct fat32_node *node = mount->nodes; node; node = node->next) {
    if (node->cleanup_pending) {
      reply->flags |= MOUNT_STATE_CLEANUP_PENDING;
      if (!mount->writer.stopped && reply->failure_status == CALL_OK) {
        reply->failure_status = node->cleanup_status;
      }
    }
  }
  for (struct fat32_mounted_object *object = mount->writer.objects; object; object = object->next) {
    if (!object->attached) {
      ++reply->detached_objects;
    }
  }
  for (struct fat32_interval *interval = mount->intervals; interval; interval = interval->next) {
    if (interval->active) {
      reply->flags |= MOUNT_STATE_INTERVAL;
      reply->interval_id = interval->id;
      reply->interval_deadline_ns = interval->deadline;
      break;
    }
  }
}

enum call_status fat32_maintain(struct npfs_store_context *context,
    bool flush, bool pressure)
{
  npfs_require_worker();
  if (!flush && !pressure) {
    return CALL_OK;
  }
  enum call_status result = CALL_OK;
  for (struct fat32_mount *mount = mounts; mount; mount = mount->next) {
    if (mount->retired || !mount->writable || !fat32_mounted_pending(&mount->writer)) {
      continue;
    }
    struct npfs_store_context flush_context = *context;
    flush_context.deadline = task_deadline_after_ms(NPFS_TIMEOUT_MS);
    mount->context = &flush_context;
    mount->backing_status = CALL_OK;
    enum call_status status = format_status(mount, fat32_mounted_sync(&mount->writer));
    mount->context = NULL;
    if (status != CALL_OK && status != mount->maintenance_error) {
      kerror("fat32: device %u GPT %u background checkpoint failed (status %u); mount retained\n",
          mount->device, mount->volume.gpt_entry, (unsigned)status);
    }
    mount->maintenance_error = status;
    if (result == CALL_OK) {
      result = status;
    }
  }
  return result;
}

enum call_status fat32_shutdown_seal(struct npfs_store_context *context)
{
  npfs_require_worker();
  for (struct fat32_mount *mount = mounts; mount; mount = mount->next) {
    if (mount->retired || !mount->writable) {
      continue;
    }
    fat32_mounted_invalidate_proofs(&mount->writer);
    if (mount->writer.stopped) {
      return CALL_OUTCOME_UNKNOWN;
    }
    if (!mount->writer.mutated) {
      continue;
    }
    struct npfs_store_context seal_context = *context;
    seal_context.deadline = task_deadline_after_ms(NPFS_TIMEOUT_MS);
    mount->context = &seal_context;
    /* Known preflight cleanup refusals remain explicit pending records. Their
     * detached chains keep dirty set; power never retries or waits for views. */
    enum call_status status = format_status(mount, fat32_mounted_seal(&mount->writer,
        &mount->workspace));
    mount->context = NULL;
    if (status != CALL_OK) {
      return status;
    }
  }
  return CALL_OK;
}

enum call_status fat32_perform(struct npfs_store_context *context, struct npfs_job *job)
{
  npfs_require_worker();
  struct fat32_mount *mount = job->fat_node ? job->fat_node->mount : NULL;
  enum call_status status;
  if (job->operation == NPFS_FAT_OPEN) {
    status = acquire_mount(context, job, &mount);
    if (status != CALL_OK) {
      return status;
    }
    status = root(mount, job->rights, &job->object);
    if (status == CALL_OK) {
      status = wrap(mount, OBJECT_MOUNT, NULL, job->rights, &job->control);
    }
    mount->context = NULL;
    if (status != CALL_OK && mount->writable) {
      fat32_mounted_invalidate_proofs(&mount->writer);
    }
    if (status != CALL_OK && !mount->nodes && !mount->published) {
      remove_instance(mount);
    }
    return status;
  }
  if (!mount) {
    return CALL_BAD_REQUEST;
  }
  if (mount->writable &&
      atomic_exchange_explicit(&mount->proofs_invalidated, false, memory_order_acq_rel)) {
    fat32_mounted_invalidate_proofs(&mount->writer);
  }
  if (job->operation == NPFS_FAT_FINISH_BEGIN) {
    finish_begin(mount, job);
    return CALL_OK;
  }
  mount->context = context;
  mount->backing_status = CALL_OK;
  update_intervals(mount);
  if (mount->writable) {
    switch (job->operation) {
    case NPFS_APPEND:
      if (!job->count) {
        break;
      }
      [[fallthrough]];
    case NPFS_WRITE:
      /* The engine alone consumes a committed append proof. */
      fat32_mounted_invalidate_reads(&mount->writer);
      break;
    case NPFS_RESIZE:
    case NPFS_CREATE:
    case NPFS_REMOVE:
    case NPFS_RENAME:
    case NPFS_SYNC:
    case NPFS_FAT_SYNC:
    case NPFS_FAT_CHECK:
    case NPFS_FAT_RETIRE:
      /* Include adapter refusals before the format engine sees the attempt. */
      fat32_mounted_invalidate_proofs(&mount->writer);
      break;
    default:
      break;
    }
  }
  switch (job->operation) {
  case NPFS_FAT_STATE:
    mount->published = true;
    state(mount, &job->mount_state);
    status = CALL_OK;
    break;
  case NPFS_FAT_RETIRE:
    mount->published = true;
    status = retry_pending_cleanup(mount);
    if (status != CALL_OK && !mount->writer.stopped) {
      break;
    }
    if (mount->retired) {
      status = CALL_OK;
    } else if (mount->writable && mount->writer.stopped) {
      status = CALL_OUTCOME_UNKNOWN;
    } else if (mount->data_nodes || other_requests(mount, job->fat_node) || active_interval(mount)) {
      status = CALL_BUSY;
    } else {
      status = mount->writable ? format_status(mount, fat32_mounted_seal(&mount->writer,
          &mount->workspace)) : CALL_OK;
      if (status != CALL_OK) {
        break;
      }
      while (mount->intervals) {
        struct fat32_interval *interval = mount->intervals;
        mount->intervals = interval->next;
        free_interval(mount, interval);
      }
      free_workspace(mount);
      free_map(mount, mount->allocation, mount->allocation_bytes);
      mount->allocation = NULL;
      mount->allocation_bytes = 0;
      free_map(mount, mount->mirror_map, mount->mirror_bytes);
      mount->mirror_map = NULL;
      mount->mirror_bytes = 0;
      free_batch(mount);
      storage_extent_release(&mount->extent);
      KASSERT(instance_count);
      --instance_count;
      mount->retired = true;
      status = CALL_OK;
    }
    break;
  case NPFS_FAT_BEGIN:
  case NPFS_FAT_END:
    status = interval_operation(mount, job);
    break;
  case NPFS_FAT_SYNC:
    status = mount->retired ? CALL_UNAVAILABLE : mutation_admission(mount, job);
    if (status == CALL_OK) {
      status = format_status(mount, fat32_mounted_sync(&mount->writer));
    }
    break;
  case NPFS_FAT_CHECK:
    status = mount->retired ? CALL_UNAVAILABLE : mutation_admission(mount, job);
    if (status == CALL_OK) {
      bool owned = false;
      for (struct fat32_interval *interval = mount->intervals; interval; interval = interval->next) {
        if (interval->actor == job->fat_actor && interval->active && !interval->pending) {
          owned = true;
          break;
        }
      }
      status = owned ? CALL_OK : CALL_DENIED;
    }
    if (status == CALL_OK) {
      size_t bytes;
      status = format_status(mount, fat32_workspace_size(&mount->writer.volume,
          FAT32_DEPTH_LIMIT, &bytes));
      if (status == CALL_OK) {
        status = ensure_workspace(mount, bytes);
      }
      if (status == CALL_OK) {
        struct fat32_check_report report;
        status = format_status(mount, fat32_mounted_check(&mount->writer,
            mount->allocation, mount->allocation_bytes, &mount->workspace, &report));
      }
    }
    break;
  case NPFS_FAT_ROOT:
    status = (job->rights & ~job->fat_node->ceiling) ? CALL_DENIED :
        mount->retired ? CALL_UNAVAILABLE : mount->writer.stopped ? CALL_OUTCOME_UNKNOWN :
        root(mount, job->rights, &job->object);
    if (status == CALL_OK) {
      mount->published = true;
    }
    break;
  default:
    status = mount->retired ? CALL_UNAVAILABLE : data_operation(job, mount);
    if (status == CALL_OK) {
      mount->published = true;
    }
    break;
  }
  if (status != CALL_OK && mount->writable) {
    fat32_mounted_invalidate_proofs(&mount->writer);
  }
  report_mirror(mount);
  mount->context = NULL;
  return status;
}

static enum call_status capability_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  default: return CALL_DENIED;
  }
}

static struct syscall_result open_pair(block_device_id device, const struct boot_origin *origin,
    const uint8_t expected_disk_guid[16],
    uint32_t partition, uint64_t access, bool write_authority, uint64_t rights,
    uintptr_t reply_address, size_t reply_capacity, struct disk_object *authority,
    bool owned_scope, const struct disk_partition_selection *reviewed)
{
  uint64_t allowed = access == MOUNT_ACCESS_READ_WRITE ? DIRECTORY_CONTENT_RIGHTS : FAT32_ROOT_RIGHTS;
  if (access > MOUNT_ACCESS_READ_WRITE || (rights & ~allowed) ||
      reply_capacity < sizeof(struct mount_fat32_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct mount_fat32_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  enum call_status status = capability_status(capability_request_reservation(2, &reservation, slots));
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct npfs_request *request = npfs_request_prepare(NPFS_FAT_OPEN);
  request->job.device = device;
  request->job.raw = authority;
  request->job.partition = partition;
  request->job.rights = rights;
  request->job.fat_access = access;
  request->job.fat_write_authority = write_authority;
  request->job.fat_owned_esp = owned_scope;
  if (reviewed) {
    request->job.disk_selection = *reviewed;
  }
  if (expected_disk_guid) {
    memcpy(request->job.disk.bytes, expected_disk_guid, 16);
  }
  if (origin) {
    request->job.origin = *origin;
  }
  npfs_request_submit_and_wait(request);
  status = request->job.status;
  struct kernel_object *objects[2] = {request->job.object, request->job.control};
  request->job.object = NULL;
  request->job.control = NULL;
  npfs_request_release(request);
  struct capability_grant grants[2] = {0};
  struct mount_fat32_reply reply = {0};
  if (status == CALL_OK) {
    KASSERT(objects[0] && objects[1]);
    if (task_stop_requested()) {
      status = CALL_ENDPOINT_CLOSED;
    } else {
      status = capability_status(capability_grant_retain(objects[0], rights, 0, &grants[0]));
      if (status == CALL_OK) {
        status = capability_status(capability_grant_retain(objects[1],
            MOUNT_RIGHT_OPEN_ROOT | MOUNT_RIGHT_OBSERVE | MOUNT_RIGHT_RETIRE |
            (access == MOUNT_ACCESS_READ_WRITE ? MOUNT_RIGHT_WRITE | MOUNT_RIGHT_INTERVAL : 0),
            0, &grants[1]));
      }
      if (status == CALL_OK) {
        status = capability_status(capability_validate_grants(reservation.table, grants, 2));
      }
      if (status == CALL_OK) {
        handle_t handles[2];
        capability_install_reserved(&reservation, slots, grants, 2, handles);
        reply = (struct mount_fat32_reply){handles[0], handles[1]};
      }
    }
  }
  for (size_t i = 0; i < 2; ++i) {
    capability_grant_release(&grants[i]);
  }
  capability_reservation_release(&reservation, slots);
  if (status == CALL_OK) {
    handle_t handles[] = {reply.root, reply.control};
    if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), handles, 2)) {
      status = CALL_BAD_BUFFER;
    } else {
      /* A successful return makes final control close ordinary retention. Failed
       * unpublished opens instead drain their wrappers before provisional abort. */
      fat32_mark_returned(((struct mount_object *)objects[1])->fat32);
    }
  }
  if (status != CALL_OK) {
    fat32_note_call_failure(objects[0]);
    fat32_note_call_failure(objects[1]);
  }
  for (size_t i = 0; i < 2; ++i) {
    if (objects[i]) {
      object_release(objects[i]);
    }
  }
  return (struct syscall_result){status, status == CALL_OK ? sizeof(reply) : 0};
}

struct syscall_result fat32_open_device(struct disk_object *disk, uint64_t disk_rights,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  struct disk_fat32_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!request.partition || request.partition > UINT32_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (request.access == MOUNT_ACCESS_READ_WRITE && !(disk_rights & DISK_RIGHT_WRITE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  return open_pair(disk->device, NULL, NULL, request.partition, request.access,
      (disk_rights & DISK_RIGHT_WRITE) != 0, request.rights, reply_address, reply_capacity, disk,
      false, NULL);
}

struct syscall_result fat32_open_selected(uint64_t inventory_rights,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(inventory_rights & (DISKS_RIGHT_MOUNT_READ_ONLY | DISKS_RIGHT_MOUNT_READ_WRITE))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct disks_fat32_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!request.id || request.id > UINT32_MAX || !request.partition ||
      request.partition > UINT32_MAX || request.access > MOUNT_ACCESS_READ_WRITE) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  bool write_authority = (inventory_rights & DISKS_RIGHT_MOUNT_READ_WRITE) != 0;
  if (request.access == MOUNT_ACCESS_READ_WRITE && !write_authority) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  return open_pair((block_device_id)request.id, NULL, request.expected_disk_guid,
      (uint32_t)request.partition, request.access, write_authority, request.rights,
      reply_address, reply_capacity, NULL, false, NULL);
}

struct syscall_result fat32_open_owned_esp(uint64_t inventory_rights,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(inventory_rights & DISKS_RIGHT_OPEN_OWNED_ESP)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct disks_owned_esp_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  const struct disk_partition_selection *selection = &request.selection;
  static const uint8_t no_guid[16] = {0};
  if (!selection->id || selection->id > UINT32_MAX || !selection->partition ||
      selection->partition > UINT32_MAX || !selection->device_block_size ||
      !selection->device_block_count ||
      selection->device_block_count > UINT64_MAX / selection->device_block_size ||
      !selection->first_block || selection->first_block >= selection->device_block_count ||
      !selection->block_count ||
      selection->block_count > selection->device_block_count - selection->first_block ||
      !memcmp(selection->disk_guid, no_guid, sizeof(no_guid))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  return open_pair((block_device_id)selection->id, NULL, selection->disk_guid,
      (uint32_t)selection->partition, request.access, true, request.rights,
      reply_address, reply_capacity, NULL, true, selection);
}

static struct syscall_result boot_esp_open(struct mount_object *mount, uint64_t rights,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & MOUNT_RIGHT_OPEN_ROOT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct mount_fat32_root_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return open_pair(0, &mount->origin, NULL, 0, MOUNT_ACCESS_READ_ONLY, false,
      request.rights, reply_address, reply_capacity, NULL, false, NULL);
}

static struct syscall_result boot_esp_observe(struct mount_object *mount, uint64_t rights,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & MOUNT_RIGHT_OBSERVE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct mount_boot_reply reply = {0};
  if (request_size || reply_capacity < sizeof(reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  memcpy(reply.disk_guid, mount->origin.disk, 16);
  memcpy(reply.partition_guid, mount->origin.partition, 16);
  if (mount->config.enabled) {
    reply.flags |= MOUNT_BOOT_POOL_DISK;
    memcpy(reply.pool_disk_guid, mount->config.disk.bytes, 16);
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

/* The upgrade writer for the install that booted: the reviewed GUIDs must still
 * be the boot origin, and the native pool must live on the ESP's own disk. The
 * issuer admits once per boot; a failed open releases the claim. */
static struct syscall_result boot_esp_admit(struct mount_object *mount, uint64_t rights,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & MOUNT_RIGHT_UPGRADE_BOOT_ESP)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct mount_boot_admit_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!request.rights || (request.rights & ~BOOT_UPGRADE_ROOT_RIGHTS)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (memcmp(request.disk_guid, mount->origin.disk, 16) ||
      memcmp(request.partition_guid, mount->origin.partition, 16) ||
      !mount->config.enabled || memcmp(mount->config.disk.bytes, mount->origin.disk, 16)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  bool unclaimed = false;
  if (!atomic_compare_exchange_strong(&mount->upgrade_admitted, &unclaimed, true)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct syscall_result result = open_pair(0, &mount->origin, NULL, 0, MOUNT_ACCESS_READ_WRITE,
      true, request.rights, reply_address, reply_capacity, NULL, true, NULL);
  if (result.status != CALL_OK) {
    atomic_store(&mount->upgrade_admitted, false);
  }
  return result;
}

struct syscall_result fat32_mount_call(struct mount_object *mount, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (mount->backend == MOUNT_BOOT_ESP) {
    switch (operation) {
    case MOUNT_OPEN_FAT32:
      return boot_esp_open(mount, rights, request_address, request_size,
          reply_address, reply_capacity);
    case MOUNT_BOOT_OBSERVE:
      return boot_esp_observe(mount, rights, request_size, reply_address, reply_capacity);
    case MOUNT_BOOT_ADMIT:
      return boot_esp_admit(mount, rights, request_address, request_size,
          reply_address, reply_capacity);
    default:
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
  }
  enum npfs_operation op;
  uint64_t required;
  size_t reply_size;
  switch (operation) {
  case MOUNT_OPEN_ROOT:
    op = NPFS_FAT_ROOT; required = MOUNT_RIGHT_OPEN_ROOT;
    reply_size = sizeof(struct mount_reply);
    break;
  case MOUNT_OBSERVE:
    op = NPFS_FAT_STATE; required = MOUNT_RIGHT_OBSERVE;
    reply_size = sizeof(struct mount_state_reply);
    break;
  case MOUNT_SYNC:
    op = NPFS_FAT_SYNC; required = MOUNT_RIGHT_WRITE;
    reply_size = 0;
    break;
  case MOUNT_CHECK:
    op = NPFS_FAT_CHECK; required = MOUNT_RIGHT_WRITE | MOUNT_RIGHT_INTERVAL;
    reply_size = 0;
    break;
  case MOUNT_BEGIN:
    op = NPFS_FAT_BEGIN; required = MOUNT_RIGHT_INTERVAL;
    reply_size = sizeof(struct mount_interval_reply);
    break;
  case MOUNT_END:
    op = NPFS_FAT_END; required = MOUNT_RIGHT_INTERVAL;
    reply_size = 0;
    break;
  case MOUNT_RETIRE:
    op = NPFS_FAT_RETIRE; required = MOUNT_RIGHT_RETIRE;
    reply_size = 0;
    break;
  default: return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if ((rights & required) != required) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t root_rights = 0, interval_id = 0;
  if (op == NPFS_FAT_ROOT) {
    struct mount_fat32_root_request request;
    if (request_size != sizeof(request)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request, request_address, sizeof(request))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (request.rights & ~DIRECTORY_CONTENT_RIGHTS) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if ((request.rights & ~FAT32_ROOT_RIGHTS) && !(rights & MOUNT_RIGHT_WRITE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request.rights & ~mount->fat32->ceiling) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    root_rights = request.rights;
  } else if (op == NPFS_FAT_END) {
    struct mount_interval_request interval;
    if (request_size != sizeof(interval)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&interval, request_address, sizeof(interval))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    interval_id = interval.id;
  } else if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_capacity < reply_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (reply_size && !user_buffer_check(reply_address, reply_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  if (op == NPFS_FAT_ROOT) {
    enum call_status status = capability_status(capability_request_reservation(1, &reservation, &slot));
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
  }
  struct npfs_request *request = npfs_request_prepare(op);
  request->job.fat_node = mount->fat32;
  request->job.fat_actor = process_current()->control;
  request->job.interval_id = interval_id;
  request->job.rights = root_rights;
  npfs_request_submit_and_wait(request);
  enum call_status status = request->job.status;
  struct mount_state_reply observed = request->job.mount_state;
  struct mount_interval_reply interval = {request->job.interval_id, request->job.interval_deadline};
  struct kernel_object *root_object = request->job.object;
  request->job.object = NULL;
  if (op == NPFS_FAT_BEGIN && status == CALL_OK) {
    bool delivered = copy_to_user(reply_address, &interval, sizeof(interval));
    npfs_request_finish_fat32_begin(request, delivered);
    return (struct syscall_result){delivered ? CALL_OK : CALL_BAD_BUFFER,
        delivered ? sizeof(interval) : 0};
  }
  npfs_request_release(request);
  if (op == NPFS_FAT_ROOT) {
    struct capability_grant grant = {0};
    struct mount_reply reply = {0};
    if (status == CALL_OK) {
      status = task_stop_requested() ? CALL_ENDPOINT_CLOSED :
          capability_status(capability_grant_retain(root_object, root_rights, 0, &grant));
      if (status == CALL_OK) {
        status = capability_status(capability_validate_grants(reservation.table, &grant, 1));
      }
      if (status == CALL_OK) {
        capability_install_reserved(&reservation, &slot, &grant, 1, &reply.root);
      }
    }
    capability_grant_release(&grant);
    capability_reservation_release(&reservation, &slot);
    if (root_object) {
      object_release(root_object);
    }
    if (status == CALL_OK && !copy_returned_handles_to_user(reply_address,
        &reply, sizeof(reply), &reply.root, 1)) {
      status = CALL_BAD_BUFFER;
    }
  } else if (status == CALL_OK && reply_size &&
      !copy_to_user(reply_address, op == NPFS_FAT_BEGIN ? (void *)&interval :
          (void *)&observed, reply_size)) {
    status = CALL_BAD_BUFFER;
  }
  return (struct syscall_result){status, status == CALL_OK ? reply_size : 0};
}
