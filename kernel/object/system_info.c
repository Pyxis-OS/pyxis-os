#include <arch/cpu.h>
#include <arch/cpu_info.h>
#include <arch/smp.h>
#include <kernel-build-revision.h>
#include <kernel/acpi.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/object/system_info.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include <kernel/user_memory.h>
#include <stdatomic.h>
#include "../usb/core.h"

static const struct system_info_identity identity = {
  .os_name = "Pyxis OS",
  .kernel_name = "Caelum",
  .architecture = "x86_64",
  .build_revision = KERNEL_BUILD_REVISION,
};
/* Published through scheduler startup; no CPU hotplug is supported. */
static struct system_info_cpu cpu;
static const struct system_info_hostname default_hostname = {.name = "pyxis"};
/* Only BSP writes hostname, before release publication. Readers use the separate
 * default until acquire observes publication, then the record is immutable. */
static struct system_info_hostname hostname;
static atomic_bool hostname_published;

void
system_info_init(void)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  _Static_assert(sizeof(cpu.brand) >= ARCH_CPU_BRAND_BYTES + 1, "CPU brand capacity");
  arch_cpu_brand(cpu.brand);
  cpu.online_count = arch_cpu_count();
  KASSERT(cpu.online_count);
}

static void
destroy_system_info(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *
system_info_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_SYSTEM_INFO, destroy_system_info);
  }
  return object;
}

void
system_info_memory_execute(struct system_info_memory_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct pmm_stats stats = pmm_get_stats();
  /* PMM bounds physical coverage to 64 GiB, so byte conversion cannot overflow. */
  request->reply = (struct system_info_memory){
    .total_bytes = stats.total_frames * PAGE_SIZE,
    .allocated_bytes = stats.allocated_frames * PAGE_SIZE,
    .free_bytes = stats.free_frames * PAGE_SIZE,
  };
}

void
system_info_power_execute(struct system_info_power_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (request->operation == SYSTEM_INFO_POWER) {
    request->reply.power = acpi_power_state();
    request->found = true;
  } else {
    request->found = acpi_battery_read(request->index, &request->reply.battery);
  }
}

void
system_info_hostname_execute(struct system_info_hostname_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (atomic_load_explicit(&hostname_published, memory_order_relaxed)) {
    request->status = CALL_DENIED;
    return;
  }
  size_t length = 0;
  while (length < sizeof(request->value.name) && request->value.name[length]) {
    unsigned char character = request->value.name[length];
    if (character < 0x20 || character > 0x7e) {
      request->status = CALL_BAD_REQUEST;
      return;
    }
    ++length;
  }
  if (!length || length == sizeof(request->value.name)) {
    request->status = CALL_BAD_REQUEST;
    return;
  }
  hostname = (struct system_info_hostname){0};
  memcpy(hostname.name, request->value.name, length);
  atomic_store_explicit(&hostname_published, true, memory_order_release);
  request->status = CALL_OK;
}

static struct syscall_result
set_hostname(uint64_t rights, uintptr_t request_address, size_t request_size)
{
  if (!(rights & SYSTEM_INFO_RIGHT_SET_HOSTNAME_ONCE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size != sizeof(struct system_info_hostname)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct system_info_hostname value;
  if (!copy_from_user(&value, request_address, sizeof(value))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct system_info_hostname_request *request = (void *)
      bsp_request_prepare(BSP_SERVICE_SYSTEM_INFO_HOSTNAME);
  request->value = value;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  bsp_request_release(&request->request);
  return (struct syscall_result){status, 0};
}

static uint64_t
pci_state(void)
{
  switch (pci_inventory_state()) {
  case PCI_INVENTORY_UNAVAILABLE:
    return SYSTEM_INFO_PCI_UNAVAILABLE;
  case PCI_INVENTORY_INCOMPLETE:
    return SYSTEM_INFO_PCI_INCOMPLETE;
  case PCI_INVENTORY_COMPLETE:
    return SYSTEM_INFO_PCI_COMPLETE;
  }
  panic("unknown PCI inventory state");
}

struct syscall_result
system_info_call(uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == SYSTEM_INFO_HOSTNAME && request_size) {
    return set_hostname(rights, request_address, request_size);
  }
  size_t size;
  size_t payload_size = 0;
  const void *reply;
  struct system_info_usb usb;
  struct system_info_usb_controller usb_controller;
  struct system_info_usb_device usb_device;
  struct system_info_usb_interface usb_interface;
  struct system_info_usb_request usb_request = {0};
  struct system_info_pci pci;
  struct system_info_pci_function function;
  struct system_info_pci_function_request function_request = {0};
  uint64_t battery_index = 0;
  switch (operation) {
  case SYSTEM_INFO_HOSTNAME:
    size = sizeof(hostname);
    reply = atomic_load_explicit(&hostname_published, memory_order_acquire) ?
        &hostname : &default_hostname;
    break;
  case SYSTEM_INFO_IDENTITY:
    size = sizeof(identity);
    reply = &identity;
    break;
  case SYSTEM_INFO_CPU:
    size = sizeof(cpu);
    reply = &cpu;
    break;
  case SYSTEM_INFO_MEMORY:
    size = sizeof(struct system_info_memory);
    reply = NULL;
    break;
  case SYSTEM_INFO_PCI:
    size = sizeof(pci);
    reply = &pci;
    break;
  case SYSTEM_INFO_PCI_FUNCTION:
    size = sizeof(function);
    payload_size = sizeof(function_request) - sizeof(function_request.header);
    reply = &function;
    break;
  case SYSTEM_INFO_USB:
    size = sizeof(usb);
    reply = &usb;
    break;
  case SYSTEM_INFO_USB_CONTROLLER:
    size = sizeof(usb_controller);
    payload_size = sizeof(usb_request.index);
    reply = &usb_controller;
    break;
  case SYSTEM_INFO_USB_DEVICE:
    size = sizeof(usb_device);
    payload_size = sizeof(usb_request.index);
    reply = &usb_device;
    break;
  case SYSTEM_INFO_USB_INTERFACE:
    size = sizeof(usb_interface);
    payload_size = sizeof(usb_request.index);
    reply = &usb_interface;
    break;
  case SYSTEM_INFO_POWER:
    size = sizeof(struct system_info_power);
    reply = NULL;
    break;
  case SYSTEM_INFO_BATTERY:
    size = sizeof(struct system_info_battery);
    payload_size = sizeof(battery_index);
    reply = NULL;
    break;
  default:
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & SYSTEM_INFO_RIGHT_READ)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size != payload_size || reply_capacity < size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }

  /* The PCI inventory is immutable before user tasks run; no BSP request. */
  if (operation == SYSTEM_INFO_PCI) {
    pci = (struct system_info_pci){
      .state = pci_state(), .function_count = pci_device_count(),
    };
  }
  if (operation == SYSTEM_INFO_PCI_FUNCTION) {
    if (!copy_from_user(&function_request.index, request_address, payload_size)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    const struct pci_device *device = function_request.index < pci_device_count() ?
        pci_device_at(function_request.index) : NULL;
    if (!device) {
      return (struct syscall_result){CALL_NOT_FOUND, 0};
    }
    function = (struct system_info_pci_function){
      .bus = device->address.bus, .device = device->address.device,
      .function = device->address.function, .header_type = device->header_type,
      .vendor_id = device->vendor_id, .device_id = device->device_id,
      .base_class = device->base_class, .subclass = device->subclass,
      .interface = device->interface, .revision = device->revision,
    };
  }

  if (operation == SYSTEM_INFO_USB) {
    usb_inventory_read(&usb);
  }
  if (operation == SYSTEM_INFO_USB_CONTROLLER || operation == SYSTEM_INFO_USB_DEVICE ||
      operation == SYSTEM_INFO_USB_INTERFACE) {
    if (!copy_from_user(&usb_request.index, request_address, payload_size)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    bool found;
    switch (operation) {
    case SYSTEM_INFO_USB_CONTROLLER:
      found = usb_inventory_read_controller(usb_request.index, &usb_controller);
      break;
    case SYSTEM_INFO_USB_DEVICE:
      found = usb_inventory_read_device(usb_request.index, &usb_device);
      break;
    default:
      found = usb_inventory_read_interface(usb_request.index, &usb_interface);
      break;
    }
    if (!found) {
      return (struct syscall_result){CALL_NOT_FOUND, 0};
    }
  }

  struct system_info_memory memory;
  if (operation == SYSTEM_INFO_MEMORY) {
    struct system_info_memory_request *request =
        (struct system_info_memory_request *)bsp_request_prepare(BSP_SERVICE_SYSTEM_INFO_MEMORY);
    bsp_request_submit_and_wait(&request->request);
    memory = request->reply;
    bsp_request_release(&request->request);
    reply = &memory;
  }
  struct system_info_power power;
  struct system_info_battery battery;
  if (operation == SYSTEM_INFO_POWER || operation == SYSTEM_INFO_BATTERY) {
    if (operation == SYSTEM_INFO_BATTERY &&
        !copy_from_user(&battery_index, request_address, payload_size)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    struct system_info_power_request *request =
        (struct system_info_power_request *)bsp_request_prepare(BSP_SERVICE_SYSTEM_INFO_POWER);
    request->operation = operation;
    request->index = battery_index;
    bsp_request_submit_and_wait(&request->request);
    bool found = request->found;
    if (operation == SYSTEM_INFO_POWER) {
      power = request->reply.power;
    } else {
      battery = request->reply.battery;
    }
    bsp_request_release(&request->request);
    if (!found) {
      return (struct syscall_result){CALL_NOT_FOUND, 0};
    }
    reply = operation == SYSTEM_INFO_POWER ? (const void *)&power : (const void *)&battery;
  }
  if (!copy_to_user(reply_address, reply, size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, size};
}
