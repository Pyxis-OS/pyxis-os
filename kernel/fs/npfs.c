#include <kernel/object/forwarder.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/npfs.h>
#include <kernel/fs/fat32.h>
#include <kernel/fs/extents.h>
#include <kernel/fs/metadata.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pressure.h>
#include <kernel/object/clock.h>
#include <kernel/object/process.h>
#include <kernel/object/object.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/object/disk.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/capability.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/completion.h>
#include <kernel-config.h>
#include "npfs_store.h"
#include "npfs_adapter.h"

#define NPFS_MAINTENANCE_MS 1000u
/* Writeback and checkpoint rounds allowed per pool before power-off gives up. */
#define NPFS_SHUTDOWN_PASSES 8u

struct npfs_pool {
  struct npfs_pool *next;
  struct storage_extent extent;
  struct gpt_partition partition;
  struct gpt_guid disk;
  block_device_id device;
  struct npfs_store_pool *store;
  enum call_status maintenance_error;
  bool gpt_degraded;
  uint64_t ram_capacity;
};

/* The worker owns pool, inode and cache state. Only queue/reference transfers
 * run with IF=0. No lock, allocation section or user pointer spans disk waits. */
static struct npfs_pool *pools;
static struct npfs_job *first_job, *last_job;
static struct npfs_node *retired_nodes;
static struct task_wait *worker_wait;
static bool available;
static atomic_size_t admitted;
static size_t wrapper_count, adapter_used, package_used;

/* Power-off flush. The requesting worker and this worker both run on the BSP,
 * so IF=0 serializes the handoff. Once sealed, pools take no further changes
 * until the request is cancelled. */
static struct task_wait *shutdown_wait;
static bool shutdown_requested, shutdown_done;
enum storage_admission { STORAGE_RUNNING, STORAGE_DRAINING, STORAGE_SEALED };
static atomic_int storage_admission;

static bool storage_sealed(void)
{
  return atomic_load_explicit(&storage_admission, memory_order_acquire) == STORAGE_SEALED;
}

static bool storage_draining(void)
{
  return atomic_load_explicit(&storage_admission, memory_order_acquire) == STORAGE_DRAINING;
}
static enum call_status shutdown_status;

static void npfs_worker(void *argument);

void npfs_memory_copy(void *destination, const void *source, size_t length)
{
  memcpy(destination, source, length);
}

void npfs_memory_zero(void *destination, size_t length)
{
  memset(destination, 0, length);
}

void npfs_require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && kernel_task_is_current(npfs_worker, NULL));
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

enum call_status npfs_adapter_allocate(size_t bytes, void **out)
{
  npfs_require_worker();
  if (bytes > NPFS_ADAPTER_BYTES - adapter_used ||
      wrapper_count == NPFS_WRAPPER_LIMIT) {
    return CALL_LIMIT;
  }
  uint64_t flags = cpu_save_interrupts();
  void *data = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!data) {
    return CALL_NO_MEMORY;
  }
  memset(data, 0, bytes);
  adapter_used += bytes;
  ++wrapper_count;
  *out = data;
  return CALL_OK;
}

void npfs_adapter_free(void *data, size_t bytes)
{
  npfs_require_worker();
  KASSERT(bytes <= adapter_used && wrapper_count);
  adapter_used -= bytes;
  --wrapper_count;
  uint64_t flags = cpu_save_interrupts();
  kfree(data);
  cpu_restore_interrupts(flags);
}

/* Zeroed memory from the package view budget, NPFS_PACKAGE_INDEX_BYTES. */
enum call_status npfs_package_allocate(size_t bytes, void **out)
{
  npfs_require_worker();
  if (bytes > NPFS_PACKAGE_INDEX_BYTES - package_used) {
    return CALL_LIMIT;
  }
  uint64_t flags = cpu_save_interrupts();
  void *data = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!data) {
    return CALL_NO_MEMORY;
  }
  memset(data, 0, bytes);
  package_used += bytes;
  *out = data;
  return CALL_OK;
}

void npfs_package_free(void *data, size_t bytes)
{
  npfs_require_worker();
  KASSERT(bytes <= package_used);
  package_used -= bytes;
  uint64_t flags = cpu_save_interrupts();
  kfree(data);
  cpu_restore_interrupts(flags);
}

static struct npfs_store_context context_for(uint64_t deadline)
{
  struct npfs_store_context context = {.deadline = deadline};
  context.time_valid = clock_wall_coarse_nanoseconds(&context.time_ns);
  return context;
}

static enum call_status select_partition(struct npfs_store_context *context,
    struct npfs_job *job, struct gpt_partition *partition,
    struct block_info *device, bool *degraded)
{
  if (job->device) {
    uint64_t flags = cpu_save_interrupts();
    bool inventory_available = block_installer_inventory_available();
    bool allowed = block_installer_device(job->device);
    cpu_restore_interrupts(flags);
    if (!inventory_available) {
      return CALL_UNAVAILABLE;
    }
    if (!allowed) {
      return CALL_NOT_FOUND;
    }
  }
  enum gpt_status selected_status = GPT_UNAVAILABLE;
  uint64_t selected_blocks = 0;
  uint32_t selected_block_size = 0;
  struct gpt_guid selected_guid = {0};
  struct gpt_partition selected_partition = {0};
  bool partition_found = false;
  block_device_id selected_id = BLOCK_DEVICE_ID_NONE;
  for (;;) {
    selected_id = BLOCK_DEVICE_ID_NONE;
    uint64_t flags = cpu_save_interrupts();
    bool pending = !job->device && !block_discovery_finished();
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
      if (job->device) {
        selected_id = id;
      } else if ((snapshot->status == GPT_HEALTHY || snapshot->status == GPT_DEGRADED) &&
          !memcmp(&job->disk, &snapshot->disk_guid, sizeof(job->disk))) {
        if (selected_id) {
          cpu_restore_interrupts(flags);
          return CALL_IO;
        }
        selected_id = id;
      } else {
        continue;
      }
      selected_status = snapshot->status;
      selected_blocks = snapshot->disk_blocks;
      selected_block_size = snapshot->block_size;
      selected_guid = snapshot->disk_guid;
      partition_found = false;
      for (size_t p = 0; p < snapshot->partition_count; ++p) {
        if (snapshot->partitions[p].entry_number == job->partition) {
          selected_partition = snapshot->partitions[p];
          partition_found = true;
          break;
        }
      }
    }
    cpu_restore_interrupts(flags);
    if (!pending) {
      break;
    }
    if (task_deadline_expired(context->deadline)) {
      return CALL_TIMED_OUT;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  if (!selected_id) {
    uint64_t flags = cpu_save_interrupts();
    bool complete = block_inventory_complete();
    cpu_restore_interrupts(flags);
    return complete ? CALL_NOT_FOUND : CALL_UNAVAILABLE;
  }
  if (disk_partition_claimed(selected_id, job->partition)) {
    return CALL_BUSY;
  }
  switch (selected_status) {
  case GPT_HEALTHY:
  case GPT_DEGRADED: break;
  case GPT_UNAVAILABLE:
  case GPT_UNSUPPORTED: return CALL_UNAVAILABLE;
  case GPT_NO_MEMORY: return CALL_NO_MEMORY;
  case GPT_TIMED_OUT: return CALL_TIMED_OUT;
  default: return CALL_IO;
  }
  if (partition) {
    if (!partition_found) {
      return CALL_NOT_FOUND;
    }
    *partition = selected_partition;
  }
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(selected_id, device);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    context->backing_error = result;
    return result == BLOCK_TIMED_OUT ? CALL_TIMED_OUT : CALL_UNAVAILABLE;
  }
  if (device->write_failed) {
    return CALL_IO;
  }
  if (device->block_size != selected_block_size ||
      device->block_count != selected_blocks ||
      (device->block_size != 512 && device->block_size != NPFS_BLOCK_SIZE) ||
      device->max_transfer < device->block_size) {
    return CALL_UNAVAILABLE;
  }
  if (partition && (!partition->block_count || partition->first_block >= device->block_count ||
      partition->block_count > device->block_count - partition->first_block)) {
    return CALL_IO;
  }
  job->device = selected_id;
  job->disk = selected_guid;
  *degraded = selected_status == GPT_DEGRADED;
  return CALL_OK;
}

static bool mutation_rights(uint64_t rights)
{
  return rights & (DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_WRITE_FILES | DIRECTORY_RIGHT_REMOVE);
}

static enum call_status open_pool(struct npfs_store_context *context,
    struct npfs_job *job, struct npfs_pool **out)
{
  struct gpt_partition partition;
  struct block_info device;
  bool degraded;
  enum call_status status = select_partition(context, job, &partition, &device, &degraded);
  if (status != CALL_OK) {
    return status;
  }
  bool writable = mutation_rights(job->rights);
  if (writable && degraded) {
    return CALL_READ_ONLY;
  }
  for (struct npfs_pool *pool = pools; pool; pool = pool->next) {
    if (pool->device == job->device &&
        pool->partition.entry_number == partition.entry_number &&
        pool->partition.first_block == partition.first_block &&
        pool->partition.block_count == partition.block_count) {
      if (writable) {
        status = npfs_store_upgrade(context, pool->store);
      }
      if (status == CALL_OK) {
        *out = pool;
      }
      return status;
    }
  }
  struct npfs_pool *pool;
  status = npfs_adapter_allocate(sizeof(*pool), (void **)&pool);
  if (status != CALL_OK) {
    return status;
  }
  status = storage_extent_reserve(&pool->extent, job->device, partition.first_block,
      partition.block_count, STORAGE_EXTENT_PENDING_NPFS);
  if (status != CALL_OK) {
    npfs_adapter_free(pool, sizeof(*pool));
    return status;
  }
  status = npfs_store_open(context, job->device, &partition, &device, writable, &pool->store);
  if (status != CALL_OK) {
    storage_extent_release(&pool->extent);
    npfs_adapter_free(pool, sizeof(*pool));
    return status;
  }
  pool->partition = partition;
  pool->disk = job->disk;
  pool->device = job->device;
  pool->gpt_degraded = degraded;
  storage_extent_publish(&pool->extent, STORAGE_EXTENT_NPFS);
  pool->next = pools;
  pools = pool;
  *out = pool;
  return CALL_OK;
}

static struct kernel_object *node_object(struct npfs_node *node)
{
  return node->kind == DIRECTORY_KIND_DIRECTORY ?
      &node->wrapper.directory.object : &node->wrapper.file.object;
}

enum call_status npfs_allocate_node(uint64_t kind, uint64_t rights,
    struct npfs_node **out)
{
  struct npfs_node *node;
  enum call_status status = npfs_adapter_allocate(sizeof(*node), (void **)&node);
  if (status != CALL_OK) {
    return status;
  }
  node->kind = kind;
  node->rights = rights;
  uint64_t flags = cpu_save_interrupts();
  if (kind == DIRECTORY_KIND_DIRECTORY) {
    directory_init_npfs(&node->wrapper.directory, node);
  } else {
    file_init_npfs(&node->wrapper.file, node);
  }
  cpu_restore_interrupts(flags);
  *out = node;
  return CALL_OK;
}

enum call_status npfs_wrap_inode(struct npfs_store_inode *inode, uint64_t rights,
    struct kernel_object **out)
{
  struct npfs_node *node;
  enum call_status status = npfs_allocate_node(npfs_store_kind(inode), rights, &node);
  if (status != CALL_OK) {
    npfs_store_release(inode);
    return status;
  }
  node->inode = inode;
  *out = node_object(node);
  return CALL_OK;
}

static void wake_worker(void)
{
  struct task_wait *wake = worker_wait;
  worker_wait = NULL;
  mm_pressure_wait(NULL);
  if (wake) {
    task_wait_wake(wake);
  }
}

void npfs_retire(struct npfs_node *node)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  node->cleanup_group = object_cleanup_defer();
  node->next = retired_nodes;
  retired_nodes = node;
  wake_worker();
}

static void destroy_nodes(struct npfs_node *list)
{
  while (list) {
    struct npfs_node *node = list;
    list = node->next;
    /* Dirty state belongs to the pool and outlives this process's wrapper.
     * Final release may enable unlink cleanup, but never promises durability. */
    if (node->inode) {
      npfs_store_release(node->inode);
    }
    npfs_commands_release(node->commands);
    npfs_sdk_release(node->sdk);
    struct execution_group *group = node->cleanup_group;
    npfs_adapter_free(node, sizeof(*node));
    if (group) {
      uint64_t flags = cpu_save_interrupts();
      execution_group_cleanup_end(group);
      cpu_restore_interrupts(flags);
    }
  }
}

static enum call_status check_name(const char *name, size_t count)
{
  if (count > NPFS_NAME_MAX) {
    return CALL_NAME_TOO_LONG;
  }
  return npfs_name_valid((const uint8_t *)name, count) ? CALL_OK : CALL_BAD_REQUEST;
}

static enum call_status acquire_root(struct npfs_store_context *context, struct npfs_job *job)
{
  enum call_status status = check_name(job->name, job->count);
  if (status != CALL_OK || !job->partition || (job->rights & ~DIRECTORY_RIGHTS) ||
      !(job->rights & DIRECTORY_RIGHT_LOOKUP)) {
    return status != CALL_OK ? status : CALL_BAD_REQUEST;
  }
  struct npfs_pool *pool;
  status = open_pool(context, job, &pool);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_store_inode *inode;
  status = npfs_store_root(context, pool->store, job->name, job->count, &inode);
  return status == CALL_OK ? npfs_wrap_inode(inode, job->rights, &job->object) : status;
}

static enum call_status acquire_ram_root(struct npfs_store_context *context,
    struct npfs_job *job)
{
  enum call_status status = check_name(job->name, job->count);
  if (status != CALL_OK || !(job->rights & DIRECTORY_RIGHT_LOOKUP) ||
      (job->rights & ~DIRECTORY_RIGHTS)) return status != CALL_OK ? status : CALL_BAD_REQUEST;
  struct npfs_pool *pool = pools;
  while (pool && !pool->ram_capacity) pool = pool->next;
  if (pool && pool->ram_capacity != job->offset) return CALL_BAD_REQUEST;
  if (!pool) {
    status = npfs_adapter_allocate(sizeof(*pool), (void **)&pool);
    if (status != CALL_OK) return status;
    status = npfs_store_open_ram(context, job->offset, &pool->store);
    if (status != CALL_OK) {
      npfs_adapter_free(pool, sizeof(*pool));
      return status;
    }
    pool->ram_capacity = job->offset;
    pool->next = pools;
    pools = pool;
  }
  struct npfs_store_inode *inode;
  status = npfs_store_root(context, pool->store, job->name, job->count, &inode);
  if (status == CALL_NOT_FOUND) {
    uint8_t id[NPFS_ID_SIZE] = {'R', 'A', 'M', 'V', 'O', 'L', 0, 0};
    uint64_t identity;
    uint64_t flags = cpu_save_interrupts();
    bool identified = fs_metadata_allocate_id(&identity);
    cpu_restore_interrupts(flags);
    if (!identified) return CALL_LIMIT;
    memcpy(id + 8, &identity, sizeof(identity));
    status = npfs_store_create_volume(context, pool->store, job->name, job->count, id);
    if (status == CALL_OK)
      status = npfs_store_root(context, pool->store, job->name, job->count, &inode);
  }
  return status == CALL_OK ? npfs_wrap_inode(inode, job->rights, &job->object) : status;
}

static enum call_status create_volume(struct npfs_store_context *context, struct npfs_job *job)
{
  enum call_status status = check_name(job->name, job->count);
  if (status != CALL_OK || !job->partition) {
    return status != CALL_OK ? status : CALL_BAD_REQUEST;
  }
  /* Content rights make open_pool admit or upgrade the pool for writing. */
  job->rights = DIRECTORY_CONTENT_RIGHTS;
  struct npfs_pool *pool;
  status = open_pool(context, job, &pool);
  if (status != CALL_OK) {
    return status;
  }
  return npfs_store_create_volume(context, pool->store, job->name, job->count, job->data);
}

static enum call_status child(struct npfs_store_context *context, struct npfs_job *job)
{
  bool typed = job->operation == NPFS_LOOKUP && job->kind == DIRECTORY_KIND_ANY;
  if (!typed && job->kind != DIRECTORY_KIND_DIRECTORY && job->kind != DIRECTORY_KIND_FILE) {
    return CALL_BAD_REQUEST;
  }
  uint64_t mask = !typed && job->kind == DIRECTORY_KIND_DIRECTORY ? DIRECTORY_RIGHTS : FILE_RIGHTS;
  if ((job->child_rights & ~mask) || (typed &&
      (job->directory_child_rights & ~DIRECTORY_RIGHTS))) {
    return CALL_BAD_REQUEST;
  }
  uint64_t allowed = job->kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
      ((job->rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0) |
      ((job->rights & DIRECTORY_RIGHT_WRITE_FILES) ? FILE_RIGHT_WRITE : 0);
  if (!typed && (job->child_rights & ~allowed)) {
    return CALL_DENIED;
  }
  enum call_status status = check_name(job->name, job->count);
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_store_inode *inode;
  if (job->operation == NPFS_LOOKUP) {
    status = npfs_store_lookup(context, job->node->inode, job->name, job->count, &inode);
    if (status != CALL_OK) {
      return status;
    }
    uint64_t kind = npfs_store_kind(inode);
    if (!typed && kind != job->kind) {
      npfs_store_release(inode);
      return CALL_WRONG_TYPE;
    }
    uint64_t child_rights = typed && kind == DIRECTORY_KIND_DIRECTORY ?
        job->directory_child_rights : job->child_rights;
    allowed = kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
        ((job->rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0) |
        ((job->rights & DIRECTORY_RIGHT_WRITE_FILES) ? FILE_RIGHT_WRITE : 0);
    if (child_rights & ~allowed) {
      npfs_store_release(inode);
      return CALL_DENIED;
    }
    return npfs_wrap_inode(inode, child_rights, &job->object);
  }
  if (!job->reservation.table && !job->completion) {
    return CALL_BAD_REQUEST;
  }
  struct npfs_node *node;
  status = npfs_allocate_node(job->kind, job->child_rights, &node);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  /* Completion CREATE retains returned authority independently; only the
   * synchronous path owns a borrowed destination table and installs here. */
  enum capability_result prepared = capability_grant_retain(node_object(node),
      job->child_rights, 0, &job->grant);
  if (prepared == CAP_OK) {
    prepared = forwarder_derive_grant(&job->grant, task_forwarder_operation());
  }
  if (prepared == CAP_OK && !job->completion) {
    prepared = capability_validate_grants(job->reservation.table, &job->grant, 1);
  }
  cpu_restore_interrupts(flags);
  if (prepared == CAP_OK) {
    status = npfs_store_create(context, job->node->inode, job->name, job->count,
        job->kind, &inode);
    if (status == CALL_OK) {
      node->inode = inode;
      if (!job->completion) {
        flags = cpu_save_interrupts();
        capability_install_reserved(&job->reservation, &job->slot, &job->grant,
            1, &job->handle);
        cpu_restore_interrupts(flags);
      }
    }
  } else {
    KASSERT(prepared == CAP_NO_MEMORY || prepared == CAP_LIMIT || prepared == CAP_DENIED);
    status = prepared == CAP_NO_MEMORY ? CALL_NO_MEMORY :
        prepared == CAP_DENIED ? CALL_DENIED : CALL_LIMIT;
  }
  flags = cpu_save_interrupts();
  if (status != CALL_OK || !job->completion) {
    capability_grant_release(&job->grant);
  }
  object_release(node_object(node));
  cpu_restore_interrupts(flags);
  return status;
}

static enum call_status enumerate_node(struct npfs_store_context *context,
    struct npfs_job *job)
{
  if (!job->cursor.generation && job->cursor.position) {
    return CALL_BAD_REQUEST;
  }
  struct npfs_dirent entry = {0};
  uint64_t position, generation;
  enum call_status status = npfs_store_enumerate(context, job->node->inode,
      job->cursor.generation, job->cursor.position, &entry, &position, &generation);
  if (status != CALL_OK) {
    return status;
  }
  struct directory_enumerate_reply reply = {.cursor = job->cursor};
  if (job->cursor.generation && job->cursor.generation != generation) {
    reply.outcome = DIRECTORY_CHANGED;
  } else if (!entry.inode) {
    reply.outcome = DIRECTORY_END;
    reply.cursor = (struct directory_cursor){generation, position};
  } else {
    struct npfs_store_inode *child_inode;
    status = npfs_store_lookup(context, job->node->inode, (const char *)entry.name,
        entry.name_length, &child_inode);
    if (status != CALL_OK) {
      return status;
    }
    reply.kind = npfs_store_kind(child_inode);
    npfs_store_release(child_inode);
    reply.name_size = entry.name_length + 1;
    if (job->count < reply.name_size) {
      reply.outcome = DIRECTORY_BUFFER_TOO_SMALL;
    } else {
      reply.outcome = DIRECTORY_ENTRY;
      reply.cursor = (struct directory_cursor){generation, position};
      memcpy(job->name, entry.name, entry.name_length);
      job->name[entry.name_length] = '\0';
    }
  }
  job->entry = reply;
  return CALL_OK;
}

static enum call_status capture_file(struct npfs_store_context *context, struct npfs_job *job)
{
  uint64_t size = npfs_store_size(job->node->inode);
  struct image_capture captured = {0};
  uint64_t flags = cpu_save_interrupts();
  enum call_status status = image_capture_allocate(size, &captured);
  cpu_restore_interrupts(flags);
  if (status != CALL_OK) {
    return status;
  }
  size_t read = 0;
  status = npfs_store_read(context, job->node->inode, 0,
      (void *)captured.address, captured.size, &read);
  if (status == CALL_OK && read != size) {
    status = CALL_IO;
  }
  if (status != CALL_OK) {
    flags = cpu_save_interrupts();
    image_capture_release(&captured);
    cpu_restore_interrupts(flags);
    return status;
  }
  job->captured = captured;
  return CALL_OK;
}

static enum call_status perform(struct npfs_store_context *context, struct npfs_job *job)
{
  if (job->operation == NPFS_FAT_FINISH_BEGIN) {
    return fat32_perform(context, job);
  }
  if (task_deadline_expired(context->deadline)) {
    return CALL_TIMED_OUT;
  }
  if (job->fat_node || (job->operation >= NPFS_FAT_OPEN && job->operation <= NPFS_FAT_CHECK)) {
    if (job->raw && job->operation == NPFS_FAT_OPEN) {
      enum call_status status = disk_validate_binding(job->raw, job->partition);
      if (status != CALL_OK) {
        return status;
      }
    }
    return fat32_perform(context, job);
  }
  if (job->operation >= NPFS_RAW_INFO) {
    return disk_perform(context, job);
  }
  if (job->raw && (job->operation == NPFS_ROOT || job->operation == NPFS_CREATE_VOLUME)) {
    enum call_status status = disk_validate_binding(job->raw, job->partition);
    if (status != CALL_OK) {
      return status;
    }
  }
  if (job->operation == NPFS_RAM_ROOT) {
    return acquire_ram_root(context, job);
  }
  if (job->operation == NPFS_ROOT) {
    return acquire_root(context, job);
  }
  if (job->operation == NPFS_CREATE_VOLUME) {
    return create_volume(context, job);
  }
  if (job->operation == NPFS_DISK_SYNC) {
    struct block_info device;
    bool degraded;
    enum call_status result = select_partition(context, job, NULL, &device, &degraded);
    if (result != CALL_OK) {
      return result;
    }
    for (struct npfs_pool *pool = pools; pool; pool = pool->next) {
      if (job->device != pool->device) {
        continue;
      }
      enum call_status status = npfs_store_sync(context, pool->store);
      if (result == CALL_OK) {
        result = status;
      }
    }
    return result;
  }
  if (!job->node || !job->node->inode) {
    return CALL_BAD_REQUEST;
  }
  struct npfs_node *node = job->node;
  bool directory = node->kind == DIRECTORY_KIND_DIRECTORY;
  uint64_t all = directory ? DIRECTORY_RIGHTS : FILE_RIGHTS;
  if (job->rights & ~all) {
    return CALL_BAD_REQUEST;
  }
  if (job->rights & ~node->rights) {
    return CALL_DENIED;
  }
  if (node->sdk || job->operation == NPFS_SDK_VIEW) {
    return npfs_sdk_perform(context, job);
  }
  if (node->commands || job->operation == NPFS_COMMAND_VIEW || job->operation == NPFS_ROOT_VIEW ||
      job->operation == NPFS_COMMAND_CAPTURE || job->operation == NPFS_COMMAND_INFO ||
      job->operation == NPFS_COMMAND_LOOKUP || job->operation == NPFS_COMMAND_BIND ||
      job->operation == NPFS_ROOT_SELECT) {
    return npfs_commands_perform(context, job);
  }
  uint64_t required;
  switch (job->operation) {
  case NPFS_LOOKUP: required = DIRECTORY_RIGHT_LOOKUP; break;
  case NPFS_ENUMERATE: required = DIRECTORY_RIGHT_ENUMERATE; break;
  case NPFS_CREATE: required = DIRECTORY_RIGHT_CREATE; break;
  case NPFS_REMOVE:
  case NPFS_RENAME: required = DIRECTORY_RIGHT_REMOVE; break;
  case NPFS_LINK: required = DIRECTORY_LINK_SOURCE_RIGHTS; break;
  case NPFS_FILESYSTEM_INFO: required = DIRECTORY_RIGHT_FILESYSTEM_INFO; break;
  case NPFS_READ:
  case NPFS_CAPTURE: required = FILE_RIGHT_READ; break;
  case NPFS_SIZE: required = FILE_RIGHTS; break;
  case NPFS_INFO: required = directory ? 0 : FILE_RIGHTS; break;
  case NPFS_WRITE:
  case NPFS_RESIZE: required = FILE_RIGHT_WRITE; break;
  case NPFS_SYNC: required = directory ? DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_REMOVE :
      FILE_RIGHT_WRITE; break;
  default: return CALL_BAD_OPERATION;
  }
  bool directory_operation = job->operation == NPFS_LOOKUP ||
      job->operation == NPFS_ENUMERATE || job->operation == NPFS_CREATE ||
      job->operation == NPFS_REMOVE || job->operation == NPFS_RENAME || job->operation == NPFS_LINK ||
      job->operation == NPFS_FILESYSTEM_INFO;
  if ((directory_operation && !directory) ||
      (!directory_operation && job->operation != NPFS_SYNC &&
       job->operation != NPFS_INFO && directory)) {
    return CALL_WRONG_TYPE;
  }
  if (required && !(job->rights & required)) {
    return CALL_DENIED;
  }
  if (job->operation == NPFS_LINK && (job->rights & required) != required) {
    return CALL_DENIED;
  }
  switch (job->operation) {
  case NPFS_LOOKUP:
  case NPFS_CREATE: return child(context, job);
  case NPFS_ENUMERATE: return enumerate_node(context, job);
  case NPFS_READ: {
    if (job->count > sizeof(job->data)) {
      return CALL_BAD_REQUEST;
    }
    size_t count = 0;
    enum call_status status = npfs_store_read(context, node->inode, job->offset,
        job->data, job->count, &count);
    job->progress = count;
    if (status == CALL_OK || job->completion) {
      job->count = count;
    }
    return status;
  }
  case NPFS_SIZE:
    job->offset = npfs_store_size(node->inode);
    return CALL_OK;
  case NPFS_INFO:
    npfs_store_file_info(node->inode, &job->file_info);
    return CALL_OK;
  case NPFS_CAPTURE: return capture_file(context, job);
  case NPFS_WRITE: {
    if (job->count > FILE_WRITE_MAX_BYTES) {
      return CALL_BAD_REQUEST;
    }
    size_t count = 0;
    enum call_status status = npfs_store_write(context, node->inode, job->offset,
        job->data, job->count, &count);
    job->progress = count;
    if (status == CALL_OK || job->completion) {
      job->count = count;
    }
    return status;
  }
  case NPFS_RESIZE: return npfs_store_resize(context, node->inode, job->offset);
  case NPFS_REMOVE: {
    enum call_status status = check_name(job->name, job->count);
    return status == CALL_OK ? npfs_store_remove(context, node->inode, job->name,
        job->count, job->kind) : status;
  }
  case NPFS_LINK:
  case NPFS_RENAME: {
    struct npfs_node *destination = job->destination;
    if (!destination || destination->kind != DIRECTORY_KIND_DIRECTORY) {
      return CALL_WRONG_TYPE;
    }
    if ((job->destination_rights & ~DIRECTORY_RIGHTS) ||
        (job->destination_rights & ~destination->rights) ||
        !(job->destination_rights & DIRECTORY_RIGHT_CREATE) ||
        (job->replace && !(job->destination_rights & DIRECTORY_RIGHT_REMOVE))) {
      return CALL_DENIED;
    }
    if (!npfs_store_same_volume(node->inode, destination->inode)) {
      return CALL_BAD_OPERATION;
    }
    enum call_status status = check_name(job->name, job->count);
    if (status == CALL_OK) {
      status = check_name(job->destination_name, job->destination_length);
    }
    if (job->operation == NPFS_LINK) {
      return status == CALL_OK ? npfs_store_link(context, node->inode, job->name,
          job->count, destination->inode, job->destination_name, job->destination_length) : status;
    }
    return status == CALL_OK ? npfs_store_rename(context, node->inode, job->name,
        job->count, destination->inode, job->destination_name, job->destination_length,
        job->replace, job->kind) : status;
  }
  case NPFS_SYNC:
    /* FILE sync covers that file and staged namespace edits; DIRECTORY sync
     * covers the whole pool. */
    if (!directory) {
      return npfs_store_sync_file(context, node->inode);
    }
    return npfs_store_sync(context, npfs_store_inode_pool(node->inode));
  case NPFS_FILESYSTEM_INFO:
    npfs_store_info(node->inode, &job->info);
    for (struct npfs_pool *pool = pools; pool; pool = pool->next) {
      if (pool->store == npfs_store_inode_pool(node->inode) && pool->gpt_degraded) {
        job->info.flags |= FILESYSTEM_FLAG_GPT_DEGRADED;
      }
    }
    return CALL_OK;
  default: return CALL_BAD_OPERATION;
  }
}

static void complete_job(struct npfs_job *job)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  bool transfer = job->transfer_destination != NULL;
  struct execution_group *transfer_previous = object_cleanup_enter(job->user_request ?
      job->user_request->request.cleanup_group : job->completion ?
      job->completion->cleanup_group : NULL);
  file_transfer_finish(job);
  object_cleanup_leave(transfer_previous);
  struct npfs_request *request = job->user_request;
  struct native_completion *completion = job->completion;
  bool pending_begin = request && job->operation == NPFS_FAT_BEGIN && job->status == CALL_OK;
  job->completion = NULL;
  job->user_request = NULL;
  job->node = NULL;
  job->destination = NULL;
  job->raw = NULL;
  if (!pending_begin && job->admitted && job->fat_node) {
    fat32_request_complete(job->fat_node);
  }
  if (job->admitted && job->fat_destination) {
    fat32_request_complete(job->fat_destination);
  }
  if (!pending_begin) {
    job->fat_node = NULL;
  }
  job->fat_destination = NULL;
  if (!pending_begin && job->fat_actor) {
    object_release(&job->fat_actor->object);
    job->fat_actor = NULL;
  }
  if (!completion) {
    capability_grant_release(&job->grant);
  }
  capability_reservation_release(&job->reservation, &job->slot);
  job->next = NULL;
  if (!pending_begin && job->admitted) {
    KASSERT(atomic_fetch_sub_explicit(&admitted, 1, memory_order_relaxed));
    job->admitted = false;
  }
  job->state = NPFS_JOB_COMPLETE;
  wake_worker();
  if (request) {
    bsp_request_complete(&request->request);
  }
  if (completion) {
    uint8_t output[FILE_PAYLOAD_MAX];
    size_t size = 0;
    enum call_status status = job->status;
    if (transfer) {
      completion->progress = job->progress;
      completion->progress_known = status != CALL_OUTCOME_UNKNOWN;
      if (status == CALL_OK) {
        struct file_transfer_reply reply = {.transferred = job->progress};
        memcpy(output, &reply, sizeof(reply));
        size = sizeof(reply);
      }
    } else if (job->operation == NPFS_READ) {
      completion->progress = job->progress;
      completion->progress_known = job->progress || status != CALL_OUTCOME_UNKNOWN;
      if (status == CALL_OK || job->progress) {
        struct file_read_reply reply = {.read = job->progress};
        memcpy(output, &reply, sizeof(reply));
        memcpy(output + sizeof(reply), job->data, job->progress);
        size = sizeof(reply) + job->progress;
      }
    } else if (job->operation == NPFS_WRITE) {
      completion->progress = job->progress;
      completion->progress_known = job->progress || status != CALL_OUTCOME_UNKNOWN;
      if (status == CALL_OK || job->progress) {
        struct file_write_reply reply = {.written = job->progress};
        memcpy(output, &reply, sizeof(reply));
        size = sizeof(reply);
      }
    } else if (job->operation == NPFS_SIZE && status == CALL_OK) {
      struct file_size_reply reply = {.size = job->offset};
      memcpy(output, &reply, sizeof(reply));
      size = sizeof(reply);
    }
    struct capability_grant grant = job->grant;
    job->grant = (struct capability_grant){0};
    completion->backend = NULL;
    kfree(job);
    native_completion_finish(completion, status, output, size, &grant);
  }
}

/* Operations that cannot change a pool or its device, allowed while sealed. */
static bool allowed_while_sealed(enum npfs_operation operation)
{
  switch (operation) {
  case NPFS_LOOKUP:
  case NPFS_COMMAND_VIEW:
  case NPFS_ROOT_VIEW:
  case NPFS_ROOT_SELECT:
  case NPFS_SDK_VIEW:
  case NPFS_COMMAND_CAPTURE:
  case NPFS_COMMAND_INFO:
  case NPFS_COMMAND_LOOKUP:
  case NPFS_COMMAND_BIND:
  case NPFS_ENUMERATE:
  case NPFS_READ:
  case NPFS_SIZE:
  case NPFS_INFO:
  case NPFS_CAPTURE:
  case NPFS_FILESYSTEM_INFO:
  case NPFS_FAT_STATE:
  case NPFS_FAT_FINISH_BEGIN:
  case NPFS_RAW_INFO:
  case NPFS_RAW_READ:
    return true;
  default:
    return false;
  }
}

/* Writes every writable pool's dirty data and checkpoints until its journal is
 * EMPTY, so the next boot has nothing to replay. Failed and read-only pools
 * take no writes and are skipped; a failure was logged when it happened. */
static enum call_status flush_for_shutdown(void)
{
  struct npfs_store_context fat_context = context_for(task_deadline_after_ms(NPFS_TIMEOUT_MS));
  enum call_status result = fat32_shutdown_seal(&fat_context);
  if (result != CALL_OK) {
    return result;
  }
  for (struct npfs_pool *pool = pools; pool; pool = pool->next) {
    if (!npfs_store_writable(pool->store)) {
      continue;
    }
    struct npfs_store_context context = context_for(task_deadline_after_ms(NPFS_TIMEOUT_MS));
    enum call_status status = npfs_store_sync(&context, pool->store);
    for (unsigned pass = 0; status == CALL_OK && !npfs_store_journal_empty(pool->store); ++pass) {
      if (pass == NPFS_SHUTDOWN_PASSES) {
        status = CALL_TIMED_OUT;
        break;
      }
      status = npfs_store_maintain(&context, pool->store, true, false);
    }
    if (status != CALL_OK) {
      kerror("npfs: power-off flush failed (status %u)\n", (unsigned)status);
      if (result == CALL_OK) {
        result = status;
      }
    }
  }
  return result;
}

static bool maintenance_pending(void)
{
  for (struct npfs_pool *pool = pools; pool; pool = pool->next) {
    if (pool->maintenance_error == CALL_OK && npfs_store_pending(pool->store)) {
      return true;
    }
  }
  return false;
}

static void npfs_worker(void *argument)
{
  (void)argument;
  uint64_t next_flush = task_deadline_after_ms(CONFIG_NPFS_FLUSH_SECONDS * 1000u);
  uint64_t next_maintenance = task_deadline_after_ms(NPFS_MAINTENANCE_MS);
  for (;;) {
    bool pending = !storage_sealed() && !storage_draining() && maintenance_pending();
    uint64_t flags = cpu_save_interrupts();
    bool shutdown = shutdown_requested;
    bool pressure = mm_pressure_take();
    struct disk_object *disks = storage_sealed() ? NULL : disk_take_retired();
    struct fat32_node *fat_nodes = storage_sealed() ? NULL : fat32_take_retired();
    struct npfs_node *nodes = retired_nodes;
    retired_nodes = NULL;
    struct npfs_job *job = first_job;
    if (job) {
      first_job = job->next;
      if (!first_job) {
        last_job = NULL;
      }
      job->next = NULL;
      job->state = NPFS_JOB_ACTIVE;
    }
    uint64_t now = clock_coarse_ns();
    bool flush = now >= next_flush;
    bool maintenance = !storage_sealed() && !storage_draining() && (pressure || flush ||
        now >= next_maintenance || pending);
    if (!nodes && !fat_nodes && !disks && !job && !maintenance &&
        (!shutdown || atomic_load_explicit(&admitted, memory_order_acquire))) {
      KASSERT(!worker_wait);
      struct task_wait *wait = task_wait_prepare();
      worker_wait = wait;
      mm_pressure_wait(wait);
      task_wait_sleep_until_coarse(wait, next_flush < next_maintenance ? next_flush : next_maintenance);
      mm_pressure_wait(NULL);
      worker_wait = NULL;
      cpu_restore_interrupts(flags);
      continue;
    }
    cpu_restore_interrupts(flags);
    disk_cleanup_retired(disks);
    destroy_nodes(nodes);
    fat32_cleanup_nodes(fat_nodes);
    if (job) {
      flags = cpu_save_interrupts();
      struct execution_group *previous = object_cleanup_enter(job->user_request ?
          job->user_request->request.cleanup_group : job->completion ?
          job->completion->cleanup_group : NULL);
      struct forwarder_operation *operation_previous = task_forwarder_set(job->user_request ?
          job->user_request->request.operation : job->completion ? &job->completion->lineage : NULL);
      bool cleanup = job->operation == NPFS_FAT_FINISH_BEGIN;
      bool start = cleanup || (job->completion ? native_completion_mark_started(job->completion) :
          task_operation_start());
      cpu_restore_interrupts(flags);
      struct npfs_store_context context = context_for(job->deadline);
      context.completion = job->completion;
      if (!start) {
        job->status = job->completion ? native_completion_cancel_reason(job->completion) : CALL_DENIED;
        if (job->operation == NPFS_READ || job->operation == NPFS_WRITE) {
          job->count = 0;
        }
      } else {
        job->status = file_transfer_prepare(job);
        if (job->status == CALL_OK) {
          job->status = storage_sealed() && !allowed_while_sealed(job->operation) ? CALL_UNAVAILABLE :
              perform(&context, job);
        }
      }
      if (job->status != CALL_OK) {
        fat32_note_job_failure(job);
      }
      job->format_status = context.format_error;
      job->backing_error = context.backing_error;
      flags = cpu_save_interrupts();
      if (job->status != CALL_OK) {
        if (job->object) {
          object_release(job->object);
          job->object = NULL;
        }
        if (job->control) {
          object_release(job->control);
          job->control = NULL;
        }
        image_capture_release(&job->captured);
      }
      task_forwarder_set(operation_previous);
      object_cleanup_leave(previous);
      complete_job(job);
      cpu_restore_interrupts(flags);
    }
    /* Queued requests drain first; held user tasks submit no more. */
    if (shutdown && !job) {
      flags = cpu_save_interrupts();
      bool drain_pending = atomic_load_explicit(&admitted, memory_order_acquire) ||
          first_job || retired_nodes || fat32_retired_pending() || disk_retired_pending();
      cpu_restore_interrupts(flags);
      if (drain_pending) {
        kernel_task_yield_if_runnable();
        continue;
      }
      enum call_status status = flush_for_shutdown();
      flags = cpu_save_interrupts();
      atomic_store_explicit(&storage_admission, status == CALL_OK ? STORAGE_SEALED :
          STORAGE_RUNNING, memory_order_release);
      shutdown_status = status;
      shutdown_requested = false;
      shutdown_done = true;
      struct task_wait *wake = shutdown_wait;
      shutdown_wait = NULL;
      if (wake) {
        task_wait_wake(wake);
      }
      cpu_restore_interrupts(flags);
      maintenance = false;
    }
    if (maintenance) {
      struct npfs_store_context fat_context = context_for(task_deadline_after_ms(NPFS_TIMEOUT_MS));
      fat32_maintain(&fat_context, flush, pressure);
      for (struct npfs_pool *pool = pools; pool; pool = pool->next) {
        struct npfs_store_context context = context_for(task_deadline_after_ms(NPFS_TIMEOUT_MS));
        enum call_status status = npfs_store_maintain(&context, pool->store, flush, pressure);
        if (status != CALL_OK && status != pool->maintenance_error) {
          kerror("npfs: background writeback/cleanup failed (status %u); pool retained\n",
              (unsigned)status);
        }
        pool->maintenance_error = status;
      }
      next_maintenance = task_deadline_after_ms(NPFS_MAINTENANCE_MS);
      if (flush) {
        next_flush = task_deadline_after_ms(CONFIG_NPFS_FLUSH_SECONDS * 1000u);
      }
    }
    kernel_task_yield_if_runnable();
  }
}

static bool reserve_job(void)
{
  if (storage_draining()) {
    return false;
  }
  size_t count = atomic_load_explicit(&admitted, memory_order_relaxed);
  while (count < NPFS_REQUEST_LIMIT) {
    if (atomic_compare_exchange_weak_explicit(&admitted, &count, count + 1,
        memory_order_relaxed, memory_order_relaxed)) {
      if (storage_draining()) {
        KASSERT(atomic_fetch_sub_explicit(&admitted, 1, memory_order_release));
        return false;
      }
      return true;
    }
  }
  return false;
}

static void enqueue_job(struct npfs_job *job)
{
  job->state = NPFS_JOB_QUEUED;
  if (last_job) {
    last_job->next = job;
  } else {
    first_job = job;
  }
  last_job = job;
  wake_worker();
}

enum call_status npfs_submit(struct npfs_job *job)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!job || job->state != NPFS_JOB_IDLE || job->object || job->captured.address ||
      job->captured.size || job->captured.backing_bytes || job->next ||
      job->user_request || job->completion || job->admitted ||
      (unsigned)job->operation > NPFS_RAW_APPEND_POOL) {
    return CALL_BAD_REQUEST;
  }
  if (!available) {
    return CALL_UNAVAILABLE;
  }
  if (!reserve_job()) {
    return storage_draining() ? CALL_UNAVAILABLE : CALL_BUSY;
  }
  job->admitted = true;
  if (job->fat_actor) {
    object_retain(&job->fat_actor->object);
  }
  if (job->fat_node) {
    fat32_request_published(job->fat_node);
  }
  if (job->fat_destination) {
    fat32_request_published(job->fat_destination);
  }
  job->deadline = task_deadline_after_ms(NPFS_TIMEOUT_MS);
  enqueue_job(job);
  return CALL_OK;
}

enum call_status npfs_completion_reserve(struct native_completion *operation,
    enum npfs_operation code, struct npfs_node *node)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!available) {
    return CALL_UNAVAILABLE;
  }
  enum call_status status = native_completion_charge(operation, sizeof(struct npfs_job));
  if (status != CALL_OK) {
    return status;
  }
  struct npfs_job *job = kmalloc(sizeof(*job));
  if (!job) {
    return CALL_NO_MEMORY;
  }
  if (!reserve_job()) {
    kfree(job);
    return storage_draining() ? CALL_UNAVAILABLE : CALL_BUSY;
  }
  *job = (struct npfs_job){.operation = code, .node = node,
    .rights = operation->target.rights, .completion = operation,
    .deadline = operation->deadline_ns, .admitted = true};
  operation->backend = job;
  return CALL_OK;
}

void npfs_completion_start(struct native_completion *operation)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct npfs_job *job = operation->backend;
  KASSERT(job && job->state == NPFS_JOB_IDLE && job->admitted);
  if (job->fat_actor) {
    KASSERT(object_retain(&job->fat_actor->object));
  }
  if (job->fat_node) {
    fat32_request_published(job->fat_node);
  }
  enqueue_job(job);
}

void npfs_completion_discard(struct native_completion *operation)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct npfs_job *job = operation->backend;
  if (!job) {
    return;
  }
  KASSERT(job->state == NPFS_JOB_IDLE && job->admitted);
  KASSERT(atomic_fetch_sub_explicit(&admitted, 1, memory_order_relaxed));
  job->status = CALL_ABANDONED;
  file_transfer_finish(job);
  operation->backend = NULL;
  kfree(job);
  wake_worker();
}

struct npfs_request *npfs_request_prepare(enum npfs_operation operation_code)
{
  struct npfs_request *request = (void *)bsp_request_prepare(BSP_SERVICE_NPFS);
  request->job.operation = operation_code;
  request->job.user_request = request;
  return request;
}

void npfs_request_submit_and_wait(struct npfs_request *request)
{
  bsp_request_submit_and_wait(&request->request);
}

void npfs_request_release(struct npfs_request *request)
{
  KASSERT(!request->job.raw && !request->job.node && !request->job.destination && !request->job.fat_node &&
      !request->job.fat_destination && !request->job.fat_actor &&
      !request->job.reservation.table && !request->job.grant.object &&
      !request->job.object && !request->job.control && !request->job.captured.address &&
      !request->job.captured.size && !request->job.captured.backing_bytes &&
      !request->job.next && !request->job.user_request && !request->job.admitted &&
      !request->job.transfer_source.object && !request->job.transfer_destination &&
      !request->job.transfer_memory &&
      !request->job.transfer && !request->job.transfer_data);
  bsp_request_release(&request->request);
}

void npfs_request_finish_fat32_begin(struct npfs_request *request, bool delivered)
{
  KASSERT(request->job.operation == NPFS_FAT_BEGIN && request->job.status == CALL_OK &&
      request->job.admitted && request->job.fat_node && request->job.fat_actor);
  struct fat32_node *node = request->job.fat_node;
  struct process_control *actor = request->job.fat_actor;
  uint64_t id = request->job.interval_id;
  request->job.fat_node = NULL;
  request->job.fat_actor = NULL;
  request->job.admitted = false;
  npfs_request_release(request);
  /* Transfer the existing admission and references; no second capacity claim. */
  request = npfs_request_prepare(NPFS_FAT_FINISH_BEGIN);
  request->job.admitted = true;
  request->job.fat_node = node;
  request->job.fat_actor = actor;
  request->job.interval_id = id;
  request->job.interval_delivered = delivered;
  npfs_request_submit_and_wait(request);
  KASSERT(request->job.status == CALL_OK);
  npfs_request_release(request);
}

void npfs_request_published(struct npfs_request *request)
{
  KASSERT(request->request.state == BSP_REQUEST_PREPARED);
  if (request->job.operation == NPFS_FAT_FINISH_BEGIN) {
    KASSERT(request->job.admitted && request->job.fat_node && request->job.fat_actor);
    return;
  }
  request->job.deadline = task_deadline_after_ms(NPFS_TIMEOUT_MS);
  request->job.admitted = reserve_job();
  request->job.status = request->job.admitted ? CALL_OK :
      storage_draining() ? CALL_UNAVAILABLE : CALL_BUSY;
  if (request->job.fat_actor) {
    object_retain(&request->job.fat_actor->object);
  }
  if (request->job.admitted && request->job.fat_node) {
    fat32_request_published(request->job.fat_node);
  }
  if (request->job.admitted && request->job.fat_destination) {
    fat32_request_published(request->job.fat_destination);
  }
}

void npfs_forward(struct npfs_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED);
  if (!request->job.admitted || !available) {
    request->job.status = available ? request->job.status : CALL_UNAVAILABLE;
    complete_job(&request->job);
    return;
  }
  enqueue_job(&request->job);
}

void npfs_start(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!available);
  enum mm_result result = kernel_task_create(npfs_worker, NULL);
  if (result != MM_OK) {
    kerror("npfs: cannot create worker (error %u)\n", (unsigned)result);
    return;
  }
  available = true;
}

bool npfs_device_mounted(block_device_id device)
{
  return storage_extent_device_mounted(device);
}

bool npfs_partition_mounted(block_device_id device, uint32_t partition)
{
  npfs_require_worker();
  struct gpt_partition selected = {0};
  uint64_t flags = cpu_save_interrupts();
  const struct gpt_snapshot *snapshot = gpt_get_snapshot(device);
  for (size_t i = 0; snapshot && i < snapshot->partition_count; ++i) {
    if (snapshot->partitions[i].entry_number == partition) {
      selected = snapshot->partitions[i];
      break;
    }
  }
  cpu_restore_interrupts(flags);
  return selected.block_count && storage_extent_range_mounted(device,
      selected.first_block, selected.block_count);
}

enum call_status npfs_shutdown_flush(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && (flags & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!kernel_task_is_current(npfs_worker, NULL));
  if (!available) {
    cpu_restore_interrupts(flags);
    return CALL_OK;
  }
  KASSERT(!shutdown_requested && !storage_sealed());
  atomic_store_explicit(&storage_admission, STORAGE_DRAINING, memory_order_release);
  shutdown_requested = true;
  shutdown_done = false;
  wake_worker();
  while (!shutdown_done) {
    struct task_wait *wait = task_wait_prepare();
    shutdown_wait = wait;
    task_wait_sleep(wait);
    shutdown_wait = NULL;
  }
  enum call_status status = shutdown_status;
  cpu_restore_interrupts(flags);
  return status;
}

void npfs_shutdown_cancel(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  atomic_store_explicit(&storage_admission, STORAGE_RUNNING, memory_order_release);
  wake_worker();
}

void npfs_notify(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  wake_worker();
}
