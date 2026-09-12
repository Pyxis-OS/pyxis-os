#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <tlsf.h>
#include <stdint.h>

#define HEAP_ALIGNMENT 16
#define HEAP_CONTROL_BYTES 16384
#define HEAP_POOL_BYTES (256 * 1024)
#define HEAP_MAX_POOLS 32

static unsigned char control[HEAP_CONTROL_BYTES] __attribute__((aligned(HEAP_ALIGNMENT)));
static tlsf_t allocator;
static struct {
  uintptr_t base;
  size_t bytes;
} pools[HEAP_MAX_POOLS];
static struct heap_stats stats;

static bool add_pool(size_t request)
{
  if (stats.pools == HEAP_MAX_POOLS) {
    return false;
  }
  /* TLSF memalign needs room for a leading split; size-class search rounds up
   * by at most 1/32 of its adjusted size. A page plus 1/16 leaves room for both
   * and for the two pool headers, without requiring contiguous physical RAM. */
  size_t overhead = tlsf_pool_overhead();
  if (request > SIZE_MAX - request / 16 ||
      request + request / 16 > SIZE_MAX - PAGE_SIZE - overhead) {
    return false;
  }
  size_t bytes = request + request / 16 + PAGE_SIZE + overhead;
  if (bytes < HEAP_POOL_BYTES) {
    bytes = HEAP_POOL_BYTES;
  }
  if (bytes > SIZE_MAX - (PAGE_SIZE - 1)) {
    return false;
  }
  bytes = (bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  if (bytes <= overhead || bytes - overhead >= tlsf_block_size_max()) {
    return false;
  }
  void *memory;
  if (vm_alloc(bytes, PAGE_SIZE, PAGE_WRITE, &memory) != MM_OK) {
    return false;
  }
  if (!tlsf_add_pool(allocator, memory, bytes)) {
    KASSERT(vm_free(memory, bytes) == MM_OK);
    return false;
  }
  pools[stats.pools].base = (uintptr_t)memory;
  pools[stats.pools].bytes = bytes;
  ++stats.pools;
  stats.pool_bytes += bytes;
  return true;
}

bool heap_init(void)
{
  KASSERT(!allocator);
  KASSERT(tlsf_size() <= sizeof(control));
  _Static_assert(_Alignof(max_align_t) <= HEAP_ALIGNMENT, "C allocation alignment");
  allocator = tlsf_create(control);
  KASSERT(allocator != NULL);
  if (!add_pool(0)) {
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
  void *pointer = tlsf_memalign(allocator, HEAP_ALIGNMENT, bytes);
  if (!pointer) {
    if (!add_pool(bytes)) {
      return NULL;
    }
    pointer = tlsf_memalign(allocator, HEAP_ALIGNMENT, bytes);
  }
  if (pointer) {
    KASSERT(!((uintptr_t)pointer & (HEAP_ALIGNMENT - 1)));
    ++stats.live_allocations;
    stats.live_block_bytes += tlsf_block_size(pointer);
  }
  return pointer;
}

void kfree(void *pointer)
{
  if (!pointer) {
    return;
  }
  KASSERT(allocator && !((uintptr_t)pointer & (HEAP_ALIGNMENT - 1)));
  bool in_pool = false;
  for (size_t i = 0; i < stats.pools; ++i) {
    uintptr_t address = (uintptr_t)pointer;
    if (address >= pools[i].base + tlsf_alloc_overhead() &&
        address < pools[i].base + pools[i].bytes - tlsf_alloc_overhead()) {
      in_pool = true;
      break;
    }
  }
  KASSERT(in_pool && stats.live_allocations);
  size_t bytes = tlsf_block_size(pointer);
  KASSERT(bytes <= stats.live_block_bytes);
  tlsf_free(allocator, pointer);
  --stats.live_allocations;
  stats.live_block_bytes -= bytes;
}

struct heap_stats heap_get_stats(void)
{
  return stats;
}
