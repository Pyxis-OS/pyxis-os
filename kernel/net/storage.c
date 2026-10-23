#include <arch/cpu.h>
#include <kernel/mm/heap.h>
#include <kernel/net/storage.h>
#include <kernel/panic.h>
#include <stdatomic.h>
#include <stdint.h>

struct net_storage_allocation {
  alignas(max_align_t) size_t bytes;
  bool protocol;
};

static atomic_flag storage_locked = ATOMIC_FLAG_INIT;
static size_t storage_bytes, protocol_bytes, receive_reserved_bytes;

static uint64_t lock_storage(void)
{
  uint64_t flags = cpu_save_interrupts();
  while (atomic_flag_test_and_set_explicit(&storage_locked, memory_order_acquire)) {
    cpu_relax();
  }
  return flags;
}

static void unlock_storage(uint64_t flags)
{
  atomic_flag_clear_explicit(&storage_locked, memory_order_release);
  cpu_restore_interrupts(flags);
}

static bool charge_storage(size_t bytes, bool protocol)
{
  uint64_t flags = lock_storage();
  size_t limit = protocol ? receive_reserved_bytes + NET_STORAGE_CONTROL_RESERVE_BYTES :
      NET_STORAGE_LIMIT_BYTES - NET_STORAGE_CONTROL_RESERVE_BYTES - receive_reserved_bytes;
  size_t used = protocol ? protocol_bytes : storage_bytes - protocol_bytes;
  bool admitted = used <= limit && bytes <= limit - used;
  if (admitted) {
    storage_bytes += bytes;
    if (protocol) {
      protocol_bytes += bytes;
    }
    KASSERT(storage_bytes <= NET_STORAGE_LIMIT_BYTES);
  }
  unlock_storage(flags);
  return admitted;
}

bool net_storage_charge(size_t bytes)
{
  return charge_storage(bytes, false);
}

static void uncharge_storage(size_t bytes, bool protocol)
{
  uint64_t flags = lock_storage();
  KASSERT(bytes <= (protocol ? protocol_bytes : storage_bytes - protocol_bytes));
  storage_bytes -= bytes;
  if (protocol) {
    protocol_bytes -= bytes;
  }
  unlock_storage(flags);
}

void net_storage_uncharge(size_t bytes)
{
  uncharge_storage(bytes, false);
}

bool net_storage_reserve_receive(size_t bytes)
{
  uint64_t flags = lock_storage();
  size_t limit = NET_STORAGE_LIMIT_BYTES - NET_STORAGE_CONTROL_RESERVE_BYTES - receive_reserved_bytes;
  size_t used = storage_bytes - protocol_bytes;
  bool admitted = used <= limit && bytes <= limit - used;
  if (admitted) {
    receive_reserved_bytes += bytes;
  }
  unlock_storage(flags);
  return admitted;
}

void net_storage_release_receive(size_t bytes)
{
  uint64_t flags = lock_storage();
  KASSERT(bytes <= receive_reserved_bytes);
  KASSERT(protocol_bytes <= receive_reserved_bytes - bytes + NET_STORAGE_CONTROL_RESERVE_BYTES);
  receive_reserved_bytes -= bytes;
  unlock_storage(flags);
}

static void *allocate_storage(size_t bytes, bool protocol)
{
  if (!bytes || bytes > SIZE_MAX - sizeof(struct net_storage_allocation)) {
    return NULL;
  }
  size_t charged = sizeof(struct net_storage_allocation) + bytes;
  if (!charge_storage(charged, protocol)) {
    return NULL;
  }
  uint64_t flags = cpu_save_interrupts();
  struct net_storage_allocation *allocation = kmalloc(charged);
  cpu_restore_interrupts(flags);
  if (!allocation) {
    uncharge_storage(charged, protocol);
    return NULL;
  }
  allocation->bytes = charged;
  allocation->protocol = protocol;
  return allocation + 1;
}

void *net_storage_alloc(size_t bytes)
{
  return allocate_storage(bytes, false);
}

void *net_storage_protocol_alloc(size_t bytes)
{
  return allocate_storage(bytes, true);
}

void net_storage_free(void *pointer)
{
  if (!pointer) {
    return;
  }
  struct net_storage_allocation *allocation = (struct net_storage_allocation *)pointer - 1;
  size_t charged = allocation->bytes;
  bool protocol = allocation->protocol;
  uint64_t flags = cpu_save_interrupts();
  kfree(allocation);
  cpu_restore_interrupts(flags);
  uncharge_storage(charged, protocol);
}
