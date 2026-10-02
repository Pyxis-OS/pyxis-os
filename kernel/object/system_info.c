#include <arch/cpu.h>
#include <arch/cpu_info.h>
#include <arch/smp.h>
#include <kernel-build-revision.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmm.h>
#include <kernel/object/system_info.h>
#include <kernel/panic.h>
#include <kernel/pci.h>
#include <kernel/user_memory.h>

static const struct system_info_identity identity = {
  .os_name = "Pyxis OS",
  .kernel_name = "Caelum",
  .architecture = "x86_64",
  .build_revision = KERNEL_BUILD_REVISION,
};
/* Published through scheduler startup; no CPU hotplug is supported. */
static struct system_info_cpu cpu;

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
  size_t size;
  size_t payload_size = 0;
  const void *reply;
  struct system_info_pci pci;
  struct system_info_pci_function function;
  struct system_info_pci_function_request function_request = {0};
  switch (operation) {
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

  struct system_info_memory memory;
  if (operation == SYSTEM_INFO_MEMORY) {
    struct system_info_memory_request *request =
        (struct system_info_memory_request *)bsp_request_prepare(BSP_SERVICE_SYSTEM_INFO_MEMORY);
    bsp_request_submit_and_wait(&request->request);
    memory = request->reply;
    bsp_request_release(&request->request);
    reply = &memory;
  }
  if (!copy_to_user(reply_address, reply, size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, size};
}
