#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/pressure.h>
#include <kernel/mm/vm.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/object.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/wait.h>
#include <tlsf.h>
#include <stdint.h>

#define HEAP_ALIGNMENT 16
#define HEAP_CONTROL_BYTES (16 * 1024)
#define HEAP_POOL_BYTES (256 * 1024)
#define HEAP_POOL_PREFIX_BYTES (sizeof(struct heap_pool) + HEAP_ALIGNMENT)
/* The arena outlives every failed growth, which retires what it mapped. */
#define HEAP_ARENA_RAM_MULTIPLE 4
/* Each page table maps 512 pages, and each page directory 512 tables. */
#define PAGES_PER_TABLE 512
/* Scheduling quantum, independent of allocation and admission sizes. */
#define RECLAIM_STEP_PAGES 64

struct heap_pool {
  size_t bytes, allocations;
  struct heap_pool *next;
  struct execution_group *cleanup_group;
};

_Static_assert(HEAP_POOL_PREFIX_BYTES % HEAP_ALIGNMENT == 0, "pool prefix alignment");

static unsigned char control[HEAP_CONTROL_BYTES] __attribute__((aligned(HEAP_ALIGNMENT)));
static tlsf_t allocator;

/* Protects the TLSF state and the allocation and pool counters. A leaf lock. */
static struct spinlock heap_lock;

/* Serializes growth and protects the arena cursor and its counters. Taken
 * before the heap lock and the PMM lock, never while holding either. */
static struct spinlock growth_lock;
static uintptr_t arena_next, arena_end;

/* Pool and allocation counters belong to heap_lock; the arena counters, which
 * move with arena_next, belong to growth_lock. */
static struct heap_stats stats;

/* Detached pools only. Never nests heap/growth guards; may wake the worker. */
static struct spinlock reclaim_guard;
static struct heap_pool *reclaim_first, *reclaim_last, *quarantined;
static size_t reclaim_bytes;
static struct task_wait *reclaim_wait;

static void reclaim_worker(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    spin_lock(&reclaim_guard);
    struct heap_pool *pool = reclaim_first;
    if (!pool) {
      struct task_wait *wait = task_wait_prepare();
      reclaim_wait = wait;
      spin_unlock(&reclaim_guard);
      task_wait_sleep(wait);
      spin_lock(&reclaim_guard);
      if (reclaim_wait == wait) {
        reclaim_wait = NULL;
      }
      spin_unlock(&reclaim_guard);
      cpu_restore_interrupts(flags);
      continue;
    }
    struct heap_pool *last = reclaim_last;
    size_t bytes = reclaim_bytes;
    reclaim_first = reclaim_last = NULL;
    reclaim_bytes = 0;
    spin_unlock(&reclaim_guard);
    cpu_restore_interrupts(flags);

    struct vm_kernel_owner owner = {0};
    vm_kernel_owner_cleanup(&owner);
    /* TLSF removal under heap_lock ended every access and prevents refills.
     * Keep all mappings/backing intact until every CPU confirms withdrawal. */
    bool confirmed = vm_kernel_flush_remote();
    flags = cpu_save_interrupts();
    if (!confirmed) {
      spin_lock(&heap_lock);
      stats.retiring_bytes -= bytes;
      stats.quarantined_bytes += bytes;
      spin_unlock(&heap_lock);
      spin_lock(&reclaim_guard);
      last->next = quarantined;
      quarantined = pool;
      spin_unlock(&reclaim_guard);
      /* Keep cleanup pins too: a late ACK cannot complete this retirement. */
      vm_kernel_owner_release(&owner);
      cpu_restore_interrupts(flags);
      continue;
    }

    while (pool) {
      uintptr_t base = (uintptr_t)pool;
      size_t size = pool->bytes;
      struct heap_pool *next = pool->next;
      struct execution_group *group = pool->cleanup_group;
      /* Capture prefix metadata before unmapping it. No pointer into this
       * pool is used again, including across a bounded scheduling step. */
      for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        phys_addr_t physical;
        KASSERT(arch_page_unmap(arch_kernel_space(), base + offset, &physical) == MM_OK);
        pmm_free(physical, 1);
        if ((offset / PAGE_SIZE + 1) % RECLAIM_STEP_PAGES == 0) {
          kernel_task_yield_masked();
        }
      }
      spin_lock(&heap_lock);
      stats.retiring_bytes -= size;
      stats.reclaimed_bytes += size;
      spin_unlock(&heap_lock);
      if (group) {
        execution_group_cleanup_end(group);
      }
      pool = next;
    }
    vm_kernel_owner_release(&owner);
    cpu_restore_interrupts(flags);
  }
}

void heap_reclaim_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  if (kernel_task_create(reclaim_worker, NULL) != MM_OK) {
    panic("cannot create heap reclaim worker");
  }
}

static void retire_pool(struct heap_pool *pool)
{
  pool->cleanup_group = object_cleanup_defer();
  pool->next = NULL;
  spin_lock(&reclaim_guard);
  if (reclaim_last) {
    reclaim_last->next = pool;
  } else {
    reclaim_first = pool;
  }
  reclaim_last = pool;
  reclaim_bytes += pool->bytes;
  if (reclaim_wait) {
    struct task_wait *wait = reclaim_wait;
    reclaim_wait = NULL;
    task_wait_wake(wait);
  }
  spin_unlock(&reclaim_guard);
}

static size_t pool_size_for_request(size_t request)
{
  /* TLSF memalign needs room for a leading split; size-class search rounds up
   * by at most 1/32 of its adjusted size. A page plus 1/16 leaves room for both
   * and for the two pool headers, without requiring contiguous physical RAM. */
  size_t overhead = tlsf_pool_overhead() + HEAP_POOL_PREFIX_BYTES;
  size_t size_class_slack = request / 16;
  if (request > SIZE_MAX - size_class_slack ||
      request + size_class_slack > SIZE_MAX - PAGE_SIZE - overhead) {
    return 0;
  }

  size_t bytes = request + size_class_slack + PAGE_SIZE + overhead;
  if (bytes < HEAP_POOL_BYTES) {
    bytes = HEAP_POOL_BYTES;
  }

  if (bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return 0;
  }
  bytes = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if (bytes <= overhead || bytes - overhead >= tlsf_block_size_max()) {
    return 0;
  }
  return bytes;
}

/* Unpublished pages: no other CPU can have used these translations, so a
 * local unmap is enough. The addresses are still never reused. */
static void retire_mapped(uintptr_t base, size_t bytes)
{
  for (size_t offset = 0; offset < bytes; offset += PAGE_SIZE) {
    phys_addr_t physical;
    KASSERT(arch_page_unmap(arch_kernel_space(), base + offset, &physical) == MM_OK);
    pmm_free(physical, 1);
  }
  arena_next += bytes;
  stats.arena_bytes += bytes;
  stats.retired_bytes += bytes;
}

static void *allocate_locked(size_t bytes)
{
  void *pointer = tlsf_memalign(allocator, HEAP_ALIGNMENT, bytes);
  if (pointer) {
    struct heap_pool *pool = (void *)((uintptr_t)tlsf_block_pool(pointer) -
        HEAP_POOL_PREFIX_BYTES);
    ++pool->allocations;
    ++stats.live_allocations;
    stats.live_block_bytes += tlsf_block_size(pointer);
  }
  return pointer;
}

/* growth_lock. Maps a pool at never-mapped arena addresses and publishes it.
 * Allocates REQUEST in the same heap-lock section, so no other CPU can take
 * the new space first. A zero REQUEST only adds the pool. */
static bool add_pool(size_t request, void **result)
{
  size_t bytes = pool_size_for_request(request);
  if (!bytes || bytes > arena_end - arena_next) {
    return false;
  }

  /* A growth that cannot fit in the free frames would map until the PMM is
   * empty, then retire all of it. Refuse it before mapping anything, but
   * still start reclamation as a failed frame allocation would. Only frames
   * taken by another CPU meanwhile can still make a growth fail. */
  size_t pages = bytes / PAGE_SIZE;
  size_t table_pages = pages / PAGES_PER_TABLE + 1 +
                       pages / (PAGES_PER_TABLE * PAGES_PER_TABLE) + 1;
  if (pages + table_pages > pmm_get_stats().free_frames) {
    mm_pressure_notify();
    return false;
  }

  uintptr_t base = arena_next;
  size_t mapped = 0;
  for (; mapped < bytes; mapped += PAGE_SIZE) {
    phys_addr_t physical = pmm_alloc(1);
    if (!physical) {
      break;
    }

    arch_frame_zero(physical);
    if (arch_page_map(arch_kernel_space(), base + mapped, physical, PAGE_WRITE) != MM_OK) {
      pmm_free(physical, 1);
      break;
    }
  }
  if (mapped != bytes) {
    /* The addresses past the mapped prefix were never mapped; keep them. */
    retire_mapped(base, mapped);
    return false;
  }

  /* TLSF's first block header borrows the padding after our pool record. */
  struct heap_pool *record = (void *)base;
  *record = (struct heap_pool){.bytes = bytes};
  void *pool = (unsigned char *)base + HEAP_POOL_PREFIX_BYTES;
  spin_lock(&heap_lock);
  bool added = tlsf_add_pool(allocator, pool, bytes - HEAP_POOL_PREFIX_BYTES);
  if (added) {
    ++stats.pools;
    stats.pool_bytes += bytes;
    if (request) {
      *result = allocate_locked(request);
    }
  }
  spin_unlock(&heap_lock);

  if (!added) {
    retire_mapped(base, bytes);
    return false;
  }
  arena_next += bytes;
  stats.arena_bytes += bytes;
  return true;
}

bool heap_init(void)
{
  KASSERT(!allocator);
  KASSERT(tlsf_size() <= sizeof(control));
  KASSERT(tlsf_alloc_overhead() <= HEAP_ALIGNMENT);
  _Static_assert(_Alignof(max_align_t) <= HEAP_ALIGNMENT, "C allocation alignment");
  KASSERT(arch_heap_arena_size() / HEAP_ARENA_RAM_MULTIPLE >= PMM_MAX_PHYS);

  allocator = tlsf_create(control);
  KASSERT(allocator != NULL);
  arena_next = arch_heap_arena_base();
  arena_end = arena_next + arch_heap_arena_size();

  spin_lock(&growth_lock);
  bool added = add_pool(0, NULL);
  spin_unlock(&growth_lock);
  if (!added) {
    allocator = NULL;
    return false;
  }
  return true;
}

void *kmalloc(size_t bytes)
{
  /* Keep alignment and size-class rounding below TLSF's highest bin. This
   * conservative upper bound also prevents its unchecked internal additions
   * from wrapping for arbitrary size_t requests. */
  if (!allocator || !bytes || bytes > tlsf_block_size_max() / 2) {
    return NULL;
  }

  spin_lock(&heap_lock);
  void *pointer = allocate_locked(bytes);
  spin_unlock(&heap_lock);
  if (pointer) {
    return pointer;
  }

  /* Another CPU may have grown the heap while this one waited for the
   * growth lock, so retry before adding a pool. */
  spin_lock(&growth_lock);
  spin_lock(&heap_lock);
  pointer = allocate_locked(bytes);
  spin_unlock(&heap_lock);
  if (!pointer) {
    add_pool(bytes, &pointer);
  }
  spin_unlock(&growth_lock);
  return pointer;
}

void kfree(void *pointer)
{
  if (!pointer) {
    return;
  }

  spin_lock(&heap_lock);
  struct heap_pool *pool = (void *)((uintptr_t)tlsf_block_pool(pointer) -
      HEAP_POOL_PREFIX_BYTES);
  size_t bytes = tlsf_block_size(pointer);
  tlsf_free(allocator, pointer);

  --stats.live_allocations;
  stats.live_block_bytes -= bytes;
  KASSERT(pool->allocations);
  bool empty = --pool->allocations == 0;
  if (empty) {
    tlsf_remove_pool(allocator, (uint8_t *)pool + HEAP_POOL_PREFIX_BYTES);
    --stats.pools;
    stats.pool_bytes -= pool->bytes;
    stats.retiring_bytes += pool->bytes;
  }
  spin_unlock(&heap_lock);
  if (empty) {
    retire_pool(pool);
  }
}

struct heap_stats heap_get_stats(void)
{
  spin_lock(&growth_lock);
  spin_lock(&heap_lock);
  struct heap_stats snapshot = stats;
  spin_unlock(&heap_lock);
  spin_unlock(&growth_lock);
  return snapshot;
}
