#include <lwip/opt.h>
#include <lwip/sys.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>

void caelum_lwip_assert_context(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  cpu_restore_interrupts(flags);
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
}

void *caelum_lwip_malloc(size_t bytes)
{
  caelum_lwip_assert_context();
  uint64_t flags = cpu_save_interrupts();
  void *pointer = kmalloc(bytes);
  cpu_restore_interrupts(flags);
  return pointer;
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
  uint64_t flags = cpu_save_interrupts();
  kfree(pointer);
  cpu_restore_interrupts(flags);
}

u32_t sys_now(void)
{
  /* lwIP compares modulo-32-bit millisecond values. Do not convert its wrapped
   * absolute timestamps directly to Caelum's 64-bit nanosecond deadlines. */
  return (u32_t)(arch_monotonic_ns() / UINT64_C(1000000));
}
