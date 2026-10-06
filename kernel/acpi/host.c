#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/pci.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <uacpi/kernel_api.h>
#include "host.h"

/* uACPI's kernel interface on Caelum primitives. Only the ACPI worker calls
 * uACPI, so locks and waits never contend with another thread: a held mutex
 * belongs to the caller, and only an SCI serviced during a wait can signal an
 * event. Allocation, mapping and logging follow the BSP kernel-task rules. */

#define WAIT_FOREVER 0xffff
#define IO_PORT_LIMIT 0x10000

struct pci_function {
  struct pci_address address;
};

struct io_range {
  uint16_t base;
  uint32_t length;
};

struct acpi_mutex {
  bool held;
};

struct acpi_event {
  uint64_t count;
};

static struct acpi_heap_use heap_use;

void *uacpi_kernel_map(uacpi_phys_addr address, uacpi_size length)
{
  void *mapping = acpi_map(address, length);
  return mapping ? mapping : UACPI_MAP_FAILED;
}

void uacpi_kernel_unmap(void *address, uacpi_size length)
{
  acpi_unmap(address, length);
}

/* uACPI's informational lines (the table list and load statistics) go to the
 * trace log; the worker prints one summary line instead. */
void uacpi_kernel_log(uacpi_log_level level, const uacpi_char *message)
{
  switch (level) {
  case UACPI_LOG_ERROR:
    klog("ACPI: error: %s", message);
    break;
  case UACPI_LOG_WARN:
    klog("ACPI: warning: %s", message);
    break;
  default:
    ktrace("ACPI: %s", message);
    break;
  }
}

/* Allocation runs with IF=0 like other BSP kernel-task heap use. uACPI may
 * request empty buffers, for which NULL would mean exhaustion. */
void *uacpi_kernel_alloc(uacpi_size size)
{
  uint64_t flags = cpu_save_interrupts();
  void *memory = kmalloc(size ? size : 1);
  if (memory) {
    heap_use.bytes += size;
    ++heap_use.blocks;
  }
  cpu_restore_interrupts(flags);
  return memory;
}

void uacpi_kernel_free(void *memory, uacpi_size size)
{
  if (!memory) {
    return;
  }
  /* The size is uACPI's hint; accounting saturates rather than trusting it. */
  uint64_t flags = cpu_save_interrupts();
  heap_use.bytes -= size < heap_use.bytes ? size : heap_use.bytes;
  heap_use.blocks -= heap_use.blocks ? 1 : 0;
  kfree(memory);
  cpu_restore_interrupts(flags);
}

struct acpi_heap_use acpi_heap_use(void)
{
  return heap_use;
}

/* PCI configuration reads use the ECAM aperture. Writes are refused: config
 * pages of unowned functions are read-only, and AML writes to functions owned
 * by drivers would bypass their ownership. */
uacpi_status uacpi_kernel_pci_device_open(uacpi_pci_address address, uacpi_handle *handle)
{
  acpi_require_worker();
  if (address.segment || address.bus >= arch_pci_bus_count() ||
      address.device >= PCI_DEVICE_COUNT || address.function >= PCI_FUNCTION_COUNT) {
    klog("ACPI: PCI %u:%u:%u.%u is outside the ECAM aperture\n",
         address.segment, address.bus, address.device, address.function);
    return UACPI_STATUS_UNIMPLEMENTED;
  }

  struct pci_function *function = uacpi_kernel_alloc(sizeof(*function));
  if (!function) {
    return UACPI_STATUS_OUT_OF_MEMORY;
  }
  function->address = (struct pci_address){
    .bus = address.bus,
    .device = address.device,
    .function = address.function,
  };
  *handle = function;
  return UACPI_STATUS_OK;
}

void uacpi_kernel_pci_device_close(uacpi_handle handle)
{
  uacpi_kernel_free(handle, sizeof(struct pci_function));
}

static bool pci_access_valid(uacpi_size offset, size_t bytes)
{
  return offset <= PCI_CONFIG_BYTES - bytes && !(offset % bytes);
}

uacpi_status uacpi_kernel_pci_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *value)
{
  const struct pci_function *function = handle;
  if (!pci_access_valid(offset, sizeof(*value))) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  *value = pci_read8(function->address, offset);
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *value)
{
  const struct pci_function *function = handle;
  if (!pci_access_valid(offset, sizeof(*value))) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  *value = pci_read16(function->address, offset);
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_pci_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *value)
{
  const struct pci_function *function = handle;
  if (!pci_access_valid(offset, sizeof(*value))) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  *value = pci_read32(function->address, offset);
  return UACPI_STATUS_OK;
}

static uacpi_status refuse_pci_write(uacpi_handle handle, uacpi_size offset,
                                     uint32_t value, unsigned bytes)
{
  const struct pci_function *function = handle;
  klog("ACPI: refused %u-byte PCI config write to %u:%u.%u offset 0x%zx value 0x%x\n",
       bytes, function->address.bus, function->address.device,
       function->address.function, offset, value);
  return UACPI_STATUS_DENIED;
}

uacpi_status uacpi_kernel_pci_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value)
{
  return refuse_pci_write(handle, offset, value, sizeof(value));
}

uacpi_status uacpi_kernel_pci_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value)
{
  return refuse_pci_write(handle, offset, value, sizeof(value));
}

uacpi_status uacpi_kernel_pci_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value)
{
  return refuse_pci_write(handle, offset, value, sizeof(value));
}

/* Port ranges belong to firmware descriptions; no port is withheld from AML. */
uacpi_status uacpi_kernel_io_map(uacpi_io_addr base, uacpi_size length, uacpi_handle *handle)
{
  acpi_require_worker();
  if (!length || base >= IO_PORT_LIMIT || length > IO_PORT_LIMIT - base) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }

  struct io_range *range = uacpi_kernel_alloc(sizeof(*range));
  if (!range) {
    return UACPI_STATUS_OUT_OF_MEMORY;
  }
  *range = (struct io_range){.base = base, .length = length};
  *handle = range;
  return UACPI_STATUS_OK;
}

void uacpi_kernel_io_unmap(uacpi_handle handle)
{
  uacpi_kernel_free(handle, sizeof(struct io_range));
}

/* Accesses keep their exact width; the offset and width must fit the range. */
static bool io_port(uacpi_handle handle, uacpi_size offset, unsigned bytes, uint16_t *port)
{
  const struct io_range *range = handle;
  if (offset >= range->length || bytes > range->length - offset) {
    return false;
  }
  *port = range->base + offset;
  return true;
}

uacpi_status uacpi_kernel_io_read8(uacpi_handle handle, uacpi_size offset, uacpi_u8 *value)
{
  uint16_t port;
  if (!io_port(handle, offset, sizeof(*value), &port)) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  *value = inb(port);
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read16(uacpi_handle handle, uacpi_size offset, uacpi_u16 *value)
{
  uint16_t port;
  if (!io_port(handle, offset, sizeof(*value), &port)) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  *value = inw(port);
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_read32(uacpi_handle handle, uacpi_size offset, uacpi_u32 *value)
{
  uint16_t port;
  if (!io_port(handle, offset, sizeof(*value), &port)) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  *value = inl(port);
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write8(uacpi_handle handle, uacpi_size offset, uacpi_u8 value)
{
  uint16_t port;
  if (!io_port(handle, offset, sizeof(value), &port)) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  outb(port, value);
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write16(uacpi_handle handle, uacpi_size offset, uacpi_u16 value)
{
  uint16_t port;
  if (!io_port(handle, offset, sizeof(value), &port)) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  outw(port, value);
  return UACPI_STATUS_OK;
}

uacpi_status uacpi_kernel_io_write32(uacpi_handle handle, uacpi_size offset, uacpi_u32 value)
{
  uint16_t port;
  if (!io_port(handle, offset, sizeof(value), &port)) {
    return UACPI_STATUS_INVALID_ARGUMENT;
  }
  outl(port, value);
  return UACPI_STATUS_OK;
}

uacpi_u64 uacpi_kernel_get_nanoseconds_since_boot(void)
{
  return arch_monotonic_ns();
}

void uacpi_kernel_stall(uacpi_u8 microseconds)
{
  uint64_t end = arch_monotonic_ns() + (uint64_t)microseconds * 1000;
  while (arch_monotonic_ns() < end) {
    __asm__ volatile("pause");
  }
}

static uint64_t deadline_after(uacpi_u64 milliseconds)
{
  return task_deadline_after_ms(milliseconds > UINT32_MAX ? UINT32_MAX : (uint32_t)milliseconds);
}

void uacpi_kernel_sleep(uacpi_u64 milliseconds)
{
  acpi_require_worker();
  uint64_t deadline = deadline_after(milliseconds);
  while (!task_deadline_expired(deadline)) {
    acpi_worker_block(deadline);
  }
}

uacpi_handle uacpi_kernel_create_mutex(void)
{
  struct acpi_mutex *mutex = uacpi_kernel_alloc(sizeof(*mutex));
  if (mutex) {
    *mutex = (struct acpi_mutex){0};
  }
  return mutex;
}

void uacpi_kernel_free_mutex(uacpi_handle handle)
{
  uacpi_kernel_free(handle, sizeof(struct acpi_mutex));
}

uacpi_status uacpi_kernel_acquire_mutex(uacpi_handle handle, uacpi_u16 timeout)
{
  acpi_require_worker();
  struct acpi_mutex *mutex = handle;
  if (!mutex->held) {
    mutex->held = true;
    return UACPI_STATUS_OK;
  }

  /* The holder is the worker itself, so waiting cannot succeed. */
  if (timeout == WAIT_FOREVER) {
    klog("ACPI: error: endless wait for a mutex the worker already holds\n");
    return UACPI_STATUS_INTERNAL_ERROR;
  }
  uacpi_kernel_sleep(timeout);
  return UACPI_STATUS_TIMEOUT;
}

void uacpi_kernel_release_mutex(uacpi_handle handle)
{
  struct acpi_mutex *mutex = handle;
  KASSERT(mutex->held);
  mutex->held = false;
}

uacpi_handle uacpi_kernel_create_event(void)
{
  struct acpi_event *event = uacpi_kernel_alloc(sizeof(*event));
  if (event) {
    *event = (struct acpi_event){0};
  }
  return event;
}

void uacpi_kernel_free_event(uacpi_handle handle)
{
  uacpi_kernel_free(handle, sizeof(struct acpi_event));
}

uacpi_bool uacpi_kernel_wait_for_event(uacpi_handle handle, uacpi_u16 timeout)
{
  acpi_require_worker();
  struct acpi_event *event = handle;
  uint64_t deadline = timeout == WAIT_FOREVER ? UINT64_MAX : deadline_after(timeout);
  for (;;) {
    if (event->count) {
      --event->count;
      return UACPI_TRUE;
    }
    if (deadline != UINT64_MAX && task_deadline_expired(deadline)) {
      return UACPI_FALSE;
    }
    acpi_worker_block(deadline);
  }
}

/* Signalled from AML or from the SCI handler, both on the worker. */
void uacpi_kernel_signal_event(uacpi_handle handle)
{
  struct acpi_event *event = handle;
  ++event->count;
}

void uacpi_kernel_reset_event(uacpi_handle handle)
{
  struct acpi_event *event = handle;
  event->count = 0;
}

uacpi_interrupt_state uacpi_kernel_disable_interrupts(void)
{
  return cpu_save_interrupts();
}

void uacpi_kernel_restore_interrupts(uacpi_interrupt_state state)
{
  cpu_restore_interrupts(state);
}

uacpi_handle uacpi_kernel_create_spinlock(void)
{
  struct spinlock *lock = uacpi_kernel_alloc(sizeof(*lock));
  if (lock) {
    *lock = (struct spinlock){0};
  }
  return lock;
}

void uacpi_kernel_free_spinlock(uacpi_handle handle)
{
  uacpi_kernel_free(handle, sizeof(struct spinlock));
}

uacpi_cpu_flags uacpi_kernel_lock_spinlock(uacpi_handle handle)
{
  uint64_t flags = cpu_save_interrupts();
  spin_lock(handle);
  return flags;
}

void uacpi_kernel_unlock_spinlock(uacpi_handle handle, uacpi_cpu_flags flags)
{
  spin_unlock(handle);
  cpu_restore_interrupts(flags);
}

uacpi_status uacpi_kernel_handle_firmware_request(uacpi_firmware_request *request)
{
  switch (request->type) {
  case UACPI_FIRMWARE_REQUEST_TYPE_BREAKPOINT:
    klog("ACPI: AML breakpoint ignored\n");
    break;
  case UACPI_FIRMWARE_REQUEST_TYPE_FATAL:
    klog("ACPI: error: AML fatal error type 0x%x code 0x%x argument 0x%llx\n",
         request->fatal.type, request->fatal.code, (unsigned long long)request->fatal.arg);
    break;
  }
  return UACPI_STATUS_OK;
}
