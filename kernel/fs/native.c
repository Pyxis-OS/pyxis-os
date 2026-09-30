#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/fs/native.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/object.h>
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
};

struct nativefs_volume {
  struct nativefs_volume *next, *retired_next;
  struct nativefs_pool *pool;
  struct pfs_volume core;
  struct pfs_volume_record record;
  size_t references;
  struct execution_group *cleanup_group;
};

struct nativefs_operation {
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
static struct nativefs_operation *operation;
static struct nativefs_pool *pools;
static struct nativefs_volume *volumes, *retired;
static struct nativefs_job *first_job, *last_job;
static struct task_wait *worker_wait;
static bool available;
static size_t admitted, wrapper_count, adapter_used, adapter_peak;
static size_t core_peak, core_heap_used, core_heap_peak;

static void nativefs_worker(void *argument);

static void require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && kernel_task_is_current(nativefs_worker));
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
  case PFS_INVALID: return CALL_IO;
  case PFS_NOT_FOUND: return CALL_NOT_FOUND;
  case PFS_ABSENT: return CALL_IO;
  case PFS_UNSUPPORTED: return CALL_UNAVAILABLE;
  case PFS_LIMIT: return CALL_LIMIT;
  case PFS_NO_MEMORY: return CALL_NO_MEMORY;
  case PFS_READ_ONLY: return CALL_READ_ONLY;
  case PFS_BUSY: return CALL_BUSY;
  case PFS_DENIED: return CALL_DENIED;
  case PFS_CORRUPT: return CALL_IO;
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
    const struct gpt_partition **partition, struct block_info *device)
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
    const struct block_info *device, struct nativefs_pool **out,
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

static enum call_status open_volume(struct nativefs_job *job)
{
  const struct gpt_partition *partition;
  struct block_info device;
  enum call_status result = select_partition(job, &partition, &device);
  if (result != CALL_OK) {
    return result;
  }
  struct nativefs_pool *pool = NULL;
  result = open_pool(partition, &device, &pool, &job->core_status);
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
      if (catalog[i].name.length == job->name.length &&
          !memcmp(catalog[i].name.bytes, job->name.bytes, job->name.length)) {
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
          job->volume = volume;
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
        job->volume = volume;
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

void nativefs_volume_put(struct nativefs_volume *volume)
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

static void nativefs_worker(void *argument)
{
  (void)argument;
  struct nativefs_volume *busy = NULL;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
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
    if (!cleanup && !job) {
      KASSERT(!worker_wait);
      struct task_wait *wait = task_wait_prepare();
      worker_wait = wait;
      task_wait_sleep(wait);
      cpu_restore_interrupts(flags);
      continue;
    }
    cpu_restore_interrupts(flags);
    /* BUSY retains all ownership; retry only on subsequent real work, never
     * spin waiting for an incorrectly retained core view to disappear. */
    while (cleanup) {
      struct nativefs_volume *next = cleanup->retired_next;
      cleanup->retired_next = busy;
      busy = cleanup;
      cleanup = next;
    }
    busy = retire_volumes(busy);
    if (job) {
      struct nativefs_operation context = {.deadline = job->deadline};
      KASSERT(!operation);
      operation = &context;
      job->core_status = PFS_OK;
      job->status = open_volume(job);
      if (job->status == CALL_OK && expired()) {
        flags = cpu_save_interrupts();
        nativefs_volume_put(job->volume);
        cpu_restore_interrupts(flags);
        job->volume = NULL;
        job->status = CALL_TIMED_OUT;
        job->core_status = PFS_IO;
      }
      job->backing_error = context.backing_error;
      operation = NULL;
      flags = cpu_save_interrupts();
      KASSERT(admitted);
      --admitted;
      job->state = NATIVEFS_JOB_COMPLETE;
      cpu_restore_interrupts(flags);
      /* No job access after ownership return. */
    }
    kernel_task_yield_if_runnable();
  }
}

enum call_status nativefs_submit(struct nativefs_job *job)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!job || job->state != NATIVEFS_JOB_IDLE || job->volume || job->next ||
      !job->partition || pfs_name_validate(job->name.bytes, job->name.length) != PFS_OK) {
    return CALL_BAD_REQUEST;
  }
  if (!available) {
    return CALL_UNAVAILABLE;
  }
  if (admitted == NATIVEFS_REQUEST_LIMIT) {
    return CALL_BUSY;
  }
  job->deadline = task_deadline_after_ms(NATIVEFS_TIMEOUT_MS);
  job->state = NATIVEFS_JOB_QUEUED;
  ++admitted;
  if (last_job) {
    last_job->next = job;
  } else {
    first_job = job;
  }
  last_job = job;
  wake_worker();
  return CALL_OK;
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
