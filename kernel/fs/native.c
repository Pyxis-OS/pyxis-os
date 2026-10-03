#include <arch/cpu.h>
#include <arch/smp.h>
#include <abi/launcher.h>
#include <kernel/fs/native.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pressure.h>
#include <kernel/object/clock.h>
#include <kernel/object/object.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/capability.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel-config.h>
#include "native_store.h"

#define NATIVEFS_MAINTENANCE_MS 1000u

struct nativefs_pool {
  struct nativefs_pool *next;
  struct gpt_partition partition;
  struct gpt_guid disk;
  struct native_store_pool *store;
  enum call_status maintenance_error;
  bool gpt_degraded;
};

struct nativefs_node {
  struct nativefs_node *next;
  struct native_store_inode *inode;
  struct execution_group *cleanup_group;
  uint64_t rights, kind;
  union {
    struct directory_object directory;
    struct file_object file;
  } wrapper;
};

/* The worker owns pool, inode and cache state. Only queue/reference transfers
 * run with IF=0. No lock, allocation section or user pointer spans disk waits. */
static struct nativefs_pool *pools;
static struct nativefs_job *first_job, *last_job;
static struct nativefs_node *retired_nodes;
static struct task_wait *worker_wait;
static bool available;
static atomic_size_t admitted;
static size_t wrapper_count, adapter_used;

static void nativefs_worker(void *argument);

void pnf_memory_copy(void *destination, const void *source, size_t length)
{
  memcpy(destination, source, length);
}

void pnf_memory_zero(void *destination, size_t length)
{
  memset(destination, 0, length);
}

void nativefs_require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && kernel_task_is_current(nativefs_worker, NULL));
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

static enum call_status adapter_allocate(size_t bytes, void **out)
{
  if (bytes > NATIVEFS_ADAPTER_BYTES - adapter_used ||
      wrapper_count == NATIVEFS_WRAPPER_LIMIT) {
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

static void adapter_free(void *data, size_t bytes)
{
  KASSERT(bytes <= adapter_used && wrapper_count);
  adapter_used -= bytes;
  --wrapper_count;
  uint64_t flags = cpu_save_interrupts();
  kfree(data);
  cpu_restore_interrupts(flags);
}

static struct native_store_context context_for(uint64_t deadline)
{
  struct native_store_context context = {.deadline = deadline};
  context.time_valid = clock_wall_nanoseconds(&context.time_ns);
  return context;
}

static enum call_status select_partition(struct native_store_context *context,
    const struct nativefs_job *job, struct gpt_partition *partition,
    struct block_info *device, bool *degraded)
{
  const struct gpt_snapshot *snapshot;
  for (;;) {
    if (task_deadline_expired(context->deadline)) {
      return CALL_TIMED_OUT;
    }
    uint64_t flags = cpu_save_interrupts();
    snapshot = gpt_get_snapshot();
    cpu_restore_interrupts(flags);
    if (snapshot) {
      break;
    }
    uint64_t retry = task_deadline_after_ms(1);
    kernel_task_sleep_until(retry < context->deadline ? retry : context->deadline);
  }
  switch (snapshot->status) {
  case GPT_HEALTHY:
  case GPT_DEGRADED: break;
  case GPT_UNAVAILABLE:
  case GPT_UNSUPPORTED: return CALL_UNAVAILABLE;
  case GPT_NO_MEMORY: return CALL_NO_MEMORY;
  case GPT_TIMED_OUT: return CALL_TIMED_OUT;
  default: return CALL_IO;
  }
  if (memcmp(&job->disk, &snapshot->disk_guid, sizeof(job->disk))) {
    return CALL_NOT_FOUND;
  }
  bool found = false;
  for (size_t i = 0; i < snapshot->partition_count; ++i) {
    if (snapshot->partitions[i].entry_number == job->partition) {
      *partition = snapshot->partitions[i];
      found = true;
      break;
    }
  }
  if (!found) {
    return CALL_NOT_FOUND;
  }
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(device);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    context->backing_error = result;
    return result == BLOCK_TIMED_OUT ? CALL_TIMED_OUT : CALL_UNAVAILABLE;
  }
  if (device->block_size != snapshot->block_size ||
      device->block_count != snapshot->disk_blocks ||
      (device->block_size != 512 && device->block_size != PNF_BLOCK_SIZE) ||
      device->max_transfer < device->block_size) {
    return CALL_UNAVAILABLE;
  }
  if (!partition->block_count || partition->first_block >= device->block_count ||
      partition->block_count > device->block_count - partition->first_block) {
    return CALL_IO;
  }
  *degraded = snapshot->status == GPT_DEGRADED;
  return CALL_OK;
}

static bool mutation_rights(uint64_t rights)
{
  return rights & (DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_WRITE_FILES | DIRECTORY_RIGHT_REMOVE);
}

static enum call_status open_pool(struct native_store_context *context,
    const struct nativefs_job *job, struct nativefs_pool **out)
{
  struct gpt_partition partition;
  struct block_info device;
  bool degraded;
  enum call_status status = select_partition(context, job, &partition, &device, &degraded);
  if (status != CALL_OK) {
    return status;
  }
  bool writable = mutation_rights(job->rights);
  for (struct nativefs_pool *pool = pools; pool; pool = pool->next) {
    if (!memcmp(&pool->disk, &job->disk, sizeof(job->disk)) &&
        pool->partition.entry_number == partition.entry_number &&
        pool->partition.first_block == partition.first_block &&
        pool->partition.block_count == partition.block_count) {
      if (writable) {
        status = native_store_upgrade(context, pool->store);
      }
      if (status == CALL_OK) {
        *out = pool;
      }
      return status;
    }
  }
  struct nativefs_pool *pool;
  status = adapter_allocate(sizeof(*pool), (void **)&pool);
  if (status != CALL_OK) {
    return status;
  }
  status = native_store_open(context, &partition, &device, writable, &pool->store);
  if (status != CALL_OK) {
    adapter_free(pool, sizeof(*pool));
    return status;
  }
  pool->partition = partition;
  pool->disk = job->disk;
  pool->gpt_degraded = degraded;
  pool->next = pools;
  pools = pool;
  *out = pool;
  return CALL_OK;
}

static struct kernel_object *node_object(struct nativefs_node *node)
{
  return node->kind == DIRECTORY_KIND_DIRECTORY ?
      &node->wrapper.directory.object : &node->wrapper.file.object;
}

static enum call_status allocate_node(uint64_t kind, uint64_t rights,
    struct nativefs_node **out)
{
  struct nativefs_node *node;
  enum call_status status = adapter_allocate(sizeof(*node), (void **)&node);
  if (status != CALL_OK) {
    return status;
  }
  node->kind = kind;
  node->rights = rights;
  uint64_t flags = cpu_save_interrupts();
  if (kind == DIRECTORY_KIND_DIRECTORY) {
    directory_init_native(&node->wrapper.directory, node);
  } else {
    file_init_native(&node->wrapper.file, node);
  }
  cpu_restore_interrupts(flags);
  *out = node;
  return CALL_OK;
}

static enum call_status wrap_inode(struct native_store_inode *inode, uint64_t rights,
    struct kernel_object **out)
{
  struct nativefs_node *node;
  enum call_status status = allocate_node(native_store_kind(inode), rights, &node);
  if (status != CALL_OK) {
    native_store_release(inode);
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

void nativefs_retire(struct nativefs_node *node)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  node->cleanup_group = object_cleanup_defer();
  node->next = retired_nodes;
  retired_nodes = node;
  wake_worker();
}

static void destroy_nodes(struct nativefs_node *list)
{
  while (list) {
    struct nativefs_node *node = list;
    list = node->next;
    /* Dirty state belongs to the pool and outlives this process's wrapper.
     * Final release may enable unlink cleanup, but never promises durability. */
    if (node->inode) {
      native_store_release(node->inode);
    }
    struct execution_group *group = node->cleanup_group;
    adapter_free(node, sizeof(*node));
    if (group) {
      uint64_t flags = cpu_save_interrupts();
      execution_group_cleanup_end(group);
      cpu_restore_interrupts(flags);
    }
  }
}

static enum call_status check_name(const char *name, size_t count)
{
  if (count > PNF_NAME_MAX) {
    return CALL_LIMIT;
  }
  return pnf_name_valid((const uint8_t *)name, count) ? CALL_OK : CALL_BAD_REQUEST;
}

static enum call_status acquire_root(struct native_store_context *context, struct nativefs_job *job)
{
  enum call_status status = check_name(job->name, job->count);
  if (status != CALL_OK || !job->partition || (job->rights & ~DIRECTORY_RIGHTS) ||
      !(job->rights & DIRECTORY_RIGHT_LOOKUP)) {
    return status != CALL_OK ? status : CALL_BAD_REQUEST;
  }
  struct nativefs_pool *pool;
  status = open_pool(context, job, &pool);
  if (status != CALL_OK) {
    return status;
  }
  struct native_store_inode *inode;
  status = native_store_root(context, pool->store, job->name, job->count, &inode);
  return status == CALL_OK ? wrap_inode(inode, job->rights, &job->object) : status;
}

static enum call_status child(struct native_store_context *context, struct nativefs_job *job)
{
  if (job->kind != DIRECTORY_KIND_DIRECTORY && job->kind != DIRECTORY_KIND_FILE) {
    return CALL_BAD_REQUEST;
  }
  uint64_t mask = job->kind == DIRECTORY_KIND_DIRECTORY ? DIRECTORY_RIGHTS : FILE_RIGHTS;
  if (job->child_rights & ~mask) {
    return CALL_BAD_REQUEST;
  }
  uint64_t allowed = job->kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
      ((job->rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0) |
      ((job->rights & DIRECTORY_RIGHT_WRITE_FILES) ? FILE_RIGHT_WRITE : 0);
  if (job->child_rights & ~allowed) {
    return CALL_DENIED;
  }
  enum call_status status = check_name(job->name, job->count);
  if (status != CALL_OK) {
    return status;
  }
  struct native_store_inode *inode;
  if (job->operation == NATIVEFS_LOOKUP) {
    status = native_store_lookup(context, job->node->inode, job->name, job->count, &inode);
    if (status != CALL_OK) {
      return status;
    }
    if (native_store_kind(inode) != job->kind) {
      native_store_release(inode);
      return CALL_WRONG_TYPE;
    }
    return wrap_inode(inode, job->child_rights, &job->object);
  }
  if (!job->table) {
    return CALL_BAD_REQUEST;
  }
  struct nativefs_node *node;
  status = allocate_node(job->kind, job->child_rights, &node);
  if (status != CALL_OK) {
    return status;
  }
  uint64_t flags = cpu_save_interrupts();
  enum capability_result installed = capability_install(job->table, node_object(node),
      job->child_rights, 0, &job->handle);
  cpu_restore_interrupts(flags);
  if (installed == CAP_OK) {
    /* The caller lends its table and cannot observe this staged handle. */
    status = native_store_create(context, job->node->inode, job->name, job->count,
        job->kind, &inode);
    if (status == CALL_OK) {
      node->inode = inode;
    } else {
      flags = cpu_save_interrupts();
      KASSERT(capability_close(job->table, job->handle) == CAP_OK);
      cpu_restore_interrupts(flags);
      job->handle = HANDLE_INVALID;
    }
  } else {
    KASSERT(installed == CAP_NO_MEMORY || installed == CAP_LIMIT);
    status = installed == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
  }
  flags = cpu_save_interrupts();
  object_release(node_object(node));
  cpu_restore_interrupts(flags);
  return status;
}

static enum call_status enumerate_node(struct native_store_context *context,
    struct nativefs_job *job)
{
  if (!job->cursor.generation && job->cursor.position) {
    return CALL_BAD_REQUEST;
  }
  struct pnf_dirent entry = {0};
  uint64_t position, generation;
  enum call_status status = native_store_enumerate(context, job->node->inode,
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
    struct native_store_inode *child_inode;
    status = native_store_lookup(context, job->node->inode, (const char *)entry.name,
        entry.name_length, &child_inode);
    if (status != CALL_OK) {
      return status;
    }
    reply.kind = native_store_kind(child_inode);
    native_store_release(child_inode);
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

static enum call_status capture_file(struct native_store_context *context, struct nativefs_job *job)
{
  uint64_t size = native_store_size(job->node->inode);
  if (!size || size > LAUNCH_EXTERNAL_IMAGE_MAX_SIZE) {
    return size ? CALL_LIMIT : CALL_BAD_REQUEST;
  }
  uint64_t flags = cpu_save_interrupts();
  void *bytes = kmalloc(size);
  cpu_restore_interrupts(flags);
  if (!bytes) {
    return CALL_NO_MEMORY;
  }
  size_t read = 0;
  enum call_status status = native_store_read(context, job->node->inode, 0, bytes, size, &read);
  if (status == CALL_OK && read != size) {
    status = CALL_IO;
  }
  if (status != CALL_OK) {
    flags = cpu_save_interrupts();
    kfree(bytes);
    cpu_restore_interrupts(flags);
    return status;
  }
  job->captured = bytes;
  job->count = size;
  return CALL_OK;
}

static enum call_status perform(struct native_store_context *context, struct nativefs_job *job)
{
  if (task_deadline_expired(context->deadline)) {
    return CALL_TIMED_OUT;
  }
  if (job->operation == NATIVEFS_ROOT) {
    return acquire_root(context, job);
  }
  if (job->operation == NATIVEFS_DISK_SYNC) {
    enum call_status result = CALL_OK;
    for (struct nativefs_pool *pool = pools; pool; pool = pool->next) {
      if (memcmp(&job->disk, &pool->disk, sizeof(job->disk))) {
        continue;
      }
      enum call_status status = native_store_sync(context, pool->store);
      if (result == CALL_OK) {
        result = status;
      }
    }
    return result;
  }
  if (!job->node || !job->node->inode) {
    return CALL_BAD_REQUEST;
  }
  struct nativefs_node *node = job->node;
  bool directory = node->kind == DIRECTORY_KIND_DIRECTORY;
  uint64_t all = directory ? DIRECTORY_RIGHTS : FILE_RIGHTS;
  if (job->rights & ~all) {
    return CALL_BAD_REQUEST;
  }
  if (job->rights & ~node->rights) {
    return CALL_DENIED;
  }
  uint64_t required;
  switch (job->operation) {
  case NATIVEFS_LOOKUP: required = DIRECTORY_RIGHT_LOOKUP; break;
  case NATIVEFS_ENUMERATE: required = DIRECTORY_RIGHT_ENUMERATE; break;
  case NATIVEFS_CREATE: required = DIRECTORY_RIGHT_CREATE; break;
  case NATIVEFS_REMOVE:
  case NATIVEFS_RENAME: required = DIRECTORY_RIGHT_REMOVE; break;
  case NATIVEFS_FILESYSTEM_INFO: required = DIRECTORY_RIGHT_FILESYSTEM_INFO; break;
  case NATIVEFS_READ:
  case NATIVEFS_SIZE:
  case NATIVEFS_CAPTURE: required = FILE_RIGHT_READ; break;
  case NATIVEFS_WRITE:
  case NATIVEFS_RESIZE: required = FILE_RIGHT_WRITE; break;
  case NATIVEFS_SYNC: required = directory ? DIRECTORY_RIGHT_CREATE | DIRECTORY_RIGHT_REMOVE :
      FILE_RIGHT_WRITE; break;
  default: return CALL_BAD_OPERATION;
  }
  bool directory_operation = job->operation == NATIVEFS_LOOKUP ||
      job->operation == NATIVEFS_ENUMERATE || job->operation == NATIVEFS_CREATE ||
      job->operation == NATIVEFS_REMOVE || job->operation == NATIVEFS_RENAME ||
      job->operation == NATIVEFS_FILESYSTEM_INFO;
  if ((directory_operation && !directory) ||
      (!directory_operation && job->operation != NATIVEFS_SYNC && directory)) {
    return CALL_WRONG_TYPE;
  }
  if (!(job->rights & required)) {
    return CALL_DENIED;
  }
  switch (job->operation) {
  case NATIVEFS_LOOKUP:
  case NATIVEFS_CREATE: return child(context, job);
  case NATIVEFS_ENUMERATE: return enumerate_node(context, job);
  case NATIVEFS_READ: {
    if (job->count > sizeof(job->data)) {
      return CALL_BAD_REQUEST;
    }
    size_t count;
    enum call_status status = native_store_read(context, node->inode, job->offset,
        job->data, job->count, &count);
    if (status == CALL_OK) {
      job->count = count;
    }
    return status;
  }
  case NATIVEFS_SIZE:
    job->offset = native_store_size(node->inode);
    return CALL_OK;
  case NATIVEFS_CAPTURE: return capture_file(context, job);
  case NATIVEFS_WRITE: {
    if (job->count > FILE_WRITE_MAX_BYTES) {
      return CALL_BAD_REQUEST;
    }
    size_t count;
    enum call_status status = native_store_write(context, node->inode, job->offset,
        job->data, job->count, &count);
    if (status == CALL_OK) {
      job->count = count;
    }
    return status;
  }
  case NATIVEFS_RESIZE: return native_store_resize(context, node->inode, job->offset);
  case NATIVEFS_REMOVE: {
    enum call_status status = check_name(job->name, job->count);
    return status == CALL_OK ? native_store_remove(context, node->inode, job->name,
        job->count, job->kind) : status;
  }
  case NATIVEFS_RENAME: {
    struct nativefs_node *destination = job->destination;
    if (!destination || destination->kind != DIRECTORY_KIND_DIRECTORY) {
      return CALL_WRONG_TYPE;
    }
    if ((job->destination_rights & ~DIRECTORY_RIGHTS) ||
        (job->destination_rights & ~destination->rights) ||
        !(job->destination_rights & DIRECTORY_RIGHT_CREATE) ||
        (job->replace && !(job->destination_rights & DIRECTORY_RIGHT_REMOVE))) {
      return CALL_DENIED;
    }
    if (!native_store_same_volume(node->inode, destination->inode)) {
      return CALL_BAD_OPERATION;
    }
    enum call_status status = check_name(job->name, job->count);
    if (status == CALL_OK) {
      status = check_name(job->destination_name, job->destination_length);
    }
    return status == CALL_OK ? native_store_rename(context, node->inode, job->name,
        job->count, destination->inode, job->destination_name, job->destination_length,
        job->replace) : status;
  }
  case NATIVEFS_SYNC:
    return native_store_sync(context, native_store_inode_pool(node->inode));
  case NATIVEFS_FILESYSTEM_INFO:
    native_store_info(node->inode, &job->info);
    for (struct nativefs_pool *pool = pools; pool; pool = pool->next) {
      if (pool->store == native_store_inode_pool(node->inode) && pool->gpt_degraded) {
        job->info.flags |= FILESYSTEM_FLAG_GPT_DEGRADED;
      }
    }
    return CALL_OK;
  default: return CALL_BAD_OPERATION;
  }
}

static void complete_job(struct nativefs_job *job)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct nativefs_request *request = job->user_request;
  job->user_request = NULL;
  job->node = NULL;
  job->destination = NULL;
  job->table = NULL;
  job->next = NULL;
  if (job->admitted) {
    KASSERT(atomic_fetch_sub_explicit(&admitted, 1, memory_order_relaxed));
    job->admitted = false;
  }
  job->state = NATIVEFS_JOB_COMPLETE;
  if (request) {
    bsp_request_complete(&request->request);
  }
}

static bool maintenance_pending(void)
{
  for (struct nativefs_pool *pool = pools; pool; pool = pool->next) {
    if (pool->maintenance_error == CALL_OK && native_store_pending(pool->store)) {
      return true;
    }
  }
  return false;
}

static void nativefs_worker(void *argument)
{
  (void)argument;
  uint64_t next_flush = task_deadline_after_ms(CONFIG_NATIVEFS_FLUSH_SECONDS * 1000u);
  uint64_t next_maintenance = task_deadline_after_ms(NATIVEFS_MAINTENANCE_MS);
  for (;;) {
    bool pending = maintenance_pending();
    uint64_t flags = cpu_save_interrupts();
    bool pressure = mm_pressure_take();
    struct nativefs_node *nodes = retired_nodes;
    retired_nodes = NULL;
    struct nativefs_job *job = first_job;
    if (job) {
      first_job = job->next;
      if (!first_job) {
        last_job = NULL;
      }
      job->next = NULL;
      job->state = NATIVEFS_JOB_ACTIVE;
    }
    bool flush = task_deadline_expired(next_flush);
    bool maintenance = pressure || flush || task_deadline_expired(next_maintenance) ||
        pending;
    if (!nodes && !job && !maintenance) {
      KASSERT(!worker_wait);
      struct task_wait *wait = task_wait_prepare();
      worker_wait = wait;
      mm_pressure_wait(wait);
      task_wait_sleep_until(wait, next_flush < next_maintenance ? next_flush : next_maintenance);
      mm_pressure_wait(NULL);
      worker_wait = NULL;
      cpu_restore_interrupts(flags);
      continue;
    }
    cpu_restore_interrupts(flags);
    destroy_nodes(nodes);
    if (job) {
      flags = cpu_save_interrupts();
      struct execution_group *previous = object_cleanup_enter(job->user_request ?
          job->user_request->request.cleanup_group : NULL);
      cpu_restore_interrupts(flags);
      struct native_store_context context = context_for(job->deadline);
      job->status = perform(&context, job);
      job->format_status = context.format_error;
      job->backing_error = context.backing_error;
      flags = cpu_save_interrupts();
      if (job->status != CALL_OK) {
        if (job->object) {
          object_release(job->object);
          job->object = NULL;
        }
        kfree(job->captured);
        job->captured = NULL;
      }
      object_cleanup_leave(previous);
      complete_job(job);
      cpu_restore_interrupts(flags);
    }
    if (maintenance) {
      for (struct nativefs_pool *pool = pools; pool; pool = pool->next) {
        struct native_store_context context = context_for(task_deadline_after_ms(NATIVEFS_TIMEOUT_MS));
        enum call_status status = native_store_maintain(&context, pool->store, flush, pressure);
        if (status != CALL_OK && status != pool->maintenance_error) {
          klog("nativefs: background writeback/cleanup failed (status %u); pool retained\n",
              (unsigned)status);
        }
        pool->maintenance_error = status;
      }
      next_maintenance = task_deadline_after_ms(NATIVEFS_MAINTENANCE_MS);
      if (flush) {
        next_flush = task_deadline_after_ms(CONFIG_NATIVEFS_FLUSH_SECONDS * 1000u);
      }
    }
    kernel_task_yield_if_runnable();
  }
}

static bool reserve_job(void)
{
  size_t count = atomic_load_explicit(&admitted, memory_order_relaxed);
  while (count < NATIVEFS_REQUEST_LIMIT) {
    if (atomic_compare_exchange_weak_explicit(&admitted, &count, count + 1,
        memory_order_relaxed, memory_order_relaxed)) {
      return true;
    }
  }
  return false;
}

static void enqueue_job(struct nativefs_job *job)
{
  job->state = NATIVEFS_JOB_QUEUED;
  if (last_job) {
    last_job->next = job;
  } else {
    first_job = job;
  }
  last_job = job;
  wake_worker();
}

enum call_status nativefs_submit(struct nativefs_job *job)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!job || job->state != NATIVEFS_JOB_IDLE || job->object || job->captured || job->next ||
      job->user_request || job->admitted || (unsigned)job->operation > NATIVEFS_DISK_SYNC) {
    return CALL_BAD_REQUEST;
  }
  if (!available) {
    return CALL_UNAVAILABLE;
  }
  if (!reserve_job()) {
    return CALL_BUSY;
  }
  job->admitted = true;
  job->deadline = task_deadline_after_ms(NATIVEFS_TIMEOUT_MS);
  enqueue_job(job);
  return CALL_OK;
}

struct nativefs_request *nativefs_request_prepare(enum nativefs_operation operation_code)
{
  struct nativefs_request *request = (void *)bsp_request_prepare(BSP_SERVICE_NATIVEFS);
  request->job.operation = operation_code;
  request->job.user_request = request;
  return request;
}

void nativefs_request_submit_and_wait(struct nativefs_request *request)
{
  bsp_request_submit_and_wait(&request->request);
}

void nativefs_request_release(struct nativefs_request *request)
{
  KASSERT(!request->job.node && !request->job.destination && !request->job.table && !request->job.object && !request->job.captured && !request->job.next &&
      !request->job.user_request && !request->job.admitted);
  bsp_request_release(&request->request);
}

void nativefs_request_published(struct nativefs_request *request)
{
  KASSERT(request->request.state == BSP_REQUEST_PREPARED);
  request->job.deadline = task_deadline_after_ms(NATIVEFS_TIMEOUT_MS);
  request->job.admitted = reserve_job();
}

void nativefs_forward(struct nativefs_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED);
  if (!request->job.admitted || !available) {
    request->job.status = available ? CALL_BUSY : CALL_UNAVAILABLE;
    complete_job(&request->job);
    return;
  }
  enqueue_job(&request->job);
}

void nativefs_start(void)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!available);
  enum mm_result result = kernel_task_create(nativefs_worker, NULL);
  if (result != MM_OK) {
    klog("nativefs: cannot create worker (error %u)\n", (unsigned)result);
    return;
  }
  available = true;
}
