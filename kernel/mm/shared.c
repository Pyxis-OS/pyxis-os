#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/shared.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/space.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>

struct shared_origin {
  struct kernel_object object;
  /* Leaf guard: IF=0, no allocation, PMM call or yield while held. */
  struct spinlock guard;
  struct space *space;
  uint64_t process_key;
  size_t allocated_bytes, quarantined_bytes, region_metadata_bytes;
};

struct shared_region {
  struct kernel_object object;
  struct shared_origin *origin;
  size_t size, page_count, creation_cpu;
  bool quarantined; /* Origin guard; the first owned reference stays permanent. */
  phys_addr_t frames[];
};

struct shared_memory_queue {
  struct spinlock guard;
  _Atomic(struct kernel_object *) first;
  struct kernel_object *last;
};

static struct shared_memory_queue *queues;
static size_t queue_count;

static size_t region_metadata_size(const struct shared_region *region)
{
  return sizeof(*region) + region->page_count * sizeof(*region->frames);
}

static void destroy_origin(struct kernel_object *object)
{
  struct shared_origin *origin = (struct shared_origin *)object;
  KASSERT(!origin->allocated_bytes && !origin->quarantined_bytes &&
      !origin->region_metadata_bytes);
  kfree(origin);
}

struct shared_origin *shared_origin_create(struct space *space, uint64_t process_key)
{
  KASSERT(!cpu_interrupts_enabled() && space && process_key);
  KASSERT(arch_space_active(arch_kernel_space()));
  struct shared_origin *origin = kmalloc(sizeof(*origin));
  if (!origin) {
    return NULL;
  }
  *origin = (struct shared_origin){.space = space, .process_key = process_key};
  atomic_init(&origin->guard.locked, false);
  object_init(&origin->object, OBJECT_SHARED_ORIGIN, destroy_origin);
  return origin;
}

struct kernel_object *shared_origin_object(struct shared_origin *origin)
{
  return &origin->object;
}

struct kernel_object *shared_region_object(struct shared_region *region)
{
  return &region->object;
}

struct shared_region *shared_region_from_object(struct kernel_object *object)
{
  return object && object->type == OBJECT_SHARED_REGION ?
      (struct shared_region *)object : NULL;
}

void shared_origin_snapshot(struct shared_origin *origin,
    struct shared_origin_snapshot *snapshot)
{
  KASSERT(!cpu_interrupts_enabled() && origin && snapshot);
  spin_lock(&origin->guard);
  *snapshot = (struct shared_origin_snapshot){
    .space = origin->space, .process_key = origin->process_key,
    .allocated_bytes = origin->allocated_bytes,
    .quarantined_bytes = origin->quarantined_bytes,
    .origin_metadata_bytes = sizeof(*origin),
    .region_metadata_bytes = origin->region_metadata_bytes,
  };
  spin_unlock(&origin->guard);
}

static void charge_frames(struct shared_origin *origin, size_t count)
{
  size_t bytes = count * PAGE_SIZE;
  spin_lock(&origin->guard);
  KASSERT(bytes <= SIZE_MAX - origin->allocated_bytes);
  origin->allocated_bytes += bytes;
  atomic_fetch_add_explicit(&origin->space->shared_allocated_bytes, bytes,
      memory_order_relaxed);
  spin_unlock(&origin->guard);
}

static void release_frames(struct shared_origin *origin, const phys_addr_t *frames,
    size_t count)
{
  pmm_free_frames(frames, count);
  size_t bytes = count * PAGE_SIZE;
  spin_lock(&origin->guard);
  KASSERT(bytes <= origin->allocated_bytes - origin->quarantined_bytes);
  origin->allocated_bytes -= bytes;
  size_t previous = atomic_fetch_sub_explicit(&origin->space->shared_allocated_bytes,
      bytes, memory_order_relaxed);
  KASSERT(bytes <= previous);
  spin_unlock(&origin->guard);
}

static void release_metadata(struct shared_origin *origin, size_t bytes)
{
  spin_lock(&origin->guard);
  KASSERT(bytes <= origin->region_metadata_bytes);
  origin->region_metadata_bytes -= bytes;
  spin_unlock(&origin->guard);
}

static void destroy_region(struct kernel_object *object)
{
  struct shared_region *region = (struct shared_region *)object;
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() == 0);
  KASSERT(queues && region->creation_cpu < queue_count);
  /* object_reap has saved its next pointer and cleanup token. Acquire a new
   * token before it ends that token, then lend this object's link to the RAM
   * queue. Neither the callback nor the BSP reaper accesses it afterward. */
  object->cleanup_group = object_cleanup_defer();
  object->retired_next = NULL;
  size_t cpu = region->creation_cpu;
  struct shared_memory_queue *queue = &queues[cpu];
  spin_lock(&queue->guard);
  if (queue->last) {
    queue->last->retired_next = object;
  } else {
    atomic_store_explicit(&queue->first, object, memory_order_release);
  }
  queue->last = object;
  spin_unlock(&queue->guard);
  process_ram_cleanup_notify(cpu);
}

enum call_status shared_region_create(struct shared_origin *origin, size_t size,
    struct shared_region **result)
{
  KASSERT(!cpu_interrupts_enabled() && task_current_actor());
  KASSERT(arch_space_active(arch_kernel_space()) && origin && result);
  KASSERT(queues && arch_cpu_index() < queue_count);
  *result = NULL;
  if (!size || size % PAGE_SIZE) {
    return CALL_BAD_REQUEST;
  }
  size_t pages = size / PAGE_SIZE;
  if (pages > (SIZE_MAX - sizeof(struct shared_region)) / sizeof(phys_addr_t)) {
    return CALL_LIMIT;
  }
  size_t metadata_bytes = sizeof(struct shared_region) + pages * sizeof(phys_addr_t);
  struct shared_region *region = kmalloc(metadata_bytes);
  if (!region) {
    return CALL_NO_MEMORY;
  }
  if (!object_retain(&origin->object)) {
    kfree(region);
    return CALL_LIMIT;
  }
  spin_lock(&origin->guard);
  bool fits = metadata_bytes <= SIZE_MAX - origin->region_metadata_bytes;
  if (fits) {
    origin->region_metadata_bytes += metadata_bytes;
  }
  spin_unlock(&origin->guard);
  if (!fits) {
    kfree(region);
    object_release(&origin->object);
    return CALL_LIMIT;
  }
  *region = (struct shared_region){
    .origin = origin, .size = size, .page_count = pages,
    .creation_cpu = arch_cpu_index(),
  };
  object_init(&region->object, OBJECT_SHARED_REGION, destroy_region);

  size_t allocated = 0;
  while (allocated < pages) {
    size_t count = pages - allocated;
    if (count > PMM_FRAME_BATCH_MAX) {
      count = PMM_FRAME_BATCH_MAX;
    }
    size_t prefix = pmm_alloc_frames(region->frames + allocated, count);
    if (prefix) {
      charge_frames(origin, prefix);
      for (size_t i = 0; i < prefix; ++i) {
        arch_frame_zero(region->frames[allocated + i]);
      }
      allocated += prefix;
    }
    if (prefix != count) {
      while (allocated) {
        count = allocated < PMM_FRAME_BATCH_MAX ? allocated : PMM_FRAME_BATCH_MAX;
        allocated -= count;
        release_frames(origin, region->frames + allocated, count);
        if (allocated) {
          task_yield_masked();
        }
      }
      kfree(region);
      release_metadata(origin, metadata_bytes);
      object_release(&origin->object);
      return CALL_NO_MEMORY;
    }
    if (allocated < pages) {
      task_yield_masked();
    }
  }
  *result = region;
  return CALL_OK;
}

size_t shared_region_size(const struct shared_region *region)
{
  return region->size;
}

size_t shared_region_page_count(const struct shared_region *region)
{
  return region->page_count;
}

phys_addr_t shared_region_frame(const struct shared_region *region, size_t index)
{
  KASSERT(index < region->page_count);
  return region->frames[index];
}

struct shared_origin *shared_region_origin(const struct shared_region *region)
{
  return region->origin;
}

void shared_region_quarantine_owned(struct shared_region **owned)
{
  KASSERT(!cpu_interrupts_enabled() && owned && *owned);
  struct shared_region *region = *owned;
  *owned = NULL;
  struct shared_origin *origin = region->origin;
  spin_lock(&origin->guard);
  bool first = !region->quarantined;
  if (first) {
    KASSERT(region->size <= origin->allocated_bytes - origin->quarantined_bytes);
    region->quarantined = true;
    origin->quarantined_bytes += region->size;
    atomic_fetch_add_explicit(&origin->space->shared_quarantined_bytes, region->size,
        memory_order_relaxed);
  }
  spin_unlock(&origin->guard);
  if (!first) {
    object_release(&region->object);
  }
}

void shared_memory_init(void)
{
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() == 0 && !queues);
  size_t count = arch_cpu_count();
  KASSERT(count && count <= SIZE_MAX / sizeof(*queues));
  queues = kmalloc(count * sizeof(*queues));
  if (!queues) {
    panic("cannot allocate shared memory retirement queues");
  }
  for (size_t cpu = 0; cpu < count; ++cpu) {
    queues[cpu] = (struct shared_memory_queue){0};
    atomic_init(&queues[cpu].guard.locked, false);
    atomic_init(&queues[cpu].first, NULL);
  }
  queue_count = count;
}

bool shared_memory_pending(void)
{
  KASSERT(!cpu_interrupts_enabled());
  size_t cpu = arch_cpu_index();
  KASSERT(queues && cpu < queue_count);
  return atomic_load_explicit(&queues[cpu].first, memory_order_acquire) != NULL;
}

void shared_memory_reap(void)
{
  KASSERT(cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  size_t cpu = arch_cpu_index();
  KASSERT(queues && cpu < queue_count);
  struct shared_memory_queue *queue = &queues[cpu];
  if (!atomic_load_explicit(&queue->first, memory_order_acquire)) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&queue->guard);
  struct kernel_object *object = atomic_load_explicit(&queue->first, memory_order_relaxed);
  if (object) {
    atomic_store_explicit(&queue->first, object->retired_next, memory_order_release);
    if (!object->retired_next) {
      queue->last = NULL;
    }
    object->retired_next = NULL;
  }
  spin_unlock(&queue->guard);
  if (!object) {
    cpu_restore_interrupts(flags);
    return;
  }
  struct shared_region *region = (struct shared_region *)object;
  KASSERT(region->creation_cpu == cpu && !atomic_load_explicit(&object->references,
      memory_order_relaxed));
  struct shared_origin *origin = region->origin;
  struct execution_group *group = object->cleanup_group;
  struct execution_group *previous = object_cleanup_enter(group);
  size_t pages = region->page_count;
  size_t metadata_bytes = region_metadata_size(region);
  for (size_t freed = 0; freed < pages;) {
    size_t count = pages - freed;
    if (count > PMM_FRAME_BATCH_MAX) {
      count = PMM_FRAME_BATCH_MAX;
    }
    release_frames(origin, region->frames + freed, count);
    freed += count;
    if (freed < pages) {
      cpu_restore_interrupts(flags);
      kernel_task_yield_if_runnable();
      flags = cpu_save_interrupts();
    }
  }
  kfree(region);
  release_metadata(origin, metadata_bytes);
  object_release(&origin->object);
  object_cleanup_leave(previous);
  if (group) {
    execution_group_cleanup_end(group);
  }
  cpu_restore_interrupts(flags);
}
