#include <lwip/opt.h>
#include <lwip/sys.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/net/interface.h>

void caelum_lwip_assert_context(void)
{
  net_worker_assert_context();
}

struct lwip_allocation {
  alignas(MEM_ALIGNMENT) size_t bytes;
};
static_assert(sizeof(struct lwip_allocation) == MEM_ALIGNMENT);

/* Requested heap bytes, including this accounting header. Includes pbufs,
 * segment/PCB/timer metadata and connection records, not just payload sizes.
 * TLSF's own overhead and shared NIC/software-packet budgets are separate. */
static struct {
  size_t bytes, peak_bytes, allocations;
} lwip_memory;

void *caelum_lwip_malloc(size_t bytes)
{
  caelum_lwip_assert_context();
  if (bytes > SIZE_MAX - sizeof(struct lwip_allocation)) {
    return NULL;
  }
  size_t charged = bytes + sizeof(struct lwip_allocation);
  if (charged > SIZE_MAX - lwip_memory.bytes) {
    return NULL;
  }
  uint64_t flags = cpu_save_interrupts();
  struct lwip_allocation *allocation = kmalloc(charged);
  cpu_restore_interrupts(flags);
  if (!allocation) {
    return NULL;
  }
  allocation->bytes = charged;
  lwip_memory.bytes += charged;
  ++lwip_memory.allocations;
  if (lwip_memory.bytes > lwip_memory.peak_bytes) {
    lwip_memory.peak_bytes = lwip_memory.bytes;
  }
  return allocation + 1;
}

void *caelum_lwip_calloc(size_t count, size_t bytes)
{
  if (count && bytes > SIZE_MAX / count) {
    return NULL;
  }
  size_t length = count * bytes;
  void *pointer = caelum_lwip_malloc(length);
  if (pointer) {
    memset(pointer, 0, length);
  }
  return pointer;
}

void caelum_lwip_free(void *pointer)
{
  caelum_lwip_assert_context();
  if (!pointer) {
    return;
  }
  struct lwip_allocation *allocation = (struct lwip_allocation *)pointer - 1;
  lwip_memory.bytes -= allocation->bytes;
  --lwip_memory.allocations;
  uint64_t flags = cpu_save_interrupts();
  kfree(allocation);
  cpu_restore_interrupts(flags);
}

u32_t sys_now(void)
{
  /* lwIP compares modulo-32-bit millisecond values. Do not convert its wrapped
   * absolute timestamps directly to Caelum's 64-bit nanosecond deadlines. */
  return (u32_t)(arch_monotonic_ns() / UINT64_C(1000000));
}
