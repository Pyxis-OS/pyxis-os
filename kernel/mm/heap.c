#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/panic.h>
#include <tlsf.h>
#include <stdint.h>

#define HEAP_ALIGNMENT 16
#define HEAP_CONTROL_BYTES (16 * 1024)
#define HEAP_POOL_BYTES (256 * 1024)
#define HEAP_POOL_PREFIX_BYTES HEAP_ALIGNMENT

static unsigned char control[HEAP_CONTROL_BYTES] __attribute__((aligned(HEAP_ALIGNMENT)));
static tlsf_t allocator;
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

static bool add_pool(size_t request)
{
  size_t bytes = pool_size_for_request(request);
  if (!bytes) {
    return false;
  }

  void *memory;
  if (vm_alloc(bytes, PAGE_SIZE, PAGE_WRITE, &memory) != MM_OK) {
    return false;
  }

  /* TLSF forms its first header one word before the pool. The prefix keeps
   * that address mapped and canonical, even at the higher-half boundary. */
  void *pool = (unsigned char *)memory + HEAP_POOL_PREFIX_BYTES;
  if (!tlsf_add_pool(allocator, pool, bytes - HEAP_POOL_PREFIX_BYTES)) {
    KASSERT(vm_free(memory, bytes) == MM_OK);
    return false;
  }

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

  size_t bytes = tlsf_block_size(pointer);
  tlsf_free(allocator, pointer);

  --stats.live_allocations;
  stats.live_block_bytes -= bytes;
}

struct heap_stats heap_get_stats(void)
{
  return stats;
}
