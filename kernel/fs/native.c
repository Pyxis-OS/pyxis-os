#include <arch/cpu.h>
#include <arch/smp.h>
#include <abi/launcher.h>
#include <kernel/fs/native.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/object.h>
#include <kernel/object/directory.h>
#include <kernel/object/file.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/task.h>

struct nativefs_pool {
  struct nativefs_pool *next;
  struct gpt_partition partition;
  struct block_info device;
  struct pfs_block_reader reader;
  struct pfs_pool core;
  struct pfs_pool_diagnostic diagnostic;
  size_t volumes;
  bool gpt_degraded;
};

struct nativefs_volume {
  struct nativefs_volume *next, *retired_next;
  struct nativefs_pool *pool;
  struct pfs_volume core;
  struct pfs_volume_record record;
  size_t references;
  struct execution_group *cleanup_group;
};

struct nativefs_node {
  struct nativefs_node *next;
  struct nativefs_volume *volume;
  struct pfs_view *view;
  struct execution_group *cleanup_group;
  uint64_t identity, rights, kind;
  union {
    struct directory_object directory;
    struct file_object file;
  } wrapper;
};

struct nativefs_context {
  uint64_t deadline;
  enum block_result backing_error;
};

struct core_allocation_header {
  void *base;
  size_t bytes;
};

/* Core state belongs solely to the worker. Queue/reference fields are BSP/IF=0;
 * no lock or allocator section spans a wait. The operation pointer is borrowed
 * only during one job and is cleared before returning ownership. */
static struct pfs_memory core_memory;
static struct nativefs_context *operation;
static struct nativefs_pool *pools;
static struct nativefs_volume *volumes, *retired;
static struct nativefs_job *first_job, *last_job;
static struct nativefs_node *retired_nodes;
static uint64_t next_identity;
static struct task_wait *worker_wait;
static bool available;
static atomic_size_t admitted;
static size_t wrapper_count, adapter_used, adapter_peak;
static size_t core_peak, core_heap_used, core_heap_peak;

static void nativefs_worker(void *argument);

static void require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && kernel_task_is_current(nativefs_worker, NULL));
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

static void *core_allocate(void *context, size_t size, size_t alignment)
{
  (void)context;
  require_worker();
  if (alignment < alignof(struct core_allocation_header)) {
    alignment = alignof(struct core_allocation_header);
  }
  if (alignment - 1 > SIZE_MAX - sizeof(struct core_allocation_header) ||
      size > SIZE_MAX - sizeof(struct core_allocation_header) - (alignment - 1)) {
    return NULL;
  }
  size_t bytes = size + sizeof(struct core_allocation_header) + alignment - 1;
  uint64_t flags = cpu_save_interrupts();
  void *base = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  if (!base) {
    return NULL;
  }
  uintptr_t address = ((uintptr_t)base + sizeof(struct core_allocation_header) +
      alignment - 1) & ~(uintptr_t)(alignment - 1);
  struct core_allocation_header *header = (void *)address;
  header[-1] = (struct core_allocation_header){base, bytes};
  core_heap_used += bytes;
  if (core_heap_used > core_heap_peak) {
    core_heap_peak = core_heap_used;
  }
  if (core_memory.used + size > core_peak) {
    core_peak = core_memory.used + size;
  }
  return (void *)address;
}

static void core_free(void *context, void *data, size_t size, size_t alignment)
{
  (void)context;
  (void)size;
  (void)alignment;
  require_worker();
  struct core_allocation_header *header = data;
  struct core_allocation_header allocation = header[-1];
  KASSERT(allocation.bytes <= core_heap_used);
  core_heap_used -= allocation.bytes;
  uint64_t flags = cpu_save_interrupts();
  kfree(allocation.base);
  cpu_restore_interrupts(flags);
}

static enum call_status adapter_allocate(size_t bytes, bool wrapper, void **out)
{
  require_worker();
  if (bytes > NATIVEFS_ADAPTER_BYTES - adapter_used ||
      (wrapper && wrapper_count == NATIVEFS_WRAPPER_LIMIT)) {
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
  wrapper_count += wrapper;
  if (adapter_used > adapter_peak) {
    adapter_peak = adapter_used;
  }
  *out = data;
  return CALL_OK;
}

static void adapter_free(void *data, size_t bytes, bool wrapper)
{
  require_worker();
  KASSERT(bytes <= adapter_used && (!wrapper || wrapper_count));
  adapter_used -= bytes;
  wrapper_count -= wrapper;
  uint64_t flags = cpu_save_interrupts();
  kfree(data);
  cpu_restore_interrupts(flags);
}

static enum pfs_status backing_failure(enum block_result result)
{
  KASSERT(operation && result != BLOCK_OK);
  if (operation->backing_error == BLOCK_OK) {
    operation->backing_error = result;
  }
  return PFS_IO;
}

static bool expired(void)
{
  KASSERT(operation);
  if (!task_deadline_expired(operation->deadline)) {
    return false;
  }
  backing_failure(BLOCK_TIMED_OUT);
  return true;
}

static enum pfs_status read_blocks(void *context, uint64_t first,
    uint32_t count, void *buffer)
{
  require_worker();
  struct nativefs_pool *pool = context;
  KASSERT(operation);
  if (!buffer || !count || count > PFS_IO_BLOCKS_MAX ||
      first >= pool->reader.geometry.block_count ||
      count > pool->reader.geometry.block_count - first) {
    return backing_failure(BLOCK_INVALID);
  }
  uint64_t sectors_per_block = PFS_BLOCK_SIZE / pool->device.block_size;
  /* Geometry creation proved that the complete extent fits device geometry.
   * Checking relative bounds before multiplication also excludes overflow. */
  uint64_t sector = pool->partition.first_block + first * sectors_per_block;
  uint32_t remaining = count * sectors_per_block;
  uint8_t *destination = buffer;
  while (remaining) {
    if (expired()) {
      return PFS_IO;
    }
    uint32_t transfer = pool->device.max_transfer / pool->device.block_size;
    if (transfer > remaining) {
      transfer = remaining;
    }
    struct block_ticket ticket;
    uint64_t flags = cpu_save_interrupts();
    enum block_result result = block_submit(BLOCK_READ, sector, transfer, NULL, &ticket);
    cpu_restore_interrupts(flags);
    if (result == BLOCK_FULL) {
      uint64_t retry = task_deadline_after_ms(1);
      kernel_task_sleep_until(retry < operation->deadline ? retry : operation->deadline);
      continue;
    }
    if (result != BLOCK_OK) {
      return backing_failure(result);
    }
    result = block_wait(&ticket, operation->deadline);
    flags = cpu_save_interrupts();
    if (result != BLOCK_OK) {
      KASSERT(block_abandon(&ticket) == BLOCK_OK);
      cpu_restore_interrupts(flags);
      return backing_failure(result);
    }
    struct block_completion completion;
    size_t bytes = (size_t)transfer * pool->device.block_size;
    result = block_collect(&ticket, destination, bytes, &completion);
    KASSERT(result == BLOCK_OK);
    cpu_restore_interrupts(flags);
    if (completion.result != BLOCK_OK) {
      return backing_failure(completion.result);
    }
    if (completion.bytes != bytes) {
      return backing_failure(BLOCK_IO_ERROR);
    }
    sector += transfer;
    remaining -= transfer;
    destination += bytes;
  }
  return expired() ? PFS_IO : PFS_OK;
}

static enum call_status core_result(enum pfs_status status)
{
  switch (status) {
  case PFS_OK: return CALL_OK;
  case PFS_INVALID:
  case PFS_EXISTS:
  case PFS_NOT_EMPTY:
  case PFS_DETACHED:
  case PFS_CHANGED:
    /* Writable-core outcomes cannot originate from this read-only adapter. */
    return CALL_IO;
  case PFS_NOT_FOUND: return CALL_NOT_FOUND;
  case PFS_ABSENT: return CALL_IO;
  case PFS_UNSUPPORTED: return CALL_UNAVAILABLE;
  case PFS_LIMIT:
  case PFS_NO_SPACE:
  case PFS_QUOTA: return CALL_LIMIT;
  case PFS_NO_MEMORY: return CALL_NO_MEMORY;
  case PFS_READ_ONLY: return CALL_READ_ONLY;
  case PFS_BUSY: return CALL_BUSY;
  case PFS_DENIED: return CALL_DENIED;
  case PFS_CORRUPT: return CALL_IO;
  case PFS_RECOVERY_REQUIRED:
  case PFS_IO:
    KASSERT(operation);
    if (operation->backing_error == BLOCK_TIMED_OUT) {
      return CALL_TIMED_OUT;
    }
    if (operation->backing_error == BLOCK_UNAVAILABLE ||
        operation->backing_error == BLOCK_UNSUPPORTED) {
      return CALL_UNAVAILABLE;
    }
    return CALL_IO;
  }
  KASSERT(false);
}

static enum call_status select_partition(const struct nativefs_job *job,
    const struct gpt_partition **partition, struct block_info *device, bool *gpt_degraded)
{
  const struct gpt_snapshot *snapshot;
  for (;;) {
    if (expired()) {
      return CALL_TIMED_OUT;
    }
    uint64_t flags = cpu_save_interrupts();
    snapshot = gpt_get_snapshot();
    cpu_restore_interrupts(flags);
    if (snapshot) {
      break;
    }
    uint64_t retry = task_deadline_after_ms(1);
    kernel_task_sleep_until(retry < operation->deadline ? retry : operation->deadline);
  }
  switch (snapshot->status) {
  case GPT_HEALTHY:
  case GPT_DEGRADED: break;
  case GPT_ABSENT: return CALL_IO;
  case GPT_UNAVAILABLE:
  case GPT_UNSUPPORTED: return CALL_UNAVAILABLE;
  case GPT_NO_MEMORY: return CALL_NO_MEMORY;
  case GPT_TIMED_OUT: return CALL_TIMED_OUT;
  default: return CALL_IO;
  }
  if (memcmp(&job->disk, &snapshot->disk_guid, sizeof(job->disk))) {
    klog("nativefs: disk selector does not match GPT\n");
    return CALL_NOT_FOUND;
  }
  *partition = NULL;
  for (size_t i = 0; i < snapshot->partition_count; ++i) {
    if (snapshot->partitions[i].entry_number == job->partition) {
      *partition = &snapshot->partitions[i];
      break;
    }
  }
  if (!*partition) {
    return CALL_NOT_FOUND;
  }
  uint64_t flags = cpu_save_interrupts();
  enum block_result result = block_get_info(device);
  cpu_restore_interrupts(flags);
  if (result != BLOCK_OK) {
    return core_result(backing_failure(result));
  }
  if (device->block_size != snapshot->block_size ||
      device->block_count != snapshot->disk_blocks ||
      (device->block_size != 512 && device->block_size != PFS_BLOCK_SIZE) ||
      device->max_transfer < device->block_size) {
    return CALL_UNAVAILABLE;
  }
  if (!(*partition)->block_count || (*partition)->first_block >= device->block_count ||
      (*partition)->block_count > device->block_count - (*partition)->first_block) {
    return CALL_IO;
  }
  *gpt_degraded = snapshot->status == GPT_DEGRADED;
  return CALL_OK;
}

static bool close_pool(struct nativefs_pool *pool)
{
  KASSERT(!pool->volumes);
  enum pfs_status status = pfs_pool_close(&pool->core);
  if (status != PFS_OK) {
    /* Keep the identity, reader and memory owner alive even on BUSY. */
    klog("nativefs: pool close retained backing (core %u)\n", (unsigned)status);
    return false;
  }
  struct nativefs_pool **link = &pools;
  while (*link != pool) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = pool->next;
  adapter_free(pool, sizeof(*pool), true);
  return true;
}

static enum call_status open_pool(const struct gpt_partition *partition,
    const struct block_info *device, bool gpt_degraded, struct nativefs_pool **out,
    enum pfs_status *core_status)
{
  for (struct nativefs_pool *pool = pools; pool; pool = pool->next) {
    if (pool->partition.entry_number == partition->entry_number &&
        pool->partition.first_block == partition->first_block &&
        pool->partition.block_count == partition->block_count) {
      *out = pool;
      return CALL_OK;
    }
  }
  struct nativefs_pool *pool;
  enum call_status result = adapter_allocate(sizeof(*pool), true, (void **)&pool);
  if (result != CALL_OK) {
    return result;
  }
  pool->partition = *partition;
  pool->device = *device;
  pool->gpt_degraded = gpt_degraded;
  struct pfs_geometry geometry = {
    .block_count = partition->block_count / (PFS_BLOCK_SIZE / device->block_size),
    .max_transfer_blocks = PFS_IO_BLOCKS_MAX,
  };
  *core_status = pfs_block_reader_init(&pool->reader, pool, &geometry, read_blocks);
  if (*core_status == PFS_OK) {
    *core_status = pfs_pool_open(&pool->core, &pool->reader, &core_memory, &pool->diagnostic);
  }
  if (*core_status != PFS_OK) {
    result = core_result(*core_status);
    adapter_free(pool, sizeof(*pool), true);
    return result;
  }
  const struct pfs_pool_id *id = &pool->diagnostic.candidate[pool->diagnostic.selected].superblock.header.pool;
  for (struct nativefs_pool *other = pools; other; other = other->next) {
    const struct pfs_pool_id *other_id = &other->diagnostic.candidate[other->diagnostic.selected].superblock.header.pool;
    if (!memcmp(id, other_id, sizeof(*id))) {
      klog("nativefs: duplicate pool identity in partitions %u and %u\n",
          other->partition.entry_number, partition->entry_number);
      KASSERT(pfs_pool_close(&pool->core) == PFS_OK); /* No volume was opened. */
      adapter_free(pool, sizeof(*pool), true);
      return CALL_ALREADY_EXISTS;
    }
  }
  pool->next = pools;
  pools = pool; /* Reserve identity before volume preparation/publication. */
  *out = pool;
  return CALL_OK;
}

static enum call_status open_volume(struct nativefs_job *job, struct nativefs_volume **out)
{
  const struct gpt_partition *partition;
  struct block_info device;
  bool gpt_degraded;
  enum call_status result = select_partition(job, &partition, &device, &gpt_degraded);
  if (result != CALL_OK) {
    return result;
  }
  struct nativefs_pool *pool = NULL;
  result = open_pool(partition, &device, gpt_degraded, &pool, &job->core_status);
  if (result != CALL_OK) {
    return result;
  }
  struct pfs_volume_record *catalog;
  result = adapter_allocate(sizeof(*catalog) * PFS_VOLUME_MAX, false, (void **)&catalog);
  if (result != CALL_OK) {
    goto fail;
  }
  size_t count = 0;
  job->core_status = expired() ? PFS_IO :
      pfs_pool_diagnostic_volumes(&pool->core, catalog, PFS_VOLUME_MAX, &count);
  result = core_result(job->core_status);
  const struct pfs_volume_record *selected = NULL;
  if (result == CALL_OK) {
    result = CALL_NOT_FOUND;
    for (size_t i = 0; i < count; ++i) {
      if (catalog[i].name.length == job->count &&
          !memcmp(catalog[i].name.bytes, job->name, job->count)) {
        selected = &catalog[i];
        result = CALL_OK;
        break;
      }
    }
  }
  if (result == CALL_OK) {
    for (struct nativefs_volume *volume = volumes; volume; volume = volume->next) {
      if (volume->pool == pool && !memcmp(&volume->record.id, &selected->id, sizeof(selected->id))) {
        uint64_t flags = cpu_save_interrupts();
        if (!volume->references) {
          result = CALL_BUSY;
        } else if (volume->references == SIZE_MAX) {
          result = CALL_LIMIT;
        } else {
          ++volume->references;
          *out = volume;
        }
        cpu_restore_interrupts(flags);
        goto catalog_done;
      }
    }
    struct nativefs_volume *volume;
    result = adapter_allocate(sizeof(*volume), true, (void **)&volume);
    if (result == CALL_OK) {
      job->core_status = expired() ? PFS_IO :
          pfs_pool_diagnostic_volume_open(&pool->core, &selected->id, &volume->core);
      result = core_result(job->core_status);
      if (result == CALL_OK) {
        volume->pool = pool;
        volume->record = *selected;
        volume->references = 1;
        volume->next = volumes;
        volumes = volume;
        ++pool->volumes;
        *out = volume;
      } else {
        adapter_free(volume, sizeof(*volume), true);
      }
    }
  }
catalog_done:
  adapter_free(catalog, sizeof(*catalog) * PFS_VOLUME_MAX, false);
fail:
  if (result != CALL_OK && !pool->volumes) {
    close_pool(pool);
  }
  return result;
}

static void wake_worker(void)
{
  struct task_wait *wake = worker_wait;
  worker_wait = NULL;
  if (wake) {
    task_wait_wake(wake);
  }
}

static void nativefs_volume_put(struct nativefs_volume *volume)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(volume && volume->references);
  if (--volume->references) {
    return;
  }
  volume->cleanup_group = object_cleanup_defer();
  volume->retired_next = retired;
  retired = volume;
  wake_worker();
}

static struct nativefs_volume *retire_volumes(struct nativefs_volume *list)
{
  struct nativefs_volume *busy = NULL;
  while (list) {
    struct nativefs_volume *volume = list;
    list = volume->retired_next;
    uint64_t flags = cpu_save_interrupts();
    struct execution_group *group = volume->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(group);
    cpu_restore_interrupts(flags);
    enum pfs_status status = pfs_volume_close(&volume->core);
    if (status != PFS_OK) {
      volume->retired_next = busy;
      busy = volume;
      flags = cpu_save_interrupts();
      object_cleanup_leave(previous);
      cpu_restore_interrupts(flags);
      continue;
    }
    struct nativefs_pool *pool = volume->pool;
    KASSERT(pool->volumes);
    --pool->volumes;
    if (!pool->volumes && !close_pool(pool)) {
      /* The emptied volume wrapper carries cleanup attribution until even the
       * final pool close succeeds. Closing an already empty core is harmless. */
      ++pool->volumes;
      volume->retired_next = busy;
      busy = volume;
      flags = cpu_save_interrupts();
      object_cleanup_leave(previous);
      cpu_restore_interrupts(flags);
      continue;
    }
    struct nativefs_volume **link = &volumes;
    while (*link != volume) {
      KASSERT(*link);
      link = &(*link)->next;
    }
    *link = volume->next;
    adapter_free(volume, sizeof(*volume), true);
    flags = cpu_save_interrupts();
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    cpu_restore_interrupts(flags);
  }
  return busy;
}

static struct kernel_object *node_object(struct nativefs_node *node)
{
  return node->kind == DIRECTORY_KIND_DIRECTORY ?
      &node->wrapper.directory.object : &node->wrapper.file.object;
}

static struct pfs_rights directory_rights(uint64_t rights)
{
  /* Filesystem observation delegates no persistent object rights. */
  return (struct pfs_rights){
    .file = rights & DIRECTORY_RIGHT_READ_FILES ? PFS_FILE_READ | PFS_FILE_METADATA : 0,
    .directory = ((rights & DIRECTORY_RIGHT_LOOKUP) ? PFS_DIR_LOOKUP : 0) |
        ((rights & DIRECTORY_RIGHT_ENUMERATE) ? PFS_DIR_LIST : 0),
  };
}

/* Transfer one view and one backing reference only on success. The wrapper is
 * part of this charged allocation, including throughout deferred retirement. */
static enum call_status create_node(struct nativefs_volume *volume,
    struct pfs_view *view, uint64_t kind, uint64_t rights, struct kernel_object **out)
{
  if (kind == DIRECTORY_KIND_DIRECTORY && next_identity == UINT64_MAX) {
    return CALL_LIMIT;
  }
  struct nativefs_node *node;
  enum call_status status = adapter_allocate(sizeof(*node), true, (void **)&node);
  if (status != CALL_OK) {
    return status;
  }
  node->volume = volume;
  node->view = view;
  node->kind = kind;
  node->rights = rights;
  uint64_t flags = cpu_save_interrupts();
  if (kind == DIRECTORY_KIND_DIRECTORY) {
    node->identity = ++next_identity;
    directory_init_native(&node->wrapper.directory, node);
  } else {
    file_init_native(&node->wrapper.file, node);
  }
  cpu_restore_interrupts(flags);
  *out = node_object(node);
  return CALL_OK;
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
    uint64_t flags = cpu_save_interrupts();
    struct execution_group *group = node->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(group);
    cpu_restore_interrupts(flags);
    /* Views have no independently retained child handles. Each derived view
     * retains the volume directly, so closing this view cannot be BUSY. */
    KASSERT(pfs_view_close(&node->view, &(struct pfs_view_close_result){0}) == PFS_OK);
    flags = cpu_save_interrupts();
    nativefs_volume_put(node->volume);
    cpu_restore_interrupts(flags);
    adapter_free(node, sizeof(*node), true);
    flags = cpu_save_interrupts();
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    cpu_restore_interrupts(flags);
  }
}

static enum call_status acquire_root(struct nativefs_job *job)
{
  struct pfs_principal_id zero = {0};
  if (!job->partition || job->count > PFS_NAME_MAX ||
      pfs_name_validate((const uint8_t *)job->name, job->count) != PFS_OK ||
      !memcmp(&job->principal, &zero, sizeof(zero)) ||
      (job->rights & ~DIRECTORY_RIGHTS) || !(job->rights & DIRECTORY_RIGHT_LOOKUP)) {
    return CALL_BAD_REQUEST;
  }
  if (job->rights & ~NATIVEFS_DIRECTORY_RIGHTS) {
    return CALL_READ_ONLY;
  }
  struct nativefs_volume *volume = NULL;
  enum call_status status = open_volume(job, &volume);
  if (status != CALL_OK) {
    return status;
  }
  struct pfs_trusted_context context = {
    .principal = job->principal,
    .root = volume->record.root_object,
    .scope = PFS_SCOPE_SUBTREE,
    .ceiling = directory_rights(NATIVEFS_DIRECTORY_RIGHTS),
  };
  struct pfs_rights requested = directory_rights(job->rights);
  struct pfs_view *view = NULL;
  job->core_status = expired() ? PFS_IO : pfs_view_acquire(&volume->core,
      &context, &context.root, PFS_SCOPE_SUBTREE, &requested, &view);
  status = core_result(job->core_status);
  if (status == CALL_OK) {
    status = create_node(volume, view, DIRECTORY_KIND_DIRECTORY, job->rights, &job->object);
  }
  if (status != CALL_OK) {
    KASSERT(pfs_view_close(&view, &(struct pfs_view_close_result){0}) == PFS_OK);
    uint64_t flags = cpu_save_interrupts();
    nativefs_volume_put(volume);
    cpu_restore_interrupts(flags);
  }
  return status;
}

static enum call_status lookup_node(struct nativefs_job *job)
{
  if (job->kind != DIRECTORY_KIND_DIRECTORY && job->kind != DIRECTORY_KIND_FILE) {
    return CALL_BAD_REQUEST;
  }
  uint64_t all = job->kind == DIRECTORY_KIND_DIRECTORY ? DIRECTORY_RIGHTS : FILE_RIGHTS;
  if (job->child_rights & ~all) {
    return CALL_BAD_REQUEST;
  }
  uint64_t allowed = job->kind == DIRECTORY_KIND_DIRECTORY ? job->rights :
      ((job->rights & DIRECTORY_RIGHT_READ_FILES) ? FILE_RIGHT_READ : 0);
  if (job->child_rights & ~allowed) {
    return CALL_DENIED;
  }
  if (job->count > PFS_NAME_MAX) {
    return CALL_LIMIT;
  }
  if (pfs_name_validate((const uint8_t *)job->name, job->count) != PFS_OK) {
    return CALL_BAD_REQUEST;
  }
  struct pfs_view *view = NULL;
  struct pfs_view_identity identity;
  struct pfs_rights requested = job->kind == DIRECTORY_KIND_DIRECTORY ?
      directory_rights(job->child_rights) : (struct pfs_rights){
        .file = job->child_rights & FILE_RIGHT_READ ? PFS_FILE_READ | PFS_FILE_METADATA : 0,
      };
  enum pfs_grant_scope scope = job->kind == DIRECTORY_KIND_DIRECTORY ?
      PFS_SCOPE_SUBTREE : PFS_SCOPE_OBJECT;
  /* The core reports scope/kind mismatch as INVALID or DENIED. Resolve kind
   * with a zero-right OBJECT view inside the held lookup authority first, so
   * scope/kind errors become WRONG_TYPE before requesting the final rights. */
  const struct pfs_rights none = {0};
  job->core_status = pfs_view_lookup(job->node->view, (const uint8_t *)job->name,
      job->count, PFS_SCOPE_OBJECT, &none, &view, &identity);
  if (job->core_status != PFS_OK) {
    return core_result(job->core_status);
  }
  KASSERT(pfs_view_close(&view, &(struct pfs_view_close_result){0}) == PFS_OK);
  if (identity.kind != (job->kind == DIRECTORY_KIND_DIRECTORY ?
      PFS_OBJECT_DIRECTORY : PFS_OBJECT_FILE)) {
    return CALL_WRONG_TYPE;
  }
  job->core_status = expired() ? PFS_IO : pfs_view_lookup(job->node->view,
      (const uint8_t *)job->name, job->count, scope, &requested, &view, &identity);
  if (job->core_status != PFS_OK) {
    return core_result(job->core_status);
  }
  struct nativefs_volume *volume = job->node->volume;
  uint64_t flags = cpu_save_interrupts();
  KASSERT(volume->references);
  bool retained = volume->references != SIZE_MAX;
  if (retained) {
    ++volume->references;
  }
  cpu_restore_interrupts(flags);
  enum call_status status = retained ?
      create_node(volume, view, job->kind, job->child_rights, &job->object) : CALL_LIMIT;
  if (status != CALL_OK) {
    KASSERT(pfs_view_close(&view, &(struct pfs_view_close_result){0}) == PFS_OK);
    if (retained) {
      flags = cpu_save_interrupts();
      nativefs_volume_put(volume);
      cpu_restore_interrupts(flags);
    }
  }
  return status;
}

static enum call_status enumerate_node(struct nativefs_job *job)
{
  struct directory_enumerate_reply entry = {.cursor = job->cursor};
  if (!job->cursor.generation && job->cursor.position) {
    return CALL_BAD_REQUEST;
  }
  if (job->cursor.generation && job->cursor.generation != job->node->identity) {
    entry.outcome = DIRECTORY_CHANGED;
  } else {
    struct pfs_view_entry candidate;
    size_t count = 0;
    bool done = false;
    uint64_t next = 0;
    job->core_status = pfs_view_directory_page(job->node->view, job->cursor.position,
        &candidate, 1, &count, &done, &next);
    if (job->core_status != PFS_OK) {
      return job->core_status == PFS_INVALID ? CALL_BAD_REQUEST : core_result(job->core_status);
    }
    if (!count) {
      KASSERT(done);
      entry.outcome = DIRECTORY_END;
      entry.cursor = (struct directory_cursor){job->node->identity, next};
    } else {
      entry.kind = candidate.kind == PFS_OBJECT_DIRECTORY ?
          DIRECTORY_KIND_DIRECTORY : DIRECTORY_KIND_FILE;
      entry.name_size = candidate.name.length + 1;
      if (job->count < entry.name_size) {
        entry.outcome = DIRECTORY_BUFFER_TOO_SMALL;
      } else {
        entry.outcome = DIRECTORY_ENTRY;
        entry.cursor = (struct directory_cursor){job->node->identity, next};
        memcpy(job->name, candidate.name.bytes, candidate.name.length);
        job->name[candidate.name.length] = '\0';
      }
    }
  }
  job->entry = entry;
  return CALL_OK;
}

static enum call_status capture_file(struct nativefs_job *job)
{
  struct pfs_view_metadata metadata;
  job->core_status = pfs_view_metadata(job->node->view, &metadata);
  if (job->core_status != PFS_OK) {
    return core_result(job->core_status);
  }
  if (!metadata.size) {
    return CALL_BAD_REQUEST;
  }
  if (metadata.size > LAUNCH_EXTERNAL_IMAGE_MAX_SIZE) {
    return CALL_LIMIT;
  }
  if (expired()) {
    return CALL_TIMED_OUT;
  }
  size_t size = metadata.size;
  uint64_t flags = cpu_save_interrupts();
  void *bytes = kmalloc(size);
  cpu_restore_interrupts(flags);
  if (!bytes) {
    return CALL_NO_MEMORY;
  }

  /* The retained view fixes object identity and generation. The core validates
   * the read, while every backing callback uses this job's original deadline. */
  size_t count = 0;
  job->core_status = pfs_view_read(job->node->view, 0, bytes, size, &count);
  enum call_status status = core_result(job->core_status);
  if (status == CALL_OK && count != size) {
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

static enum call_status filesystem_info(struct nativefs_job *job)
{
  const struct nativefs_volume *volume = job->node->volume;
  const struct nativefs_pool *pool = volume->pool;
  const struct pfs_superblock *superblock =
      &pool->diagnostic.candidate[pool->diagnostic.selected].superblock;
  if (superblock->block_count < 2 ||
      superblock->block_count - 2 > UINT64_MAX / PFS_BLOCK_SIZE ||
      volume->record.name.length > FILESYSTEM_VOLUME_NAME_MAX) {
    return CALL_IO;
  }

  job->info = (struct directory_filesystem_info){
    .type = FILESYSTEM_TYPE_PYXIS,
    .flags = FILESYSTEM_FLAG_READ_ONLY |
        (pool->gpt_degraded ? FILESYSTEM_FLAG_GPT_DEGRADED : 0) |
        (pool->diagnostic.degraded ? FILESYSTEM_FLAG_DEGRADED : 0),
    .generation = superblock->header.birth,
    .pool_allocatable_bytes = (superblock->block_count - 2) * PFS_BLOCK_SIZE,
  };
  memcpy(job->info.pool_id, superblock->header.pool.bytes, sizeof(job->info.pool_id));
  memcpy(job->info.volume_id, volume->record.id.bytes, sizeof(job->info.volume_id));
  memcpy(job->info.volume_name, volume->record.name.bytes, volume->record.name.length);
  return CALL_OK;
}

static enum call_status perform(struct nativefs_job *job)
{
  if (expired()) {
    return CALL_TIMED_OUT;
  }
  if (job->operation == NATIVEFS_ROOT) {
    return acquire_root(job);
  }
  if (!job->node) {
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
  switch (job->operation) {
  case NATIVEFS_FILESYSTEM_INFO:
    if (!directory) {
      return CALL_WRONG_TYPE;
    }
    if (!(job->rights & DIRECTORY_RIGHT_FILESYSTEM_INFO)) {
      return CALL_DENIED;
    }
    return filesystem_info(job);
  case NATIVEFS_LOOKUP:
  case NATIVEFS_ENUMERATE:
    if (!directory) {
      return CALL_WRONG_TYPE;
    }
    if (!(job->rights & (job->operation == NATIVEFS_LOOKUP ?
        DIRECTORY_RIGHT_LOOKUP : DIRECTORY_RIGHT_ENUMERATE))) {
      return CALL_DENIED;
    }
    return job->operation == NATIVEFS_LOOKUP ? lookup_node(job) : enumerate_node(job);
  case NATIVEFS_READ:
  case NATIVEFS_SIZE:
  case NATIVEFS_CAPTURE:
    if (directory) {
      return CALL_WRONG_TYPE;
    }
    if (!(job->rights & FILE_RIGHT_READ)) {
      return CALL_DENIED;
    }
    if (job->operation == NATIVEFS_CAPTURE) {
      return capture_file(job);
    }
    if (job->operation == NATIVEFS_READ) {
      if (job->count > sizeof(job->data)) {
        return CALL_BAD_REQUEST;
      }
      size_t count = 0;
      job->core_status = pfs_view_read(node->view, job->offset, job->data, job->count, &count);
      if (job->core_status == PFS_OK) {
        job->count = count;
      }
    } else {
      struct pfs_view_metadata metadata;
      job->core_status = pfs_view_metadata(node->view, &metadata);
      if (job->core_status == PFS_OK) {
        job->offset = metadata.size;
      }
    }
    return core_result(job->core_status);
  default: return CALL_BAD_OPERATION;
  }
}

/* Detach loans/context/queue state before returning the record. User requests
 * are consumed only after notification; kernel jobs are BSP-only observations. */
static void complete_job(struct nativefs_job *job)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct nativefs_request *request = job->user_request;
  job->user_request = NULL;
  job->node = NULL;
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

static void nativefs_worker(void *argument)
{
  (void)argument;
  struct nativefs_volume *busy = NULL;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    struct nativefs_node *nodes = retired_nodes;
    retired_nodes = NULL;
    struct nativefs_volume *cleanup = retired;
    retired = NULL;
    struct nativefs_job *job = first_job;
    if (job) {
      first_job = job->next;
      if (!first_job) {
        last_job = NULL;
      }
      job->next = NULL;
      job->state = NATIVEFS_JOB_ACTIVE;
    }
    if (!nodes && !cleanup && !job) {
      KASSERT(!worker_wait);
      struct task_wait *wait = task_wait_prepare();
      worker_wait = wait;
      task_wait_sleep(wait);
      cpu_restore_interrupts(flags);
      continue;
    }
    cpu_restore_interrupts(flags);
    destroy_nodes(nodes);
    while (cleanup) {
      struct nativefs_volume *next = cleanup->retired_next;
      cleanup->retired_next = busy;
      busy = cleanup;
      cleanup = next;
    }
    busy = retire_volumes(busy);
    if (job) {
      flags = cpu_save_interrupts();
      struct execution_group *previous = object_cleanup_enter(job->user_request ?
          job->user_request->request.cleanup_group : NULL);
      cpu_restore_interrupts(flags);
      struct nativefs_context context = {.deadline = job->deadline};
      KASSERT(!operation);
      operation = &context;
      job->core_status = PFS_OK;
      job->status = perform(job);
      if (job->status == CALL_OK && expired()) {
        job->status = CALL_TIMED_OUT;
        job->core_status = PFS_IO;
      }
      job->backing_error = context.backing_error;
      operation = NULL;
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
      job->user_request || job->admitted || (unsigned)job->operation > NATIVEFS_FILESYSTEM_INFO) {
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
  KASSERT(!request->job.node && !request->job.object && !request->job.captured && !request->job.next &&
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
  KASSERT(!available && !core_memory.limit);
  KASSERT(pfs_memory_init(&core_memory, NULL, core_allocate, core_free, NATIVEFS_CORE_BYTES) == PFS_OK);
  enum mm_result result = kernel_task_create(nativefs_worker, NULL);
  if (result != MM_OK) {
    klog("nativefs: cannot create worker (error %u)\n", (unsigned)result);
    return;
  }
  available = true;
}
