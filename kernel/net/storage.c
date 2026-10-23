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

static bool charge_storage(size_t bytes, size_t limit)
{
  size_t current = atomic_load_explicit(&storage_bytes, memory_order_relaxed);
  for (;;) {
    if (current > limit || bytes > limit - current) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&storage_bytes, &current,
        current + bytes, memory_order_relaxed, memory_order_relaxed)) {
      return true;
    }
  }
}

bool net_storage_charge(size_t bytes)
{
  return charge_storage(bytes, NET_STORAGE_LIMIT_BYTES - NET_STORAGE_PROTOCOL_HEADROOM_BYTES);
}

void net_storage_uncharge(size_t bytes)
{
  size_t previous = atomic_fetch_sub_explicit(&storage_bytes, bytes, memory_order_relaxed);
  KASSERT(bytes <= previous);
}

static void *allocate_storage(size_t bytes, size_t limit)
{
  if (!bytes || bytes > SIZE_MAX - sizeof(struct net_storage_allocation)) {
    return NULL;
  }
  size_t charged = sizeof(struct net_storage_allocation) + bytes;
  if (!charge_storage(charged, limit)) {
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

void *net_storage_alloc(size_t bytes)
{
  return allocate_storage(bytes, NET_STORAGE_LIMIT_BYTES - NET_STORAGE_PROTOCOL_HEADROOM_BYTES);
}

void *net_storage_protocol_alloc(size_t bytes)
{
  return allocate_storage(bytes, NET_STORAGE_LIMIT_BYTES);
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
