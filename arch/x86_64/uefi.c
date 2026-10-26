#include <arch/cpu.h>
#include <arch/uefi.h>
#include <kernel/panic.h>
#include <stdatomic.h>

typedef uint64_t (__attribute__((ms_abi)) *efi_get_variable_fn)(
    const uint16_t *, const struct efi_guid *, uint32_t *, size_t *, void *);
typedef uint64_t (__attribute__((ms_abi)) *efi_set_variable_fn)(
    const uint16_t *, const struct efi_guid *, uint32_t, size_t, const void *);

static efi_get_variable_fn get_variable;
static efi_set_variable_fn set_variable;
static atomic_bool firmware_busy;

void arch_uefi_enable(uintptr_t get, uintptr_t set)
{
  KASSERT(get && set && !get_variable && !set_variable);
  get_variable = (efi_get_variable_fn)get;
  set_variable = (efi_set_variable_fn)set;
}

bool arch_uefi_available(void)
{
  return get_variable && set_variable;
}

enum arch_uefi_gate_result arch_uefi_begin(uint64_t *interrupt_flags)
{
  uint64_t flags = cpu_save_interrupts();
  if (!arch_uefi_available()) {
    cpu_restore_interrupts(flags);
    return ARCH_UEFI_UNAVAILABLE;
  }
  bool idle = false;
  if (!atomic_compare_exchange_strong_explicit(&firmware_busy, &idle, true,
      memory_order_acquire, memory_order_relaxed)) {
    cpu_restore_interrupts(flags);
    return ARCH_UEFI_BUSY;
  }
  *interrupt_flags = flags;
  return ARCH_UEFI_READY;
}

void arch_uefi_end(uint64_t interrupt_flags)
{
  atomic_store_explicit(&firmware_busy, false, memory_order_release);
  cpu_restore_interrupts(interrupt_flags);
}

uint64_t arch_uefi_get_variable(const uint16_t *name, const struct efi_guid *guid,
    uint32_t *attributes, size_t *size, void *bytes)
{
  KASSERT(atomic_load_explicit(&firmware_busy, memory_order_relaxed));
  return get_variable(name, guid, attributes, size, bytes);
}

uint64_t arch_uefi_set_variable(const uint16_t *name, const struct efi_guid *guid,
    uint32_t attributes, size_t size, const void *bytes)
{
  KASSERT(atomic_load_explicit(&firmware_busy, memory_order_relaxed));
  return set_variable(name, guid, attributes, size, bytes);
}
