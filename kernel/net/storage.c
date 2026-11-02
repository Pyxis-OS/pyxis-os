#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/net/storage.h>
#include <kernel/panic.h>
#include <stdatomic.h>
#include <stdint.h>

struct net_storage_allocation {
  alignas(max_align_t) size_t bytes;
  bool protocol;
};

/* Ordinary actual + reserved RX + control <= limit; protocol actual consumes
 * those preclaimed RX/control credits. This guard never covers heap work. */
static atomic_flag storage_locked = ATOMIC_FLAG_INIT;
static size_t storage_bytes, protocol_bytes, receive_reserved_bytes;
static size_t storage_limit = NET_STORAGE_MIN_BYTES;
/* Admissions refused since the last report; reported outside this guard because
 * the log can itself be sent over the network. */
static atomic_size_t refused_admissions;

void net_storage_init(void)
{
  uint64_t share = (uint64_t)pmm_get_stats().total_frames * PAGE_SIZE / NET_STORAGE_RAM_DIVISOR;
  storage_limit = share > NET_STORAGE_MIN_BYTES ? (size_t)share : NET_STORAGE_MIN_BYTES;
}

size_t net_storage_limit(void)
{
  return storage_limit;
}

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

void net_storage_report(void)
{
  static uint64_t last_report_ns;
  uint64_t now = arch_monotonic_ns();
  if (!atomic_load_explicit(&refused_admissions, memory_order_relaxed) ||
      now - last_report_ns < UINT64_C(1000000000)) {
    return;
  }
  size_t refused = atomic_exchange_explicit(&refused_admissions, 0, memory_order_relaxed);
  uint64_t flags = lock_storage();
  size_t used = storage_bytes + receive_reserved_bytes;
  unlock_storage(flags);
  last_report_ns = now;
  kwarn("net: %zu admissions refused, %zu of %zu bytes of network storage in use "
      "(one sixteenth of installed RAM, at least 64 MiB)\n", refused, used, storage_limit);
}

static bool charge_storage(size_t bytes, bool protocol)
{
  uint64_t flags = lock_storage();
  size_t limit = protocol ? receive_reserved_bytes + NET_STORAGE_CONTROL_RESERVE_BYTES :
      storage_limit - NET_STORAGE_CONTROL_RESERVE_BYTES - receive_reserved_bytes;
  size_t used = protocol ? protocol_bytes : storage_bytes - protocol_bytes;
  bool admitted = used <= limit && bytes <= limit - used;
  if (admitted) {
    storage_bytes += bytes;
    if (protocol) {
      protocol_bytes += bytes;
    }
    KASSERT(storage_bytes <= storage_limit);
  } else {
    atomic_fetch_add_explicit(&refused_admissions, 1, memory_order_relaxed);
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
  size_t limit = storage_limit - NET_STORAGE_CONTROL_RESERVE_BYTES - receive_reserved_bytes;
  size_t used = storage_bytes - protocol_bytes;
  bool admitted = used <= limit && bytes <= limit - used;
  if (admitted) {
    receive_reserved_bytes += bytes;
  } else {
    atomic_fetch_add_explicit(&refused_admissions, 1, memory_order_relaxed);
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
  uint64_t flags = cpu_save_interrupts();
  if (!charge_storage(charged, protocol)) {
    cpu_restore_interrupts(flags);
    return NULL;
  }
  struct net_storage_allocation *allocation = kmalloc(charged);
  if (!allocation) {
    uncharge_storage(charged, protocol);
    cpu_restore_interrupts(flags);
    return NULL;
  }
  allocation->bytes = charged;
  allocation->protocol = protocol;
  cpu_restore_interrupts(flags);
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

bool net_storage_make_ordinary(void *pointer)
{
  struct net_storage_allocation *allocation = (struct net_storage_allocation *)pointer - 1;
  uint64_t flags = lock_storage();
  bool admitted = true;
  if (allocation->protocol) {
    size_t limit = storage_limit - NET_STORAGE_CONTROL_RESERVE_BYTES - receive_reserved_bytes;
    size_t used = storage_bytes - protocol_bytes;
    admitted = used <= limit && allocation->bytes <= limit - used;
    if (admitted) {
      protocol_bytes -= allocation->bytes;
      allocation->protocol = false;
    } else {
      atomic_fetch_add_explicit(&refused_admissions, 1, memory_order_relaxed);
    }
  }
  unlock_storage(flags);
  return admitted;
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
  uncharge_storage(charged, protocol);
  cpu_restore_interrupts(flags);
}
