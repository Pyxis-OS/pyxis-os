#include <lwip/opt.h>
#include <lwip/sys.h>
#include <lwip/priv/tcp_priv.h>
#include <caelum_hooks.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/net/storage.h>
#include <kernel/net/interface.h>

void caelum_lwip_assert_context(void)
{
  net_worker_assert_context();
}

static bool protocol_allocations;

bool caelum_lwip_set_protocol_allocations(bool allowed)
{
  caelum_lwip_assert_context();
  bool previous = protocol_allocations;
  protocol_allocations = allowed;
  return previous;
}

err_t caelum_lwip_split_unsent(struct tcp_pcb *pcb, u16_t split)
{
  bool previous = caelum_lwip_set_protocol_allocations(false);
  err_t result = tcp_split_unsent_seg(pcb, split);
  caelum_lwip_set_protocol_allocations(previous);
  return result;
}

void *caelum_lwip_malloc(size_t bytes)
{
  caelum_lwip_assert_context();
  return protocol_allocations ? net_storage_protocol_alloc(bytes) : net_storage_alloc(bytes);
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
  net_storage_free(pointer);
}

u32_t sys_now(void)
{
  /* lwIP compares modulo-32-bit millisecond values. Do not convert its wrapped
   * absolute timestamps directly to Caelum's 64-bit nanosecond deadlines. */
  return (u32_t)(arch_monotonic_ns() / UINT64_C(1000000));
}
