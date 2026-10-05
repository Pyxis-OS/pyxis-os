#include <arch/paging.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <tlsf.h>
#include <stdint.h>

#define HEAP_ALIGNMENT 16
#define HEAP_CONTROL_BYTES (16 * 1024)
#define HEAP_POOL_BYTES (256 * 1024)
#define HEAP_POOL_PREFIX_BYTES HEAP_ALIGNMENT
/* The arena outlives every failed growth, which retires what it mapped. */
#define HEAP_ARENA_RAM_MULTIPLE 4

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

  /* TLSF forms its first header one word before the pool. The prefix keeps
   * that address inside this pool's mapping. */
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
  size_t bytes = tlsf_block_size(pointer);
  tlsf_free(allocator, pointer);

  --stats.live_allocations;
  stats.live_block_bytes -= bytes;
  spin_unlock(&heap_lock);
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
