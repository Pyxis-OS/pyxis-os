#include <arch/cpu.h>
#include <kernel/mm/heap.h>
#include <kernel/net/storage.h>
#include <kernel/panic.h>
#include <stdatomic.h>
#include <stdint.h>

struct net_storage_allocation {
  alignas(max_align_t) size_t bytes;
};

static atomic_size_t storage_bytes;

bool net_storage_charge(size_t bytes)
{
  size_t current = atomic_load_explicit(&storage_bytes, memory_order_relaxed);
  for (;;) {
    if (bytes > NET_STORAGE_LIMIT_BYTES - current) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&storage_bytes, &current,
        current + bytes, memory_order_relaxed, memory_order_relaxed)) {
      return true;
    }
  }
}

void net_storage_uncharge(size_t bytes)
{
  size_t previous = atomic_fetch_sub_explicit(&storage_bytes, bytes, memory_order_relaxed);
  KASSERT(bytes <= previous);
}

void *net_storage_alloc(size_t bytes)
{
  if (!bytes || bytes > SIZE_MAX - sizeof(struct net_storage_allocation)) {
    return NULL;
  }
  size_t charged = sizeof(struct net_storage_allocation) + bytes;
  if (!net_storage_charge(charged)) {
    return NULL;
  }
  uint64_t flags = cpu_save_interrupts();
  struct net_storage_allocation *allocation = kmalloc(charged);
  cpu_restore_interrupts(flags);
  if (!allocation) {
    net_storage_uncharge(charged);
    return NULL;
  }
  allocation->bytes = charged;
  return allocation + 1;
}

void net_storage_free(void *pointer)
{
  if (!pointer) {
    return;
  }
  struct net_storage_allocation *allocation = (struct net_storage_allocation *)pointer - 1;
  size_t charged = allocation->bytes;
  uint64_t flags = cpu_save_interrupts();
  kfree(allocation);
  cpu_restore_interrupts(flags);
  net_storage_uncharge(charged);
}
