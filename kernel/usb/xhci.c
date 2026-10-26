#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <arch/smp.h>
#include <kernel/format.h>
#include <kernel/bluetooth.h>
#include <kernel/log.h>
#include <kernel/boot/options.h>
#include <kernel/memory.h>
#include <kernel/mm/dma.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/pci/msi.h>
#include <kernel/pci/msix.h>
#include <kernel/pci/registers.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/usb/xhci.h>
#include "registers.h"
#include "host.h"
#include "core.h"
#include "settings.h"

#define USB_ENDPOINT_DIRECTION_IN 0x80
#define USB_ENDPOINT_NUMBER 0x0f
#define USB_ENDPOINT_RESERVED 0x70
#define USB_FULL_SPEED_INTERRUPT_PACKET_MAX 64
#define USB_MICROFRAMES_PER_FRAME 8
#define USB_REQUEST_ENDPOINT_OUT 0x02
#define USB_REQUEST_CLEAR_FEATURE 1
#define USB_FEATURE_ENDPOINT_HALT 0
#define USB_REQUEST_TT_OUT 0x23
#define USB_REQUEST_CLEAR_TT_BUFFER 8
#define USB_REQUEST_GET_DESCRIPTOR 6
#define USB_REQUEST_GET_CONFIGURATION 8
#define USB_REQUEST_SET_CONFIGURATION 9
#define USB_REQUEST_DEVICE_IN 0x80
#define USB_REQUEST_DEVICE_OUT 0x00
#define USB_DESCRIPTOR_DEVICE 1
#define USB_DEVICE_DESCRIPTOR_BYTES 18
#define USB_TT_ADDRESS_SHIFT 4
#define USB_TT_TYPE_SHIFT 11
#define USB_TT_DIRECTION_IN 0x8000
#define USB_TT_BULK_TYPE 2
#define USB_ADDRESS_MASK 0xff
#define USB_ADDRESS_MAX 127
#define XHCI_EVENT_BABBLE 3
#define XHCI_TRB_STOP_ENDPOINT 15
#define XHCI_EVENT_RESOURCE 7
#define XHCI_EVENT_BANDWIDTH 8
#define XHCI_EVENT_NO_SLOTS 9
#define XHCI_EVENT_CONTEXT_STATE 19
#define XHCI_EVENT_STOPPED 26
#define XHCI_EVENT_STOPPED_LENGTH_INVALID 27
#define XHCI_EVENT_STOPPED_SHORT 28
#define XHCI_EVENT_SECONDARY_BANDWIDTH 35
#define XHCI_ENDPOINT_STATE_MASK 7
#define XHCI_ENDPOINT_STATE_RUNNING 1
#define XHCI_ENDPOINT_STATE_HALTED 2
#define XHCI_ENDPOINT_STATE_STOPPED 3
#define XHCI_ENDPOINT_STATE_ERROR 4

#define XHCI_RING_TRBS (PAGE_SIZE / XHCI_TRB_BYTES)
#define XHCI_DIAGNOSE_INTERVAL_NS UINT64_C(5000000000)

struct xhci_trb {
  uint64_t parameter;
  uint32_t status, control;
};
struct xhci_diagnostic_registers {
  uint32_t status, command, management;
  uint64_t dequeue;
};
struct xhci_erst_entry {
  uint64_t base;
  uint32_t size, reserved;
};
_Static_assert(sizeof(struct xhci_trb) == XHCI_TRB_BYTES, "xHCI TRB hardware layout");
_Static_assert(sizeof(struct xhci_erst_entry) == XHCI_ERST_BYTES, "xHCI ERST hardware layout");

enum port_state { PORT_ABSENT, PORT_UNSUPPORTED, PORT_CONNECTED, PORT_RESERVED, PORT_REMOVED };
enum control_state { CONTROL_IDLE, CONTROL_ACTIVE, CONTROL_DONE, CONTROL_HALTED, CONTROL_HELD };
struct xhci_bulk_endpoint {
  struct dma_buffer ring;
  struct usb_bulk_endpoint descriptor;
  unsigned enqueue, dci;
  bool cycle, halted;
};
struct xhci_bulk {
  struct usb_host_device *device;
  struct xhci_bulk_endpoint in, out, *active;
  uintptr_t data_address;
  phys_addr_t data_physical, trb;
  size_t requested, actual;
  enum control_state state;
  enum usb_result result;
  bool configured;
};
enum interrupt_receive_state { INTERRUPT_FREE, INTERRUPT_POSTED, INTERRUPT_HELD };
struct xhci_interrupt_receive {
  uintptr_t data_address;
  phys_addr_t data_physical, trb;
  enum interrupt_receive_state state;
};
struct xhci_interrupt_record {
  struct usb_interrupt_completion completion;
  uint8_t bytes[USB_HID_INTERRUPT_BYTES];
};
struct usb_host_interrupt {
  struct usb_host_device *device;
  struct dma_buffer ring;
  struct xhci_interrupt_receive receive[USB_INTERRUPT_RECEIVES];
  struct xhci_interrupt_record queue[USB_INTERRUPT_COMPLETIONS];
  size_t receive_bytes;
  uint64_t sequence;
  unsigned enqueue, dci, queue_head, queue_count;
  enum usb_result result;
  enum usb_interrupt_kind kind;
  bool cycle, configured, started, successful_short_traced, loss_acknowledged;
  struct xhci_trb diagnostic_event;
};
enum async_bulk_receive_state { ASYNC_BULK_FREE, ASYNC_BULK_POSTED, ASYNC_BULK_HELD };
struct xhci_async_bulk_receive {
  uintptr_t data_address;
  phys_addr_t data_physical, trb;
  enum async_bulk_receive_state state;
};
struct xhci_async_bulk_record {
  struct usb_interrupt_completion completion;
  uint8_t *bytes;
};
enum async_bulk_boot_state { ASYNC_BULK_BOOT_NONE, ASYNC_BULK_BOOT_HALTED, ASYNC_BULK_BOOT_RETIRED };
struct xhci_async_bulk {
  struct usb_host_device *device;
  struct xhci_bulk_endpoint in, out;
  struct xhci_async_bulk_receive receive[USB_ASYNC_BULK_RECEIVES];
  struct xhci_async_bulk_record queue[USB_ASYNC_BULK_COMPLETIONS];
  size_t receive_bytes, capacity;
  uint64_t sequence;
  unsigned queue_head, queue_count;
  bool network;
  enum usb_result result;
  enum async_bulk_boot_state boot_state;
  bool configured, started, successful_short_traced;
  struct {
    uintptr_t data_address;
    phys_addr_t data_physical, trb;
    size_t requested, actual;
    uint64_t generation, deadline;
    enum control_state state;
    enum usb_result result;
    bool client;
  } tx;
};
struct usb_host_device {
  struct usb_host_controller *controller;
  struct usb_host_device *parent;
  struct xhci_bulk *bulk;
  struct usb_host_interrupt *interrupt[USB_HID_ENDPOINTS_PER_DEVICE];
  struct xhci_async_bulk *async_bulk;
  struct dma_buffer input, output, control_ring, data;
  uintptr_t data_address;
  phys_addr_t data_physical;
  unsigned port, slot, enqueue, downstream_port, depth, hub_ports;
  uint32_t route;
  uint8_t raw_speed, parent_slot, parent_port;
  uint16_t packet;
  struct usb_link link;
  bool cycle, addressed, multi_tt, removed, hid, retiring, topology;
  uint8_t identity_descriptor[USB_DEVICE_DESCRIPTOR_BYTES], identity_configuration;
  bool identity_descriptor_valid, identity_configuration_valid, identity_hold;
  phys_addr_t stopped_trb;
  unsigned stopping_dci;
  struct {
    enum control_state state;
    uint64_t generation, deadline;
    phys_addr_t setup, data, status;
    size_t requested, actual;
    enum usb_result result;
    bool client, inbound, short_packet;
    struct usb_setup setup_packet;
  } request;
};
struct xhci_speed {
  struct usb_link link;
  bool symmetric;
};
struct xhci_port {
  enum port_state state;
  uint8_t major, minor, slot_type, speed, slot;
  struct xhci_speed speeds[XHCI_PORT_SPEED_MASK + 1];
  bool protocol, dirty, boot_present, identity_hold;
  uint64_t generation;
  struct usb_host_device device;
  struct usb_host_device *active;
};

static void controller_worker(void *argument);
static void stop_controller(struct usb_host_controller *controller);
static enum usb_result recover_control_stall(struct usb_host_device *device, uint64_t deadline);
static bool device_present(const struct usb_host_device *device);
static bool device_identity_held(const struct usb_host_device *device);
static struct usb_host_interrupt *device_interrupt(const struct usb_host_device *device, unsigned dci);

/* Controller work stays BSP-owned. The notification lock protects only the
 * latch/wait handoff, including IRQ and remote producers, at IF=0. It may nest
 * the scheduler queue lock through wake; never hold it across sleep or logging.
 * The retained list is complete before AP startup and never changes at runtime. */
struct usb_host_controller {
  struct usb_host_controller *next;
  struct usb_discovery *discovery;
  struct pci_address address;
  size_t inventory_index;
  struct pci_claim claim;
  /* MSI-X when the function has it, otherwise one MSI message. */
  bool msi_only;
  struct pci_msix msix;
  struct pci_msi msi;
  struct pci_mapping bootstrap, registers;
  /* Bootstrap window over the extended capability list, from the page of its
   * first header; released after the firmware handoff. extended_end is the
   * furthest byte read, checked against the sized BAR. */
  struct pci_mapping extended;
  size_t extended_first;
  uint64_t extended_end;
  struct dma_buffer dcbaa, command_ring, event_ring, erst, scratchpad_array, scratchpads;
  struct dma_buffer descendant_dma, topology_dma, bulk_dma, interrupt_dma, async_bulk_dma, network_bulk_dma;
  struct xhci_bulk bulk[USB_STORAGE_DEVICE_BUDGET];
  struct usb_host_interrupt *interrupt_streams;
  struct xhci_async_bulk *async_bulk_streams;
  uint8_t *async_bulk_queue;
  struct xhci_port *ports;
  struct usb_host_device *descendants;
  struct usb_host_device *topology_devices;
  uint32_t operational, runtime, doorbells;
  unsigned port_count, slot_count, scratchpad_count, context_bytes;
  unsigned descendant_capacity, descendants_used, hid_used, interrupt_count;
  unsigned topology_capacity, topology_used, topology_reserved, async_bulk_count;
  bool topology_exhaustion_reported, hid_exhaustion_reported;
  unsigned command_enqueue, event_dequeue;
  bool command_cycle, event_cycle, port_power;
  bool prepared, running, interrupt_ready, notified, failed, enumerating, hotplug_ready;
  struct usb_host_device *identity_probe;
  struct spinlock notification_lock;
  struct task_wait *wait;
  struct {
    phys_addr_t physical;
    unsigned type, slot, completion;
    bool pending, timed_out, admission;
  } command;
  uint64_t interrupts, commands_completed, events_consumed;
  const char *failure;
  struct xhci_trb diagnostic_event;
  uint64_t diagnostic_next_ns, diagnostic_worker_ns;
  const char *diagnostic_stage, *diagnostic_first_failure;
  bool diagnostic_stopped;
};

static struct usb_host_controller *controllers;

static unsigned device_capacity(const struct usb_host_controller *controller)
{
  return controller->port_count + controller->descendant_capacity + controller->topology_used;
}

static struct usb_host_device *device_at(struct usb_host_controller *controller, unsigned index)
{
  if (index < controller->port_count) {
    return &controller->ports[index].device;
  }
  index -= controller->port_count;
  if (index < controller->descendant_capacity) {
    return &controller->descendants[index];
  }
  index -= controller->descendant_capacity;
  return &controller->topology_devices[index];
}

static uint32_t read32(uintptr_t base, unsigned offset)
{
  return *(volatile uint32_t *)(base + offset);
}

static void write32(uintptr_t base, unsigned offset, uint32_t value)
{
  *(volatile uint32_t *)(base + offset) = value;
}

static void write64(uintptr_t base, unsigned offset, uint64_t value)
{
  *(volatile uint64_t *)(base + offset) = value;
}

static uint64_t read64(uintptr_t base, unsigned offset)
{
  uint32_t low = read32(base, offset);
  return ((uint64_t)read32(base, offset + sizeof(uint32_t)) << 32) | low;
}

static uintptr_t operational(const struct usb_host_controller *controller)
{
  return controller->registers.address + controller->operational;
}

static uintptr_t interrupter(const struct usb_host_controller *controller)
{
  return controller->registers.address + controller->runtime + XHCI_INTERRUPTER_ZERO;
}

static struct xhci_diagnostic_registers diagnostic_registers(struct usb_host_controller *controller)
{
  uintptr_t op = operational(controller), intr = interrupter(controller);
  return (struct xhci_diagnostic_registers){
    .status = read32(op, XHCI_OP_STATUS), .command = read32(op, XHCI_OP_COMMAND),
    .management = read32(intr, XHCI_INTR_MANAGEMENT), .dequeue = read64(intr, XHCI_INTR_DEQUEUE),
  };
}

static void diagnose_controller(struct usb_host_controller *controller, bool endpoints,
    const struct xhci_diagnostic_registers *saved)
{
  uint64_t now = arch_monotonic_ns();
  uint64_t age = controller->diagnostic_worker_ns ? now - controller->diagnostic_worker_ns : 0;
  struct xhci_diagnostic_registers registers = saved ? *saved : diagnostic_registers(controller);
  volatile const struct xhci_trb *ring = (const volatile struct xhci_trb *)controller->event_ring.address;
  dma_read_barrier();
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&controller->notification_lock);
  bool waiting = controller->wait != NULL, notified = controller->notified;
  spin_unlock(&controller->notification_lock);
  cpu_restore_interrupts(flags);
  kinfo("usb xhci diag %x:%x.%u: sample=%s status=%x command=%x iman=%x erdp=%llx "
      "dequeue=%u cycle=%u irq=%llu events=%llu running=%u failed=%u stage=%s "
      "worker-age-ms=%llu pending-command=%u type=%u wait=%u notified=%u head=%x/%x\n",
      controller->address.bus, controller->address.device, controller->address.function,
      saved ? "pre-quarantine" : "live", registers.status, registers.command,
      registers.management, (unsigned long long)registers.dequeue,
      controller->event_dequeue, controller->event_cycle,
      (unsigned long long)controller->interrupts, (unsigned long long)controller->events_consumed,
      controller->running, controller->failed,
      controller->diagnostic_stage ? controller->diagnostic_stage : "startup",
      (unsigned long long)(age / UINT64_C(1000000)), controller->command.pending, controller->command.type,
      waiting, notified,
      ring[controller->event_dequeue].status, ring[controller->event_dequeue].control);
  if (!endpoints && !controller->failed) {
    return;
  }
  kinfo("usb xhci diag: failure=%s first-failure=%s command-slot=%u command-trb=%llx\n",
      controller->failure ? controller->failure : "none",
      controller->diagnostic_first_failure ? controller->diagnostic_first_failure : "none",
      controller->command.slot, (unsigned long long)controller->command.physical);
  const struct xhci_trb *last = &controller->diagnostic_event;
  kinfo("usb xhci diag: last-event type=%u cc=%u pointer=%llx status=%x control=%x "
      "head-pointer=%llx head-status=%x head-control=%x\n",
      (last->control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK,
      last->status >> XHCI_EVENT_COMPLETION_SHIFT, (unsigned long long)last->parameter,
      last->status, last->control, (unsigned long long)ring[controller->event_dequeue].parameter,
      ring[controller->event_dequeue].status, ring[controller->event_dequeue].control);
  if (!endpoints) {
    return;
  }
  for (unsigned i = 0; i < controller->interrupt_count; ++i) {
    const struct usb_host_interrupt *stream = &controller->interrupt_streams[i];
    if (!stream->device || !stream->started) {
      continue;
    }
    const struct usb_host_device *device = stream->device;
    volatile const uint32_t *context = (const volatile uint32_t *)(device->output.address +
        stream->dci * controller->context_bytes);
    kinfo("usb xhci diag: slot=%u dci=%u result=%u bytes=%zu queued=%u sequence=%llu "
        "endpoint=%x configuration=%x tx-info=%x hardware-dequeue=%llx enqueue=%u cycle=%u removed=%u retiring=%u "
        "stopping-dci=%u stopped-trb=%llx\n",
        device->slot, stream->dci, stream->result, stream->receive_bytes, stream->queue_count,
        (unsigned long long)stream->sequence, context[0], context[1], context[4],
        (unsigned long long)(((uint64_t)context[3] << 32) | context[2]),
        stream->enqueue, stream->cycle, device->removed, device->retiring,
        device->stopping_dci, (unsigned long long)device->stopped_trb);
    for (unsigned j = 0; j < USB_INTERRUPT_RECEIVES; ++j) {
      kinfo("usb xhci diag: slot=%u dci=%u receive=%u state=%u trb=%llx\n",
          device->slot, stream->dci, j, stream->receive[j].state,
          (unsigned long long)stream->receive[j].trb);
    }
  }
}

static bool diagnose_failed(struct usb_host_controller *controller)
{
  if (boot_options_get()->usb_xhci_diagnose && !controller->diagnostic_first_failure) {
    controller->diagnostic_first_failure = controller->failure;
  }
  return false;
}

void usb_host_diagnose_tick(void)
{
  if (!boot_options_get()->usb_xhci_diagnose) {
    return;
  }
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  uint64_t now = arch_monotonic_ns();
  for (struct usb_host_controller *controller = controllers; controller; controller = controller->next) {
    if (!controller->prepared || now < controller->diagnostic_next_ns) {
      continue;
    }
    controller->diagnostic_next_ns = now + XHCI_DIAGNOSE_INTERVAL_NS;
    /* The presenter observes a parked worker too. These racy hardware reads
     * diagnose progress; they never establish ownership or change USB state. */
    diagnose_controller(controller, false, NULL);
  }
}

static void set_interrupt_enable(struct usb_host_controller *controller, bool enabled)
{
  uintptr_t intr = interrupter(controller);
  uint32_t management = read32(intr, XHCI_INTR_MANAGEMENT);
  /* IP is W1C; changing IE must preserve pending notification. */
  write32(intr, XHCI_INTR_MANAGEMENT,
          (management & ~(XHCI_INTR_ENABLE | XHCI_INTR_PENDING)) |
          (enabled ? XHCI_INTR_ENABLE : 0));
}

static uintptr_t port_register(const struct usb_host_controller *controller, unsigned index)
{
  return operational(controller) + XHCI_PORTS_OFFSET + index * XHCI_PORT_BYTES;
}

static bool bootstrap_fits(unsigned offset, size_t bytes)
{
  return offset <= PCI_BOOTSTRAP_BAR_BYTES && bytes <= PCI_BOOTSTRAP_BAR_BYTES - offset;
}

static bool wait_boot_bits(uintptr_t base, unsigned offset, uint32_t mask, uint32_t value)
{
  uint64_t deadline = task_deadline_after_ms(USB_STATE_TIMEOUT_MS);
  do {
    if ((read32(base, offset) & mask) == value) {
      return true;
    }
    __asm__ volatile("pause");
  } while (!task_deadline_expired(deadline));
  return false;
}

static bool inspect_capabilities(struct usb_host_controller *controller)
{
  uintptr_t base = controller->bootstrap.address;
  uint32_t header = read32(base, XHCI_CAP_LENGTH);
  controller->operational = header & XHCI_EXT_ID_MASK;
  unsigned version = header >> XHCI_CAP_VERSION_SHIFT;
  uint32_t counts = read32(base, XHCI_CAP_SLOTS_PORTS);
  uint32_t scratchpads = read32(base, XHCI_CAP_SCRATCHPADS);
  uint32_t features = read32(base, XHCI_CAP_FEATURES);
  controller->slot_count = counts & XHCI_MAX_SLOTS_MASK;
  controller->port_count = counts >> XHCI_MAX_PORTS_SHIFT;
  controller->context_bytes = features & XHCI_FEATURE_CONTEXT_64 ? 64 : 32;
  controller->port_power = (features & XHCI_FEATURE_PORT_POWER) != 0;
  controller->scratchpad_count =
    (((scratchpads >> XHCI_SCRATCHPAD_HIGH_SHIFT) & XHCI_SCRATCHPAD_FIELD_MASK) <<
     XHCI_SCRATCHPAD_HIGH_WEIGHT) |
    ((scratchpads >> XHCI_SCRATCHPAD_LOW_SHIFT) & XHCI_SCRATCHPAD_FIELD_MASK);
  controller->runtime = read32(base, XHCI_CAP_RUNTIME) & XHCI_RUNTIME_OFFSET_MASK;
  controller->doorbells = read32(base, XHCI_CAP_DOORBELLS) & XHCI_DOORBELL_OFFSET_MASK;
  if ((version >> XHCI_VERSION_MAJOR_SHIFT) != 1 || !(features & XHCI_FEATURE_64_BIT) ||
      !controller->slot_count || !controller->port_count ||
      !((counts >> XHCI_MAX_INTERRUPTERS_SHIFT) & XHCI_MAX_INTERRUPTERS_MASK) ||
      controller->operational < XHCI_CAP_BYTES || (controller->operational & 7) ||
      !bootstrap_fits(controller->operational, XHCI_OP_BYTES)) {
    return false;
  }
  controller->ports = kmalloc(controller->port_count * sizeof(*controller->ports));
  if (!controller->ports) {
    return false;
  }
  for (unsigned i = 0; i < controller->port_count; ++i) {
    controller->ports[i] = (struct xhci_port){0};
  }
  /* Preserve all possible root reservations before budgeting descendants. */
  unsigned available = controller->slot_count > controller->port_count ?
    controller->slot_count - controller->port_count : 0;
  controller->descendant_capacity = available < USB_DESCENDANT_BUDGET ? available : USB_DESCENDANT_BUDGET;
  controller->topology_capacity = USB_TOPOLOGY_DEVICE_BUDGET + USB_NETWORK_INITIAL_RETRIES *
    (controller->port_count + controller->descendant_capacity) + USB_NETWORK_STREAMS *
    (XHCI_SLOT_ROUTE_DEPTH + 1);
  controller->topology_reserved = USB_TOPOLOGY_NCM_RESERVE;
  controller->topology_devices = kmalloc(controller->topology_capacity * sizeof(*controller->topology_devices));
  if (!controller->topology_devices) {
    return false;
  }
  memset(controller->topology_devices, 0, controller->topology_capacity * sizeof(*controller->topology_devices));
  if (controller->descendant_capacity) {
    controller->descendants = kmalloc(controller->descendant_capacity * sizeof(*controller->descendants));
    if (!controller->descendants) {
      controller->descendant_capacity = 0;
      return false;
    }
    memset(controller->descendants, 0, controller->descendant_capacity * sizeof(*controller->descendants));
  }
  kdebug("xHCI %x:%x.%u: version=%x slots=%u ports=%u context=%u scratchpads=%u\n",
         controller->address.bus, controller->address.device, controller->address.function,
         version, controller->slot_count, controller->port_count,
         controller->context_bytes, controller->scratchpad_count);
  return true;
}

static uintptr_t extended_base(const struct usb_host_controller *controller)
{
  return controller->extended.address - controller->extended_first;
}

/* Maps [offset, offset + bytes) of the extended capability list, growing the
 * window forward from its first page as far as the list reaches. */
static bool extended_window(struct usb_host_controller *controller, unsigned offset,
                            size_t bytes, const struct boot_info *boot)
{
  uint64_t end = (uint64_t)offset + bytes;
  if (end > PCI_BOOTSTRAP_WINDOW_LIMIT) {
    return false;
  }
  size_t first = controller->extended.bytes ? controller->extended_first :
                 offset & ~(PAGE_SIZE - 1);
  if (offset < first) {
    return false;
  }
  if (!controller->extended.bytes || end > first + controller->extended.bytes) {
    size_t mapped_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (controller->extended.bytes) {
      pci_unmap(&controller->claim, &controller->extended);
    }
    if (pci_map_bootstrap_bar(&controller->claim, 0, first, mapped_end - first, boot,
                              &controller->extended) != MM_OK) {
      return false;
    }
    controller->extended_first = first;
  }
  if (end > controller->extended_end) {
    controller->extended_end = end;
  }
  return true;
}

static bool legacy_handoff(struct usb_host_controller *controller, unsigned offset)
{
  uintptr_t base = extended_base(controller) + offset;
  volatile uint8_t *os_owned = (volatile uint8_t *)(base + XHCI_LEGACY_OS_BYTE);
  volatile uint8_t *bios_owned = (volatile uint8_t *)(base + XHCI_LEGACY_BIOS_BYTE);
  /* Byte access avoids writing the independently changing BIOS semaphore. */
  *os_owned |= XHCI_LEGACY_OWNED;
  uint64_t deadline = task_deadline_after_ms(USB_STATE_TIMEOUT_MS);
  while (*bios_owned & XHCI_LEGACY_OWNED) {
    if (task_deadline_expired(deadline)) {
      /* As Linux does: firmware that never answers loses ownership. */
      kwarn("xHCI %x:%x.%u: firmware kept ownership for %u ms; taking it\n",
            controller->address.bus, controller->address.device, controller->address.function,
            (unsigned)USB_STATE_TIMEOUT_MS);
      *bios_owned &= (uint8_t)~XHCI_LEGACY_OWNED;
      break;
    }
    __asm__ volatile("pause");
  }
  if (!(*os_owned & XHCI_LEGACY_OWNED)) {
    return false;
  }
  uint32_t control = read32(base, XHCI_LEGACY_CONTROL);
  /* Preserve reserved fields, disable known sources and clear observed W1C. */
  write32(base, XHCI_LEGACY_CONTROL,
          control & ~(XHCI_LEGACY_SMI_ENABLES | XHCI_LEGACY_RESERVED_ZERO));
  return !(read32(base, XHCI_LEGACY_CONTROL) & XHCI_LEGACY_SMI_ENABLES);
}

static enum usb_speed protocol_speed(unsigned major, uint32_t psi)
{
  uint64_t rate = psi >> XHCI_PSI_MANTISSA_SHIFT;
  unsigned exponent = (psi >> XHCI_PSI_EXPONENT_SHIFT) & XHCI_PSI_EXPONENT_MASK;
  unsigned protocol = (psi >> XHCI_PSI_PROTOCOL_SHIFT) & XHCI_PSI_PROTOCOL_MASK;
  if (!rate) {
    return USB_SPEED_UNKNOWN;
  }
  if (major == XHCI_PROTOCOL_USB_3 && (psi & XHCI_PSI_FULL_DUPLEX)) {
    if (protocol == XHCI_PSI_PROTOCOL_SUPER) {
      return USB_SPEED_SUPER;
    }
    if (protocol == XHCI_PSI_PROTOCOL_SUPER_PLUS) {
      return USB_SPEED_SUPER_PLUS;
    }
  }
  if (major != XHCI_PROTOCOL_USB_2 || protocol || (psi & XHCI_PSI_FULL_DUPLEX)) {
    return USB_SPEED_UNKNOWN;
  }
  while (exponent--) {
    rate *= 1000;
  }
  return rate == 1500000 ? USB_SPEED_LOW : rate == 12000000 ? USB_SPEED_FULL :
    rate == 480000000 ? USB_SPEED_HIGH : USB_SPEED_UNKNOWN;
}

static uint64_t protocol_rate(uint32_t psi)
{
  uint64_t rate = psi >> XHCI_PSI_MANTISSA_SHIFT;
  unsigned exponent = (psi >> XHCI_PSI_EXPONENT_SHIFT) & XHCI_PSI_EXPONENT_MASK;
  while (exponent--) {
    rate *= 1000;
  }
  return rate;
}

static struct xhci_speed default_speed(enum usb_speed speed, uint64_t lane_bps, unsigned lanes)
{
  return (struct xhci_speed){
    .link = { .speed = speed, .rx_bps = lane_bps * lanes, .tx_bps = lane_bps * lanes,
              .rx_lanes = lanes, .tx_lanes = lanes },
    .symmetric = true,
  };
}

static bool protocol_speeds(uintptr_t base, unsigned offset, unsigned count,
                            unsigned major, unsigned minor, struct xhci_speed *speeds)
{
  if (!count) {
    /* Defaults exist only for these exact BCD protocol revisions. Explicit PSI
     * entries replace them, including the meanings of otherwise familiar IDs. */
    if (major == XHCI_PROTOCOL_USB_2 && minor == XHCI_PROTOCOL_MINOR_0) {
      speeds[XHCI_SPEED_FULL].link.speed = USB_SPEED_FULL;
      speeds[XHCI_SPEED_LOW].link.speed = USB_SPEED_LOW;
      speeds[XHCI_SPEED_HIGH].link.speed = USB_SPEED_HIGH;
    } else if (major == XHCI_PROTOCOL_USB_3 &&
               (minor == XHCI_PROTOCOL_MINOR_0 || minor == XHCI_PROTOCOL_MINOR_1 ||
                minor == XHCI_PROTOCOL_MINOR_2)) {
      speeds[XHCI_SPEED_GEN_1X1] = default_speed(USB_SPEED_SUPER, USB_SUPER_LANE_BPS, 1);
      if (minor != XHCI_PROTOCOL_MINOR_0) {
        speeds[XHCI_SPEED_GEN_2X1] = default_speed(USB_SPEED_SUPER_PLUS, USB_GEN2_LANE_BPS, 1);
      }
      if (minor == XHCI_PROTOCOL_MINOR_2) {
        speeds[XHCI_SPEED_GEN_1X2] = default_speed(USB_SPEED_SUPER_PLUS, USB_SUPER_LANE_BPS, 2);
        speeds[XHCI_SPEED_GEN_2X2] = default_speed(USB_SPEED_SUPER_PLUS, USB_GEN2_LANE_BPS, 2);
      }
    }
    return true;
  }
  unsigned seen = 0;
  for (unsigned i = 0; i < count; ++i) {
    uint32_t psi = read32(base, offset + XHCI_PROTOCOL_BYTES + i * sizeof(uint32_t));
    unsigned id = psi & XHCI_PSI_ID_MASK;
    unsigned link = (psi >> XHCI_PSI_LINK_TYPE_SHIFT) & XHCI_PSI_LINK_TYPE_MASK;
    if (!id || (seen & (1u << id))) {
      return false;
    }
    seen |= 1u << id;
    enum usb_speed speed = protocol_speed(major, psi);
    if (link == XHCI_PSI_ASYMMETRIC_RX) {
      if (++i == count) {
        return false;
      }
      uint32_t transmit = read32(base, offset + XHCI_PROTOCOL_BYTES + i * sizeof(uint32_t));
      if ((transmit & XHCI_PSI_ID_MASK) != id ||
          ((transmit >> XHCI_PSI_LINK_TYPE_SHIFT) & XHCI_PSI_LINK_TYPE_MASK) != XHCI_PSI_ASYMMETRIC_TX) {
        return false;
      }
      /* Directional rates/lanes may differ. EP0 needs a common USB 3 protocol,
       * not an invented aggregate rate or a normalized controller speed ID. */
      if (usb_speed_is_enhanced(speed) && protocol_speed(major, transmit) == speed) {
        speeds[id].link = (struct usb_link){ .speed = speed,
          .rx_bps = protocol_rate(psi), .tx_bps = protocol_rate(transmit) };
      }
      continue;
    }
    if (link != XHCI_PSI_SYMMETRIC) {
      return false;
    }
    speeds[id].link = (struct usb_link){ .speed = speed,
      .rx_bps = protocol_rate(psi), .tx_bps = protocol_rate(psi) };
    speeds[id].symmetric = true;
  }
  return true;
}

static void skip_protocol(const struct usb_host_controller *controller, unsigned offset,
                          const char *reason)
{
  kwarn("xHCI %x:%x.%u: skipped Supported Protocol capability at 0x%x: %s\n",
        controller->address.bus, controller->address.device, controller->address.function,
        offset, reason);
}

/* A malformed Supported Protocol capability is skipped and its ports stay
 * unusable; the rest of the controller still works. */
static void supported_protocol(struct usb_host_controller *controller, unsigned offset,
                               uint32_t header, unsigned speeds)
{
  uintptr_t base = extended_base(controller);
  uint32_t ports = read32(base, offset + 8);
  unsigned first = ports & XHCI_EXT_ID_MASK;
  unsigned count = (ports >> XHCI_PROTOCOL_PORT_COUNT_SHIFT) & XHCI_EXT_ID_MASK;
  if (read32(base, offset + 4) != XHCI_PROTOCOL_NAME || !first || !count ||
      first > controller->port_count || count > controller->port_count - first + 1) {
    skip_protocol(controller, offset, "invalid name or port range");
    return;
  }
  unsigned slot_type = read32(base, offset + 12) & XHCI_PROTOCOL_SLOT_TYPE_MASK;
  struct xhci_speed speed_map[XHCI_PORT_SPEED_MASK + 1] = {0};
  if (!protocol_speeds(base, offset, speeds, header >> XHCI_PROTOCOL_MAJOR_SHIFT,
                       (header >> XHCI_PROTOCOL_MINOR_SHIFT) & XHCI_EXT_ID_MASK, speed_map)) {
    skip_protocol(controller, offset, "invalid speed entries");
    return;
  }
  for (unsigned i = first - 1; i < first - 1 + count; ++i) {
    if (controller->ports[i].protocol) {
      skip_protocol(controller, offset, "ports already described");
      return;
    }
  }
  for (unsigned i = first - 1; i < first - 1 + count; ++i) {
    struct xhci_port *port = &controller->ports[i];
    port->protocol = true;
    port->major = header >> XHCI_PROTOCOL_MAJOR_SHIFT;
    port->minor = header >> XHCI_PROTOCOL_MINOR_SHIFT;
    port->slot_type = slot_type;
    memcpy(port->speeds, speed_map, sizeof(speed_map));
  }
}

/* Unknown capability IDs (vendor, debug, power management) are skipped. A
 * list that leaves the window, overlaps the register regions or repeats the
 * legacy capability is refused. */
static bool extended_capabilities(struct usb_host_controller *controller,
                                  const struct boot_info *boot)
{
  unsigned offset = (read32(controller->bootstrap.address, XHCI_CAP_FEATURES) >>
                     XHCI_EXT_CAP_SHIFT) * XHCI_EXT_CAP_DWORD_BYTES;
  unsigned legacy = 0;
  /* Next pointers only move forward; a body must fit before the next header. */
  while (offset) {
    if (offset < XHCI_CAP_BYTES || !extended_window(controller, offset, sizeof(uint32_t), boot)) {
      return false;
    }
    uint32_t header = read32(extended_base(controller), offset);
    unsigned id = header & XHCI_EXT_ID_MASK;
    unsigned next = ((header >> XHCI_EXT_NEXT_SHIFT) & XHCI_EXT_NEXT_MASK) *
      XHCI_EXT_CAP_DWORD_BYTES;
    kdebug("xHCI %x:%x.%u: extended capability %u at 0x%x\n",
           controller->address.bus, controller->address.device, controller->address.function,
           id, offset);
    unsigned bytes = sizeof(uint32_t);
    if (id == XHCI_EXT_LEGACY) {
      bytes = XHCI_LEGACY_BYTES;
      if (legacy) {
        return false;
      }
      legacy = offset;
    } else if (id == XHCI_EXT_PROTOCOL) {
      if (!extended_window(controller, offset, XHCI_PROTOCOL_BYTES, boot)) {
        return false;
      }
      unsigned speeds = read32(extended_base(controller), offset + 8) >>
        XHCI_PROTOCOL_SPEED_COUNT_SHIFT;
      bytes = XHCI_PROTOCOL_BYTES + speeds * sizeof(uint32_t);
      if (!extended_window(controller, offset, bytes, boot)) {
        return false;
      }
      supported_protocol(controller, offset, header, speeds);
    }
    if (!extended_window(controller, offset, bytes, boot) || (next && next < bytes)) {
      return false;
    }
    uint64_t end = (uint64_t)offset + bytes;
    uint64_t ports = (uint64_t)controller->operational + XHCI_PORTS_OFFSET;
    uint64_t runtime = (uint64_t)controller->runtime + XHCI_INTERRUPTER_ZERO;
    if ((offset < controller->operational + XHCI_OP_BYTES && controller->operational < end) ||
        (offset < ports + controller->port_count * XHCI_PORT_BYTES && ports < end) ||
        (offset < runtime + XHCI_INTERRUPTER_BYTES && runtime < end) ||
        (offset < (uint64_t)controller->doorbells + (controller->slot_count + 1) * XHCI_DOORBELL_BYTES &&
         controller->doorbells < end)) {
      return false;
    }
    offset = next ? offset + next : 0;
  }
  bool owned = !legacy || legacy_handoff(controller, legacy);
  if (controller->extended.bytes) {
    pci_unmap(&controller->claim, &controller->extended);
  }
  return owned;
}

static bool halt_and_reset(struct usb_host_controller *controller)
{
  uintptr_t op = controller->bootstrap.address + controller->operational;
  if (!wait_boot_bits(op, XHCI_OP_STATUS, XHCI_STATUS_NOT_READY, 0)) {
    return false;
  }
  uint32_t command = read32(op, XHCI_OP_COMMAND);
  write32(op, XHCI_OP_COMMAND, command & ~XHCI_COMMAND_RUN);
  if (!wait_boot_bits(op, XHCI_OP_STATUS, XHCI_STATUS_HALTED, XHCI_STATUS_HALTED) ||
      !pci_complete_claim(&controller->claim)) {
    return false;
  }
  command = read32(op, XHCI_OP_COMMAND);
  write32(op, XHCI_OP_COMMAND, command | XHCI_COMMAND_RESET);
  return wait_boot_bits(op, XHCI_OP_COMMAND, XHCI_COMMAND_RESET, 0) &&
    wait_boot_bits(op, XHCI_OP_STATUS, XHCI_STATUS_NOT_READY | XHCI_STATUS_HALTED,
                   XHCI_STATUS_HALTED);
}

static bool register_region_fits(struct usb_host_controller *controller, uint64_t first, uint64_t bytes)
{
  return first <= controller->claim.bars[0].bytes && bytes <= controller->claim.bars[0].bytes - first;
}

static bool regions_overlap(uint64_t first, uint64_t bytes, const struct pci_region *region)
{
  return region->bar == 0 && first < (uint64_t)region->offset + region->length &&
    region->offset < first + bytes;
}

static bool interrupt_delivery(struct usb_host_controller *controller, bool enabled)
{
  if (controller->msi_only) {
    return enabled ? pci_msi_enable(&controller->msi) : pci_msi_disable(&controller->msi);
  }
  return enabled ? pci_msix_enable(&controller->msix) : pci_msix_disable(&controller->msix);
}

static bool has_msix(const struct pci_claim *claim)
{
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    if (pci_read8(claim->device->address, claim->capabilities[i]) == PCI_CAP_MSIX) {
      return true;
    }
  }
  return false;
}

static bool map_registers(struct usb_host_controller *controller, const struct boot_info *boot)
{
  struct pci_claim *claim = &controller->claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  pci_write16(claim, PCI_COMMAND, command & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO));
  if (pci_read16(claim->device->address, PCI_COMMAND) &
      (PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_MASTER)) {
    return false;
  }
  uint32_t ports_end = controller->operational + XHCI_PORTS_OFFSET +
    controller->port_count * XHCI_PORT_BYTES;
  uint64_t runtime_end = (uint64_t)controller->runtime + XHCI_INTERRUPTER_ZERO + XHCI_INTERRUPTER_BYTES;
  uint64_t doorbells_end = (uint64_t)controller->doorbells +
    (controller->slot_count + 1) * XHCI_DOORBELL_BYTES;
  /* Every bootstrap read, including the extended capability list, must have
   * been inside BAR0. */
  uint64_t bootstrap_end = controller->extended_end > PCI_BOOTSTRAP_BAR_BYTES ?
                           controller->extended_end : PCI_BOOTSTRAP_BAR_BYTES;
  controller->msi_only = !has_msix(claim);
  if (!pci_size_bars(claim) || claim->bars[0].bytes < bootstrap_end ||
      !register_region_fits(controller, 0, ports_end) ||
      !register_region_fits(controller, controller->runtime, runtime_end - controller->runtime) ||
      !register_region_fits(controller, controller->doorbells, doorbells_end - controller->doorbells) ||
      controller->runtime < ports_end || controller->doorbells < ports_end ||
      ((uint64_t)controller->runtime < doorbells_end && controller->doorbells < runtime_end) ||
      (!controller->msi_only && !pci_msix_discover(claim, &controller->msix))) {
    return false;
  }
  uint64_t end = ports_end;
  if (end < runtime_end) {
    end = runtime_end;
  }
  if (end < doorbells_end) {
    end = doorbells_end;
  }
  /* The single register mapping includes holes. Reject table/PBA overlap with
   * that whole extent before any MSI-X access, not merely individual registers. */
  if (!controller->msi_only &&
      (regions_overlap(0, end, &controller->msix.table) ||
       regions_overlap(0, end, &controller->msix.pba))) {
    return false;
  }
  if (pci_map_bar(claim, 0, 0, end, boot, &controller->registers) != MM_OK ||
      (!controller->msi_only && pci_msix_map(&controller->msix, boot) != MM_OK)) {
    return false;
  }
  pci_write16(claim, PCI_COMMAND, (command | PCI_COMMAND_MEMORY) & ~PCI_COMMAND_IO);
  return (pci_read16(claim->device->address, PCI_COMMAND) &
    (PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE)) ==
    (PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE);
}

static bool ring_layout(const struct dma_buffer *ring)
{
  return !(ring->physical & (XHCI_RING_ALIGNMENT - 1)) &&
    ring->bytes <= XHCI_RING_BOUNDARY &&
    (ring->physical & (XHCI_RING_BOUNDARY - 1)) <= XHCI_RING_BOUNDARY - ring->bytes;
}

static void initialize_transfer_ring(const struct dma_buffer *ring)
{
  volatile struct xhci_trb *trbs = (volatile struct xhci_trb *)ring->address;
  trbs[XHCI_RING_TRBS - 1] = (struct xhci_trb){
    .parameter = ring->physical,
    .control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE,
  };
}

static bool prepare_device_buffers(struct usb_host_device *device, size_t capacity, size_t padding)
{
  if (!ring_layout(&device->control_ring)) {
    return false;
  }
  size_t offset = padding ? (-device->data.physical & (XHCI_RING_BOUNDARY - 1)) : 0;
  if (offset > device->data.bytes || capacity > device->data.bytes - offset ||
      ((device->data.physical + offset) & (XHCI_RING_BOUNDARY - 1)) > XHCI_RING_BOUNDARY - capacity) {
    return false;
  }
  device->data_address = device->data.address + offset;
  device->data_physical = device->data.physical + offset;
  device->cycle = true;
  initialize_transfer_ring(&device->control_ring);
  return true;
}

static struct dma_buffer descendant_dma_slice(const struct dma_buffer *arena, size_t offset, size_t bytes)
{
  KASSERT(offset <= arena->bytes && bytes <= arena->bytes - offset);
  return (struct dma_buffer){
    .address = arena->address + offset,
    .physical = arena->physical + offset,
    .bytes = bytes,
  };
}

static bool allocate_device_pool(struct usb_host_controller *controller,
                                 struct usb_host_device *devices, struct dma_buffer *arena,
                                 unsigned count, size_t capacity, size_t padding)
{
  if (!count) {
    return true;
  }
  size_t input_bytes =
    (XHCI_INPUT_CONTEXT_COUNT * controller->context_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t output_bytes =
    (XHCI_CONTEXT_COUNT * controller->context_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t data_bytes = (capacity + padding + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t stride = input_bytes + output_bytes + PAGE_SIZE + data_bytes;
  if (count > SIZE_MAX / stride || dma_buffer_allocate(arena, count * stride) != MM_OK) {
    return false;
  }
  /* The controller owns the arena; device slices are retained loans. */
  for (unsigned i = 0; i < count; ++i) {
    struct usb_host_device *device = &devices[i];
    size_t offset = i * stride;
    device->controller = controller;
    device->input = descendant_dma_slice(arena, offset, input_bytes);
    offset += input_bytes;
    device->output = descendant_dma_slice(arena, offset, output_bytes);
    offset += output_bytes;
    device->control_ring = descendant_dma_slice(arena, offset, PAGE_SIZE);
    offset += PAGE_SIZE;
    device->data = descendant_dma_slice(arena, offset, data_bytes);
    if (!prepare_device_buffers(device, capacity, padding)) {
      return false;
    }
  }
  return true;
}

static bool allocate_devices(struct usb_host_controller *controller)
{
  size_t capacity = USB_CONTROL_BYTES;
  if (!capacity || capacity > UINT16_MAX) {
    return false;
  }
  /* A single Data Stage TRB cannot cross a 64 KiB boundary. Larger configured
   * buffers retain their full allocation while selecting an aligned interior. */
  size_t padding = capacity > PAGE_SIZE ? XHCI_RING_BOUNDARY - PAGE_SIZE : 0;
  for (unsigned i = 0; i < controller->port_count; ++i) {
    struct usb_host_device *device = &controller->ports[i].device;
    device->controller = controller;
    device->port = i;
    if (dma_buffer_allocate(&device->input, XHCI_INPUT_CONTEXT_COUNT * controller->context_bytes) != MM_OK ||
        dma_buffer_allocate(&device->output, XHCI_CONTEXT_COUNT * controller->context_bytes) != MM_OK ||
        dma_buffer_allocate(&device->control_ring, PAGE_SIZE) != MM_OK ||
        dma_buffer_allocate(&device->data, capacity + padding) != MM_OK ||
        !prepare_device_buffers(device, capacity, padding)) {
      return false;
    }
  }
  return allocate_device_pool(controller, controller->descendants, &controller->descendant_dma,
                              controller->descendant_capacity, capacity, padding) &&
    allocate_device_pool(controller, controller->topology_devices, &controller->topology_dma,
                         controller->topology_capacity, capacity, padding);
}

static bool allocate_bulk(struct usb_host_controller *controller)
{
  _Static_assert(USB_BULK_BYTES && USB_BULK_BYTES <= XHCI_RING_BOUNDARY,
                 "bulk buffer fits one boundary-aligned Normal TRB");
  size_t padding = XHCI_RING_BOUNDARY - PAGE_SIZE;
  size_t data_bytes = (USB_BULK_BYTES + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t stride = 2 * PAGE_SIZE + data_bytes + padding;
  if (dma_buffer_allocate(&controller->bulk_dma, USB_STORAGE_DEVICE_BUDGET * stride) != MM_OK) {
    return false;
  }
  for (unsigned i = 0; i < USB_STORAGE_DEVICE_BUDGET; ++i) {
    struct xhci_bulk *bulk = &controller->bulk[i];
    size_t offset = i * stride;
    bulk->in.ring = descendant_dma_slice(&controller->bulk_dma, offset, PAGE_SIZE);
    bulk->out.ring = descendant_dma_slice(&controller->bulk_dma, offset + PAGE_SIZE, PAGE_SIZE);
    offset += 2 * PAGE_SIZE;
    phys_addr_t physical = controller->bulk_dma.physical + offset;
    offset += -physical & (XHCI_RING_BOUNDARY - 1);
    bulk->data_address = controller->bulk_dma.address + offset;
    bulk->data_physical = controller->bulk_dma.physical + offset;
    if (!ring_layout(&bulk->in.ring) || !ring_layout(&bulk->out.ring)) {
      return false;
    }
    initialize_transfer_ring(&bulk->in.ring);
    initialize_transfer_ring(&bulk->out.ring);
    bulk->in.cycle = bulk->out.cycle = true;
  }
  return true;
}

static bool allocate_async_bulk(struct usb_host_controller *controller)
{
  _Static_assert(USB_ASYNC_BULK_BYTES && USB_ASYNC_BULK_BYTES <= PAGE_SIZE,
                 "HCI backing remains one page");
  _Static_assert(USB_NETWORK_BULK_BYTES &&
                 (1 + USB_ASYNC_BULK_RECEIVES) * USB_NETWORK_BULK_BYTES <= XHCI_RING_BOUNDARY,
                 "network data slices fit one aligned TRB boundary");
  _Static_assert(USB_ASYNC_BULK_RECEIVES && USB_ASYNC_BULK_RECEIVES < XHCI_RING_TRBS,
                 "posted receives fit the usable ring");
  size_t count = USB_ASYNC_BULK_DEVICE_BUDGET + USB_NETWORK_STREAMS;
  size_t stride = 2 * PAGE_SIZE + XHCI_RING_BOUNDARY - PAGE_SIZE +
    (1 + USB_ASYNC_BULK_RECEIVES) * USB_NETWORK_BULK_BYTES;
  size_t hci_stride = (3 + USB_ASYNC_BULK_RECEIVES) * PAGE_SIZE;
  size_t queue_bytes = USB_ASYNC_BULK_COMPLETIONS *
    (USB_ASYNC_BULK_DEVICE_BUDGET * USB_ASYNC_BULK_BYTES + USB_NETWORK_STREAMS * USB_NETWORK_BULK_BYTES);
  if (!count || USB_NETWORK_STREAMS > SIZE_MAX / stride ||
      count > SIZE_MAX / sizeof(*controller->async_bulk_streams)) {
    return false;
  }
  controller->async_bulk_count = count;
  controller->async_bulk_streams = kmalloc(count * sizeof(*controller->async_bulk_streams));
  controller->async_bulk_queue = kmalloc(queue_bytes);
  if (!controller->async_bulk_streams || !controller->async_bulk_queue) {
    return false;
  }
  memset(controller->async_bulk_streams, 0, count * sizeof(*controller->async_bulk_streams));
  if (dma_buffer_allocate(&controller->async_bulk_dma, USB_ASYNC_BULK_DEVICE_BUDGET * hci_stride) != MM_OK ||
      dma_buffer_allocate(&controller->network_bulk_dma, USB_NETWORK_STREAMS * stride) != MM_OK) {
    return false;
  }
  size_t queue_offset = 0;
  for (unsigned i = 0; i < count; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    bulk->network = i >= USB_ASYNC_BULK_DEVICE_BUDGET;
    bulk->capacity = bulk->network ? USB_NETWORK_BULK_BYTES : USB_ASYNC_BULK_BYTES;
    struct dma_buffer *arena = bulk->network ? &controller->network_bulk_dma : &controller->async_bulk_dma;
    size_t offset = bulk->network ? (i - USB_ASYNC_BULK_DEVICE_BUDGET) * stride : i * hci_stride;
    bulk->in.ring = descendant_dma_slice(arena, offset, PAGE_SIZE);
    bulk->out.ring = descendant_dma_slice(arena, offset + PAGE_SIZE, PAGE_SIZE);
    if (!ring_layout(&bulk->in.ring) || !ring_layout(&bulk->out.ring)) {
      return false;
    }
    initialize_transfer_ring(&bulk->in.ring);
    initialize_transfer_ring(&bulk->out.ring);
    bulk->in.cycle = bulk->out.cycle = true;
    offset += 2 * PAGE_SIZE;
    if (bulk->network) {
      offset += -(arena->physical + offset) & (XHCI_RING_BOUNDARY - 1);
    }
    bulk->tx.data_address = arena->address + offset;
    bulk->tx.data_physical = arena->physical + offset;
    for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
      size_t data_offset = offset + (j + 1) * bulk->capacity;
      bulk->receive[j].data_address = arena->address + data_offset;
      bulk->receive[j].data_physical = arena->physical + data_offset;
    }
    for (unsigned j = 0; j < USB_ASYNC_BULK_COMPLETIONS; ++j) {
      bulk->queue[j].bytes = controller->async_bulk_queue + queue_offset;
      queue_offset += bulk->capacity;
    }
  }
  return true;
}

static bool allocate_interrupts(struct usb_host_controller *controller)
{
  _Static_assert(USB_INTERRUPT_BYTES && USB_INTERRUPT_BYTES <= USB_HID_INTERRUPT_BYTES &&
                 USB_HID_INTERRUPT_BYTES <= PAGE_SIZE,
                 "prepared interrupt capacities fit one page and one Normal TRB boundary");
  _Static_assert(USB_INTERRUPT_RECEIVES && USB_INTERRUPT_RECEIVES < XHCI_RING_TRBS,
                 "posted interrupt receives fit the usable transfer ring");
  _Static_assert(USB_INTERRUPT_COMPLETIONS, "interrupt completion queue is nonempty");
  size_t stride = (1 + USB_INTERRUPT_RECEIVES) * PAGE_SIZE;
  size_t ports = controller->port_count + USB_HID_DEVICE_BUDGET * USB_HID_ENDPOINTS_PER_DEVICE +
    USB_HUB_INTERRUPT_DEVICE_BUDGET + controller->topology_capacity + USB_NETWORK_STREAMS;
  controller->interrupt_count = ports;
  if (ports > SIZE_MAX / stride || ports > SIZE_MAX / sizeof(*controller->interrupt_streams)) {
    return false;
  }
  controller->interrupt_streams = kmalloc(ports * sizeof(*controller->interrupt_streams));
  if (!controller->interrupt_streams) {
    return false;
  }
  memset(controller->interrupt_streams, 0, ports * sizeof(*controller->interrupt_streams));
  if (dma_buffer_allocate(&controller->interrupt_dma, ports * stride) != MM_OK) {
    return false;
  }
  for (unsigned i = 0; i < controller->interrupt_count; ++i) {
    struct usb_host_interrupt *interrupt = &controller->interrupt_streams[i];
    size_t offset = i * stride;
    interrupt->ring = descendant_dma_slice(&controller->interrupt_dma, offset, PAGE_SIZE);
    if (!ring_layout(&interrupt->ring)) {
      return false;
    }
    initialize_transfer_ring(&interrupt->ring);
    interrupt->cycle = true;
    for (unsigned j = 0; j < USB_INTERRUPT_RECEIVES; ++j) {
      size_t data_offset = offset + (j + 1) * PAGE_SIZE;
      interrupt->receive[j].data_address = controller->interrupt_dma.address + data_offset;
      interrupt->receive[j].data_physical = controller->interrupt_dma.physical + data_offset;
    }
  }
  return true;
}

static bool allocate_dma(struct usb_host_controller *controller)
{
  if (dma_buffer_allocate(&controller->dcbaa, (controller->slot_count + 1) * sizeof(uint64_t)) != MM_OK ||
      dma_buffer_allocate(&controller->command_ring, PAGE_SIZE) != MM_OK ||
      dma_buffer_allocate(&controller->event_ring, PAGE_SIZE) != MM_OK ||
      dma_buffer_allocate(&controller->erst, XHCI_RING_ALIGNMENT) != MM_OK) {
    return false;
  }
  if (controller->scratchpad_count &&
      (dma_buffer_allocate(&controller->scratchpad_array,
                          controller->scratchpad_count * sizeof(uint64_t)) != MM_OK ||
       dma_buffer_allocate(&controller->scratchpads, controller->scratchpad_count * PAGE_SIZE) != MM_OK)) {
    return false;
  }
  if (!ring_layout(&controller->command_ring) || !ring_layout(&controller->event_ring)) {
    return false;
  }
  volatile uint64_t *dcbaa = (volatile uint64_t *)controller->dcbaa.address;
  if (controller->scratchpad_count) {
    volatile uint64_t *array = (volatile uint64_t *)controller->scratchpad_array.address;
    for (unsigned i = 0; i < controller->scratchpad_count; ++i) {
      array[i] = controller->scratchpads.physical + i * PAGE_SIZE;
    }
    dcbaa[0] = controller->scratchpad_array.physical;
  }
  volatile struct xhci_trb *commands = (volatile struct xhci_trb *)controller->command_ring.address;
  commands[XHCI_RING_TRBS - 1] = (struct xhci_trb){
    .parameter = controller->command_ring.physical,
    .control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE,
  };
  volatile struct xhci_erst_entry *erst = (volatile struct xhci_erst_entry *)controller->erst.address;
  *erst = (struct xhci_erst_entry){.base = controller->event_ring.physical, .size = XHCI_RING_TRBS};
  controller->command_cycle = controller->event_cycle = true;
  return allocate_devices(controller) && allocate_bulk(controller) &&
    allocate_interrupts(controller) && allocate_async_bulk(controller);
}

static void release_boot_resources(struct usb_host_controller *controller)
{
  usb_release_prepared(controller->discovery);
  controller->discovery = NULL;
  if (controller->ports) {
    for (unsigned i = 0; i < controller->port_count; ++i) {
      struct usb_host_device *device = &controller->ports[i].device;
      dma_buffer_release(&device->data);
      dma_buffer_release(&device->control_ring);
      dma_buffer_release(&device->output);
      dma_buffer_release(&device->input);
    }
  }
  dma_buffer_release(&controller->bulk_dma);
  dma_buffer_release(&controller->interrupt_dma);
  kfree(controller->interrupt_streams);
  controller->interrupt_streams = NULL;
  dma_buffer_release(&controller->network_bulk_dma);
  dma_buffer_release(&controller->async_bulk_dma);
  kfree(controller->async_bulk_queue);
  controller->async_bulk_queue = NULL;
  kfree(controller->async_bulk_streams);
  controller->async_bulk_streams = NULL;
  dma_buffer_release(&controller->topology_dma);
  kfree(controller->topology_devices);
  controller->topology_devices = NULL;
  controller->topology_capacity = 0;
  dma_buffer_release(&controller->descendant_dma);
  dma_buffer_release(&controller->scratchpads);
  dma_buffer_release(&controller->scratchpad_array);
  dma_buffer_release(&controller->erst);
  dma_buffer_release(&controller->event_ring);
  dma_buffer_release(&controller->command_ring);
  dma_buffer_release(&controller->dcbaa);
  kfree(controller->descendants);
  controller->descendants = NULL;
  controller->descendant_capacity = 0;
  kfree(controller->ports);
  controller->ports = NULL;
  if (controller->claim.reserved) {
    pci_cancel_reservation(&controller->claim);
  } else {
    pci_release_device(&controller->claim);
  }
}

static void prepare_controller(struct usb_host_controller *controller, size_t pci_index,
                               const struct boot_info *boot)
{
  if (!pci_reserve_device_at(pci_index, &controller->claim)) {
    kerror("xHCI %x:%x.%u: controller busy or unsupported PCI state\n",
           controller->address.bus, controller->address.device, controller->address.function);
    controller->failed = true;
    usb_inventory_controller_failed(controller->inventory_index);
    return;
  }
  const char *failure = "unsupported assigned BAR0 bootstrap mapping";
  if (pci_map_bootstrap_bar(&controller->claim, 0, 0, PCI_BOOTSTRAP_BAR_BYTES, boot,
                            &controller->bootstrap) != MM_OK) {
    goto fail;
  }
  failure = "unsupported capabilities or controller record allocation failed";
  if (!inspect_capabilities(controller)) {
    goto fail;
  }
  failure = "cannot prepare USB observation records";
  controller->discovery = usb_prepare(controller, controller->inventory_index);
  if (!controller->discovery) {
    goto fail;
  }
  failure = "unsupported extended capability list or firmware handoff failed";
  if (!extended_capabilities(controller, boot)) {
    goto fail;
  }
  failure = "cannot confirm controller halt/reset";
  if (!halt_and_reset(controller)) {
    /* Completion/reset failure may leave hardware ownership unresolved. */
    if (!controller->claim.reserved) {
      controller->failure = failure;
      kerror("xHCI %x:%x.%u: %s; claim retained until reboot\n",
             controller->address.bus, controller->address.device, controller->address.function,
             failure);
      controller->failed = true;
      usb_inventory_controller_failed(controller->inventory_index);
      return;
    }
    goto fail;
  }
  failure = "invalid register or interrupt resources";
  if (!map_registers(controller, boot)) {
    goto fail;
  }
  failure = "4 KiB controller pages required";
  if (!(read32(operational(controller), XHCI_OP_PAGE_SIZE) & XHCI_PAGE_4K)) {
    goto fail;
  }
  failure = "DMA allocation or ring placement failed";
  if (!allocate_dma(controller)) {
    goto fail;
  }
  failure = controller->msi_only ? "MSI setup failed" : "MSI-X setup failed";
  bool interrupt_ready = controller->msi_only ?
    pci_msi_prepare(&controller->claim, &controller->msi, APIC_XHCI_VECTOR) :
    pci_msix_prepare(&controller->msix, APIC_XHCI_VECTOR);
  if (!interrupt_ready) {
    /* A failed MSI prepare never enables delivery; MSI-X may need undoing. */
    if (!controller->msi_only && !pci_msix_disable(&controller->msix)) {
      controller->failure = failure;
      kerror("xHCI %x:%x.%u: %s; resources retained until reboot\n",
             controller->address.bus, controller->address.device, controller->address.function,
             failure);
      controller->failed = true;
      usb_inventory_controller_failed(controller->inventory_index);
      return;
    }
    goto fail;
  }
  controller->prepared = true;
  kdebug("xHCI %x:%x.%u: controller prepared, command/event rings=%u TRBs; DMA disabled\n",
         controller->address.bus, controller->address.device, controller->address.function,
         (unsigned)XHCI_RING_TRBS);
  return;

fail:
  controller->failure = failure;
  kerror("xHCI %x:%x.%u: %s\n",
         controller->address.bus, controller->address.device, controller->address.function,
         failure);
  release_boot_resources(controller);
  controller->failed = true;
  usb_inventory_controller_failed(controller->inventory_index);
}

void xhci_prepare(const struct boot_info *boot)
{
  usb_inventory_prepare();
  struct usb_host_controller **next = &controllers;
  for (size_t index = 0; index < usb_inventory_controller_count(); ++index) {
    size_t pci_index = usb_inventory_pci_index(index);
    const struct pci_device *device = pci_device_at(pci_index);
    if (!device) {
      usb_inventory_controller_failed(index);
      continue;
    }
    if (device->interface != XHCI_PCI_INTERFACE) {
      continue;
    }
    struct usb_host_controller *controller = kmalloc(sizeof(*controller));
    if (!controller) {
      kerror("xHCI %x:%x.%u: cannot allocate controller record\n",
             device->address.bus, device->address.device, device->address.function);
      usb_inventory_controller_failed(index);
      continue;
    }
    *controller = (struct usb_host_controller){
      .address = device->address,
      .inventory_index = index,
    };
    *next = controller;
    next = &controller->next;
    prepare_controller(controller, pci_index, boot);
  }
}

void xhci_interrupt(void)
{
  for (struct usb_host_controller *controller = controllers; controller; controller = controller->next) {
    if (!controller->interrupt_ready) {
      continue;
    }
    ++controller->interrupts;
    uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
    if (status & XHCI_STATUS_INTERRUPT) {
      write32(operational(controller), XHCI_OP_STATUS, XHCI_STATUS_INTERRUPT);
    }
    uint32_t management = read32(interrupter(controller), XHCI_INTR_MANAGEMENT);
    write32(interrupter(controller), XHCI_INTR_MANAGEMENT, management);
    /* MSI or MSI-X delivery can already have cleared IP. The shared vector notifies
     * every active controller, whose worker checks its own cycle-owned events. */
    usb_host_notify(controller);
  }
}

void usb_host_notify(struct usb_host_controller *controller)
{
  spin_lock(&controller->notification_lock);
  controller->notified = true;
  struct task_wait *wait = controller->wait;
  controller->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
  spin_unlock(&controller->notification_lock);
}

static void wait_activity(struct usb_host_controller *controller, uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&controller->notification_lock);
  if (!controller->notified) {
    struct task_wait *wait = task_wait_prepare();
    controller->wait = wait;
    spin_unlock(&controller->notification_lock);
    task_wait_sleep_until(wait, deadline);
    spin_lock(&controller->notification_lock);
    if (controller->wait == wait) {
      controller->wait = NULL;
    }
  }
  controller->notified = false;
  spin_unlock(&controller->notification_lock);
  cpu_restore_interrupts(flags);
}

static bool consume_command(struct usb_host_controller *controller, const struct xhci_trb *event)
{
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  unsigned slot = event->control >> XHCI_TRB_SLOT_SHIFT;
  bool rejected = controller->command.admission &&
    ((controller->command.type == XHCI_TRB_ENABLE_SLOT && completion == XHCI_EVENT_NO_SLOTS) ||
     ((controller->command.type == XHCI_TRB_CONFIGURE_ENDPOINT ||
       controller->command.type == XHCI_TRB_ADDRESS_DEVICE) && completion == XHCI_EVENT_RESOURCE) ||
     (controller->command.type == XHCI_TRB_CONFIGURE_ENDPOINT &&
      (completion == XHCI_EVENT_BANDWIDTH || completion == XHCI_EVENT_SECONDARY_BANDWIDTH)));
  bool stopped_race = controller->command.type == XHCI_TRB_STOP_ENDPOINT &&
    completion == XHCI_EVENT_CONTEXT_STATE;
  if (!controller->command.pending || event->parameter != controller->command.physical ||
      (completion != XHCI_EVENT_SUCCESS && !rejected && !stopped_race) ||
      (slot > controller->slot_count) || (!slot && !(rejected && completion == XHCI_EVENT_NO_SLOTS))) {
    controller->failure = "invalid or failed command completion";
    return false;
  }
  if (controller->command.type == XHCI_TRB_ENABLE_SLOT) {
    if (completion == XHCI_EVENT_SUCCESS) {
      for (unsigned i = 0; i < device_capacity(controller); ++i) {
        if (device_at(controller, i)->slot == slot && !device_at(controller, i)->removed) {
          controller->failure = "Enable Slot returned an owned slot";
          return false;
        }
      }
    }
  } else if (slot != controller->command.slot) {
    controller->failure = "command completion changed slot identity";
    return false;
  }
  controller->command.slot = slot;
  controller->command.completion = completion;
  controller->command.pending = false;
  ++controller->commands_completed;
  return true;
}

static bool publish_interrupt_receive(struct usb_host_interrupt *interrupt,
                                      struct xhci_interrupt_receive *receive)
{
  struct usb_host_device *device = interrupt->device;
  struct usb_host_controller *controller = device->controller;
  unsigned index = interrupt->enqueue;
  phys_addr_t physical = interrupt->ring.physical + index * XHCI_TRB_BYTES;
  for (unsigned i = 0; i < USB_INTERRUPT_RECEIVES; ++i) {
    if (interrupt->receive[i].state != INTERRUPT_FREE && interrupt->receive[i].trb == physical) {
      controller->failure = "interrupt producer frontier is still owned";
      return false;
    }
  }
  uint64_t flags = cpu_save_interrupts();
  bool cycle = interrupt->cycle;
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)interrupt->ring.address;
  receive->trb = physical;
  receive->state = INTERRUPT_POSTED;
  ring[index].parameter = receive->data_physical;
  ring[index].status = interrupt->receive_bytes;
  if (++interrupt->enqueue == XHCI_RING_TRBS - 1) {
    ring[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
      XHCI_TRB_TOGGLE_CYCLE | (cycle ? XHCI_TRB_CYCLE : 0);
    interrupt->enqueue = 0;
    interrupt->cycle = !cycle;
  }
  dma_write_barrier();
  ring[index].control = (XHCI_TRB_NORMAL << XHCI_TRB_TYPE_SHIFT) |
    XHCI_TRB_COMPLETION_INTERRUPT | XHCI_TRB_SHORT_INTERRUPT | (cycle ? XHCI_TRB_CYCLE : 0);
  dma_write_barrier();
  write32(controller->registers.address, controller->doorbells + device->slot * XHCI_DOORBELL_BYTES,
          interrupt->dci);
  cpu_restore_interrupts(flags);
  return true;
}

static void mark_device_retiring(struct usb_host_device *device)
{
  device->retiring = true;
  device->identity_hold = false;
  for (unsigned i = 0; i < USB_HID_ENDPOINTS_PER_DEVICE; ++i) {
    struct usb_host_interrupt *interrupt = device->interrupt[i];
    if (interrupt && interrupt->result == USB_OK) {
      interrupt->result = USB_IO;
    }
  }
  if (device->request.state == CONTROL_ACTIVE || device->request.state == CONTROL_HALTED) {
    device->request.state = CONTROL_HELD;
    device->request.result = USB_IO;
  }
  if (device->bulk && (device->bulk->state == CONTROL_ACTIVE || device->bulk->state == CONTROL_HALTED)) {
    device->bulk->state = CONTROL_HELD;
    device->bulk->result = USB_IO;
  }
  if (device->async_bulk) {
    struct xhci_async_bulk *bulk = device->async_bulk;
    if (bulk->result == USB_OK) {
      bulk->result = USB_IO;
    }
    if (bulk->tx.state == CONTROL_ACTIVE || bulk->tx.state == CONTROL_HALTED) {
      bulk->tx.state = CONTROL_HELD;
      bulk->tx.result = USB_IO;
    }
  }
}

static bool in_subtree(const struct usb_host_device *device, const struct usb_host_device *ancestor)
{
  if (!ancestor->parent) {
    return device->port == ancestor->port;
  }
  for (const struct usb_host_device *parent = device; parent; parent = parent->parent) {
    if (parent == ancestor) {
      return true;
    }
  }
  return false;
}

static void mark_subtree_retiring(struct usb_host_device *ancestor)
{
  struct usb_host_controller *controller = ancestor->controller;
  /* Command waits drain events and rearm other streams. Close admission on the
   * affected subtree before the first endpoint fence can yield. */
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *device = device_at(controller, i);
    if (!device->removed && (device->slot || device->hid) && in_subtree(device, ancestor)) {
      mark_device_retiring(device);
    }
  }
}

static void mark_disconnected(struct usb_host_device *device)
{
  while (device->parent) {
    device = device->parent;
  }
  mark_subtree_retiring(device);
}

static bool rearm_interrupts(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < controller->interrupt_count; ++i) {
    struct usb_host_interrupt *interrupt = &controller->interrupt_streams[i];
    if (!interrupt->started || interrupt->device->removed || interrupt->device->retiring) {
      continue;
    }
    if (device_identity_held(interrupt->device)) {
      continue;
    }
    if (!device_present(interrupt->device)) {
      mark_disconnected(interrupt->device);
      continue;
    }
    if (interrupt->result != USB_OK) {
      continue;
    }
    for (unsigned j = 0; j < USB_INTERRUPT_RECEIVES; ++j) {
      if (interrupt->receive[j].state == INTERRUPT_FREE &&
          !publish_interrupt_receive(interrupt, &interrupt->receive[j])) {
        return false;
      }
    }
  }
  return true;
}

static bool consume_interrupt(struct usb_host_controller *controller, struct usb_host_interrupt *interrupt,
                              const struct xhci_trb *event)
{
  struct xhci_interrupt_receive *receive = NULL;
  for (unsigned i = 0; i < USB_INTERRUPT_RECEIVES; ++i) {
    if (interrupt->receive[i].state == INTERRUPT_POSTED && interrupt->receive[i].trb == event->parameter) {
      receive = &interrupt->receive[i];
      break;
    }
  }
  if (!receive) {
    controller->failure = "unowned interrupt transfer event";
    return false;
  }
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  size_t residue = event->status & XHCI_EVENT_RESIDUE_MASK;
  if ((boot_options_get()->usb_xhci_diagnose || boot_options_get()->usb_hid_describe) && interrupt->result == USB_OK &&
      ((completion != XHCI_EVENT_SUCCESS && completion != XHCI_EVENT_SHORT_PACKET) ||
       residue > interrupt->receive_bytes)) {
    interrupt->diagnostic_event = *event;
  }
  bool successful_short = completion == XHCI_EVENT_SUCCESS && residue &&
      residue <= interrupt->receive_bytes;
  if ((completion != XHCI_EVENT_SUCCESS && completion != XHCI_EVENT_SHORT_PACKET) ||
      residue > interrupt->receive_bytes ||
      (successful_short && !interrupt->successful_short_traced)) {
    ktrace("xHCI %x:%x.%u: interrupt IN completion code %u residue %zu requested %zu "
        "slot %u dci %u receive %u ring index %llu control %x\n",
        controller->address.bus, controller->address.device, controller->address.function,
        completion, residue, interrupt->receive_bytes, interrupt->device->slot,
        interrupt->dci, (unsigned)(receive - interrupt->receive),
        (unsigned long long)((receive->trb - interrupt->ring.physical) / XHCI_TRB_BYTES),
        event->control);
    if (LOG_LEVEL_CEILING >= LOG_LEVEL_TRACE && successful_short) {
      interrupt->successful_short_traced = true;
    }
  }
  /* Some controllers report successful short IN transfers as SUCCESS.
   * Ownership and the bounded residual establish the retired byte count. */
  if ((completion == XHCI_EVENT_SUCCESS || completion == XHCI_EVENT_SHORT_PACKET) &&
      residue <= interrupt->receive_bytes) {
    size_t actual = interrupt->receive_bytes - residue;
    receive->state = INTERRUPT_FREE;
    /* Terminal failure stops rearm, but other posted TDs still have owners. */
    if (interrupt->result != USB_OK) {
      return true;
    }
    if (interrupt->queue_count == USB_INTERRUPT_COMPLETIONS || interrupt->sequence == UINT64_MAX) {
      if (boot_options_get()->usb_xhci_diagnose || boot_options_get()->usb_hid_describe) {
        interrupt->diagnostic_event = *event;
      }
      interrupt->result = USB_DISCONTINUITY;
      return true;
    }
    unsigned tail = (interrupt->queue_head + interrupt->queue_count) % USB_INTERRUPT_COMPLETIONS;
    struct xhci_interrupt_record *record = &interrupt->queue[tail];
    dma_read_barrier();
    memcpy(record->bytes, (const void *)receive->data_address, actual);
    record->completion = (struct usb_interrupt_completion){.sequence = ++interrupt->sequence, .bytes = actual};
    ++interrupt->queue_count;
    return true;
  }
  receive->state = INTERRUPT_HELD;
  if (interrupt->device->retiring && residue <= interrupt->receive_bytes &&
      (completion == XHCI_EVENT_STALL || completion == XHCI_EVENT_TRANSACTION_ERROR ||
       completion == XHCI_EVENT_BABBLE)) {
    return true;
  }
  if (completion == XHCI_EVENT_STALL && residue <= interrupt->receive_bytes) {
    if (interrupt->kind == USB_INTERRUPT_HID) {
      mark_device_retiring(interrupt->device);
      return true;
    }
    if (interrupt->result == USB_OK) {
      interrupt->result = USB_STALL;
    }
    return true;
  }
  if (residue <= interrupt->receive_bytes &&
      (completion == XHCI_EVENT_TRANSACTION_ERROR || completion == XHCI_EVENT_BABBLE)) {
    mark_subtree_retiring(interrupt->device);
    return true;
  }
  interrupt->result = USB_IO;
  controller->failure = "failed or invalid interrupt transfer completion";
  return false;
}

static bool publish_async_bulk_receive(struct xhci_async_bulk *bulk,
                                       struct xhci_async_bulk_receive *receive)
{
  struct usb_host_device *device = bulk->device;
  struct usb_host_controller *controller = device->controller;
  struct xhci_bulk_endpoint *endpoint = &bulk->in;
  unsigned index = endpoint->enqueue;
  phys_addr_t physical = endpoint->ring.physical + index * XHCI_TRB_BYTES;
  for (unsigned i = 0; i < USB_ASYNC_BULK_RECEIVES; ++i) {
    if (bulk->receive[i].state != ASYNC_BULK_FREE && bulk->receive[i].trb == physical) {
      controller->failure = "async bulk producer frontier is still owned";
      return false;
    }
  }
  uint64_t flags = cpu_save_interrupts();
  bool cycle = endpoint->cycle;
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)endpoint->ring.address;
  receive->trb = physical;
  receive->state = ASYNC_BULK_POSTED;
  ring[index].parameter = receive->data_physical;
  ring[index].status = bulk->receive_bytes;
  if (++endpoint->enqueue == XHCI_RING_TRBS - 1) {
    ring[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
      XHCI_TRB_TOGGLE_CYCLE | (cycle ? XHCI_TRB_CYCLE : 0);
    endpoint->enqueue = 0;
    endpoint->cycle = !cycle;
  }
  dma_write_barrier();
  ring[index].control = (XHCI_TRB_NORMAL << XHCI_TRB_TYPE_SHIFT) |
    XHCI_TRB_COMPLETION_INTERRUPT | XHCI_TRB_SHORT_INTERRUPT | (cycle ? XHCI_TRB_CYCLE : 0);
  dma_write_barrier();
  write32(controller->registers.address, controller->doorbells + device->slot * XHCI_DOORBELL_BYTES,
          endpoint->dci);
  cpu_restore_interrupts(flags);
  return true;
}

static bool rearm_async_bulk(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < controller->async_bulk_count; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    if (!bulk->started || bulk->device->removed || bulk->device->retiring ||
        device_identity_held(bulk->device)) {
      continue;
    }
    if (!device_present(bulk->device)) {
      mark_disconnected(bulk->device);
      continue;
    }
    if (bulk->result != USB_OK || bulk->boot_state != ASYNC_BULK_BOOT_NONE) {
      continue;
    }
    unsigned reserved = bulk->queue_count;
    for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
      reserved += bulk->receive[j].state == ASYNC_BULK_POSTED;
    }
    for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
      if (bulk->receive[j].state != ASYNC_BULK_FREE) {
        continue;
      }
      /* Each posted network TD reserves a software completion slot, so a
       * consumer stall cannot overflow the queue when those TDs complete. */
      if (bulk->network && reserved >= USB_ASYNC_BULK_COMPLETIONS) {
        break;
      }
      if (!publish_async_bulk_receive(bulk, &bulk->receive[j])) {
        return false;
      }
      ++reserved;
    }
  }
  return true;
}

static bool async_bulk_deadlines(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < controller->async_bulk_count; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    if (bulk->device && !bulk->device->removed && !bulk->device->retiring &&
        !device_present(bulk->device)) {
      mark_disconnected(bulk->device);
    }
    if (bulk->tx.state == CONTROL_ACTIVE &&
        task_deadline_expired(bulk->tx.deadline)) {
      bulk->tx.state = CONTROL_HELD;
      bulk->tx.result = USB_TIMEOUT;
      mark_subtree_retiring(bulk->device);
    }
  }
  return true;
}

static bool consume_async_bulk(struct usb_host_controller *controller, struct xhci_async_bulk *bulk,
                               unsigned endpoint, const struct xhci_trb *event)
{
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  size_t residue = event->status & XHCI_EVENT_RESIDUE_MASK;
  if (endpoint == bulk->out.dci) {
    if ((bulk->tx.state != CONTROL_ACTIVE &&
         !(bulk->device->retiring && bulk->tx.state == CONTROL_HELD)) || event->parameter != bulk->tx.trb) {
      controller->failure = "unowned async bulk OUT transfer event";
      return false;
    }
    if (bulk->device->retiring && residue <= bulk->tx.requested &&
        ((completion == XHCI_EVENT_SUCCESS && !residue) || completion == XHCI_EVENT_STALL ||
         completion == XHCI_EVENT_TRANSACTION_ERROR || completion == XHCI_EVENT_BABBLE)) {
      return true;
    }
    if (completion == XHCI_EVENT_SUCCESS && !residue) {
      bulk->tx.actual = bulk->tx.requested;
      bulk->tx.result = USB_OK;
      bulk->tx.state = CONTROL_DONE;
      return true;
    }
    bulk->tx.state = CONTROL_HELD;
    if (completion == XHCI_EVENT_STALL && residue <= bulk->tx.requested) {
      bulk->tx.result = USB_STALL;
      return true;
    }
    bulk->tx.result = USB_IO;
    if (residue <= bulk->tx.requested &&
        (completion == XHCI_EVENT_TRANSACTION_ERROR || completion == XHCI_EVENT_BABBLE)) {
      mark_subtree_retiring(bulk->device);
      return true;
    }
    controller->failure = "failed or invalid async bulk OUT transfer completion";
    return false;
  }
  struct xhci_async_bulk_receive *receive = NULL;
  for (unsigned i = 0; i < USB_ASYNC_BULK_RECEIVES; ++i) {
    if (bulk->receive[i].state == ASYNC_BULK_POSTED && bulk->receive[i].trb == event->parameter) {
      receive = &bulk->receive[i];
      break;
    }
  }
  if (endpoint != bulk->in.dci || !receive) {
    controller->failure = "unowned async bulk IN transfer event";
    return false;
  }
  if (bulk->device->retiring && residue <= bulk->receive_bytes &&
      (completion == XHCI_EVENT_SUCCESS || completion == XHCI_EVENT_SHORT_PACKET ||
       completion == XHCI_EVENT_STALL || completion == XHCI_EVENT_TRANSACTION_ERROR ||
       completion == XHCI_EVENT_BABBLE)) {
    receive->state = ASYNC_BULK_HELD;
    return true;
  }
  bool successful_short = completion == XHCI_EVENT_SUCCESS && residue &&
      residue <= bulk->receive_bytes;
  if ((completion != XHCI_EVENT_SUCCESS && completion != XHCI_EVENT_SHORT_PACKET) ||
      residue > bulk->receive_bytes || (successful_short && !bulk->successful_short_traced)) {
    ktrace("xHCI %x:%x.%u: async bulk IN completion code %u residue %zu requested %zu "
        "slot %u dci %u receive %u ring index %llu control %x queued bulk/interrupt %u/%u\n",
        controller->address.bus, controller->address.device, controller->address.function,
        completion, residue, bulk->receive_bytes, bulk->device->slot, endpoint,
        (unsigned)(receive - bulk->receive),
        (unsigned long long)((receive->trb - bulk->in.ring.physical) / XHCI_TRB_BYTES),
        event->control, bulk->queue_count,
        bulk->device->interrupt[0] ? bulk->device->interrupt[0]->queue_count : 0);
    if (LOG_LEVEL_CEILING >= LOG_LEVEL_TRACE && successful_short) {
      bulk->successful_short_traced = true;
    }
  }
  if (!bulk->network && completion == XHCI_EVENT_TRANSACTION_ERROR && residue == bulk->receive_bytes &&
      bulk->result == USB_OK && bulk->boot_state == ASYNC_BULK_BOOT_NONE &&
      !bulk->queue_count && bluetooth_hci_boot_bulk_end(bulk->device)) {
    /* The error halted IN; neither this TD nor its prefetched successor may be
     * reused until the outer worker's reset/dequeue fence has completed. */
    receive->state = ASYNC_BULK_HELD;
    bulk->in.halted = true;
    bulk->boot_state = ASYNC_BULK_BOOT_HALTED;
    return true;
  }
  if (bulk->boot_state != ASYNC_BULK_BOOT_NONE) {
    receive->state = ASYNC_BULK_HELD;
    bulk->result = USB_IO;
    controller->failure = "unexpected bulk IN completion after firmware boot halt";
    return false;
  }
  /* Match the retained interrupt path's successful short-transfer handling. */
  if ((completion == XHCI_EVENT_SUCCESS || completion == XHCI_EVENT_SHORT_PACKET) &&
      residue <= bulk->receive_bytes) {
    size_t actual = bulk->receive_bytes - residue;
    /* Terminal failure stops rearm, but other posted TDs still have owners. */
    if (bulk->result != USB_OK) {
      receive->state = ASYNC_BULK_FREE;
      return true;
    }
    if (bulk->queue_count == USB_ASYNC_BULK_COMPLETIONS || bulk->sequence == UINT64_MAX) {
      receive->state = ASYNC_BULK_FREE;
      bulk->result = USB_DISCONTINUITY;
      return true;
    }
    unsigned tail = (bulk->queue_head + bulk->queue_count) % USB_ASYNC_BULK_COMPLETIONS;
    struct xhci_async_bulk_record *record = &bulk->queue[tail];
    dma_read_barrier();
    memcpy(record->bytes, (const void *)receive->data_address, actual);
    record->completion = (struct usb_interrupt_completion){.sequence = ++bulk->sequence, .bytes = actual};
    ++bulk->queue_count;
    receive->state = ASYNC_BULK_FREE;
    return true;
  }
  receive->state = ASYNC_BULK_HELD;
  if (completion == XHCI_EVENT_STALL && residue <= bulk->receive_bytes) {
    if (bulk->result == USB_OK) {
      bulk->result = USB_STALL;
    }
    return true;
  }
  bulk->result = USB_IO;
  if (residue <= bulk->receive_bytes &&
      (completion == XHCI_EVENT_TRANSACTION_ERROR || completion == XHCI_EVENT_BABBLE)) {
    mark_subtree_retiring(bulk->device);
    return true;
  }
  controller->failure = "failed or invalid async bulk IN transfer completion";
  return false;
}

static bool consume_bulk(struct usb_host_controller *controller, struct usb_host_device *device,
                         unsigned endpoint, const struct xhci_trb *event)
{
  struct xhci_bulk *bulk = device->bulk;
  if (!bulk || (bulk->state != CONTROL_ACTIVE &&
               !(device->retiring && bulk->state == CONTROL_HELD)) || !bulk->active ||
      endpoint != bulk->active->dci || event->parameter != bulk->trb) {
    controller->failure = "unowned bulk transfer event";
    return false;
  }
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  size_t residue = event->status & XHCI_EVENT_RESIDUE_MASK;
  bool inbound = bulk->active == &bulk->in;
  if (device->retiring && residue <= bulk->requested &&
      ((completion == XHCI_EVENT_SUCCESS && !residue) ||
       (completion == XHCI_EVENT_SHORT_PACKET && inbound) || completion == XHCI_EVENT_STALL ||
       completion == XHCI_EVENT_TRANSACTION_ERROR || completion == XHCI_EVENT_BABBLE)) {
    return true;
  }
  if ((completion == XHCI_EVENT_SUCCESS && !residue) ||
      (completion == XHCI_EVENT_SHORT_PACKET && inbound && residue <= bulk->requested)) {
    bulk->actual = bulk->requested - residue;
    bulk->result = USB_OK;
    bulk->state = CONTROL_DONE;
    return true;
  }
  if (completion == XHCI_EVENT_STALL && residue <= bulk->requested) {
    bulk->actual = bulk->requested - residue;
    bulk->result = USB_STALL;
    bulk->active->halted = true;
    bulk->state = CONTROL_HALTED;
    return true;
  }
  bulk->result = USB_IO;
  bulk->state = CONTROL_HELD;
  if (residue <= bulk->requested &&
      (completion == XHCI_EVENT_TRANSACTION_ERROR || completion == XHCI_EVENT_BABBLE)) {
    mark_subtree_retiring(device);
    return true;
  }
  controller->failure = "failed or invalid bulk transfer completion";
  return false;
}

static const struct dma_buffer *endpoint_ring(struct usb_host_device *device, unsigned dci)
{
  if (dci == XHCI_ENDPOINT_ZERO) {
    return device->addressed ? &device->control_ring : NULL;
  }
  struct usb_host_interrupt *interrupt = device_interrupt(device, dci);
  if (interrupt && interrupt->configured) {
    return &interrupt->ring;
  }
  struct xhci_bulk *bulk = device->bulk;
  if (bulk && bulk->configured) {
    if (dci == bulk->in.dci) {
      return &bulk->in.ring;
    }
    if (dci == bulk->out.dci) {
      return &bulk->out.ring;
    }
  }
  struct xhci_async_bulk *async = device->async_bulk;
  if (async && async->configured) {
    if (dci == async->in.dci) {
      return &async->in.ring;
    }
    if (dci == async->out.dci) {
      return &async->out.ring;
    }
  }
  return NULL;
}

static bool consume_transfer(struct usb_host_controller *controller, const struct xhci_trb *event)
{
  unsigned slot = event->control >> XHCI_TRB_SLOT_SHIFT;
  unsigned endpoint = (event->control >> XHCI_EVENT_ENDPOINT_SHIFT) & XHCI_EVENT_ENDPOINT_MASK;
  struct usb_host_device *device = NULL;
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    if (device_at(controller, i)->slot == slot && slot && !device_at(controller, i)->removed) {
      device = device_at(controller, i);
      break;
    }
  }
  if (!device || (event->control & XHCI_EVENT_DATA)) {
    controller->failure = "unowned transfer event";
    return false;
  }
  if (!device->retiring && !device_present(device)) {
    mark_disconnected(device);
  }
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  bool stopped = completion == XHCI_EVENT_STOPPED || completion == XHCI_EVENT_STOPPED_LENGTH_INVALID ||
    completion == XHCI_EVENT_STOPPED_SHORT;
  if (stopped && device->retiring && device->stopping_dci == endpoint &&
      controller->command.pending && controller->command.type == XHCI_TRB_STOP_ENDPOINT &&
      controller->command.slot == slot) {
    const struct dma_buffer *ring = endpoint_ring(device, endpoint);
    if (!ring || event->parameter < ring->physical || event->parameter >= ring->physical + ring->bytes ||
        (event->parameter - ring->physical) % XHCI_TRB_BYTES || device->stopped_trb) {
      controller->failure = "invalid stopped endpoint event identity";
      return false;
    }
    /* A forced stop between TDs names the frontier, not a posted receive. */
    device->stopped_trb = event->parameter;
    return true;
  }
  if (endpoint != XHCI_ENDPOINT_ZERO) {
    struct usb_host_interrupt *interrupt = device_interrupt(device, endpoint);
    if (interrupt) {
      return consume_interrupt(controller, interrupt, event);
    }
    if (device->async_bulk &&
        (endpoint == device->async_bulk->in.dci || endpoint == device->async_bulk->out.dci)) {
      return consume_async_bulk(controller, device->async_bulk, endpoint, event);
    }
    return consume_bulk(controller, device, endpoint, event);
  }
  if ((device->request.state != CONTROL_ACTIVE &&
       !(device->retiring && device->request.state == CONTROL_HELD)) ||
      (event->parameter != device->request.setup && event->parameter != device->request.status &&
       (!device->request.requested || event->parameter != device->request.data))) {
    controller->failure = "unowned control transfer event";
    return false;
  }
  size_t residue = event->status & XHCI_EVENT_RESIDUE_MASK;
  if (completion == XHCI_EVENT_SHORT_PACKET && event->parameter == device->request.data &&
      device->request.inbound && !device->request.short_packet && residue <= device->request.requested) {
    device->request.actual = device->request.requested - residue;
    device->request.short_packet = true;
    return true;
  }
  if (completion == XHCI_EVENT_SUCCESS && event->parameter == device->request.status && !residue) {
    dma_read_barrier();
    device->request.result = device->retiring ? USB_IO : USB_OK;
    device->request.state = device->retiring ? CONTROL_HELD :
      device->request.client ? CONTROL_DONE : CONTROL_IDLE;
    return true;
  }
  if (completion == XHCI_EVENT_STALL && residue <= device->request.requested) {
    device->request.result = device->retiring ? USB_IO : USB_STALL;
    device->request.state = device->retiring ? CONTROL_HELD : CONTROL_HALTED;
    return true;
  }
  /* An early error does not retire the rest of the control TD sequence. */
  device->request.result = USB_IO;
  device->request.state = CONTROL_HELD;
  if (residue <= device->request.requested &&
      (completion == XHCI_EVENT_TRANSACTION_ERROR || completion == XHCI_EVENT_BABBLE)) {
    mark_subtree_retiring(device);
    return true;
  }
  controller->failure = "failed or invalid control transfer completion";
  return false;
}

static bool drain_events(struct usb_host_controller *controller)
{
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)controller->event_ring.address;
  unsigned consumed = 0;
  while (consumed < XHCI_RING_TRBS) {
    volatile struct xhci_trb *entry = &ring[controller->event_dequeue];
    uint32_t control = entry->control;
    if ((bool)(control & XHCI_TRB_CYCLE) != controller->event_cycle) {
      break;
    }
    dma_read_barrier();
    struct xhci_trb event = {.parameter = entry->parameter, .status = entry->status, .control = control};
    if (boot_options_get()->usb_xhci_diagnose) {
      controller->diagnostic_event = event;
    }
    unsigned type = (control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;
    if (type == XHCI_EVENT_COMMAND) {
      if (!consume_command(controller, &event)) {
        return false;
      }
    } else if (type == XHCI_EVENT_TRANSFER) {
      if (!consume_transfer(controller, &event)) {
        return false;
      }
    } else if (type == XHCI_EVENT_PORT) {
      unsigned port = (event.parameter >> XHCI_EVENT_PORT_SHIFT) & XHCI_EXT_ID_MASK;
      if (!port || port > controller->port_count ||
          event.status >> XHCI_EVENT_COMPLETION_SHIFT != XHCI_EVENT_SUCCESS) {
        controller->failure = "invalid root-port event";
        return false;
      }
      controller->ports[port - 1].dirty = true;
      if (controller->hotplug_ready && controller->ports[port - 1].state == PORT_RESERVED) {
        controller->ports[port - 1].identity_hold = true;
      }
    } else {
      controller->failure = "unexpected controller event";
      return false;
    }
    ++consumed;
    ++controller->events_consumed;
    if (++controller->event_dequeue == XHCI_RING_TRBS) {
      controller->event_dequeue = 0;
      controller->event_cycle = !controller->event_cycle;
    }
  }
  if (consumed) {
    dma_full_barrier();
    /* Never repeat an unchanged dequeue on empty polls. A whole-ring drain
     * may repeat the pointer: that is the specified full-to-empty exception. */
    write64(interrupter(controller), XHCI_INTR_DEQUEUE,
            (controller->event_ring.physical + controller->event_dequeue * XHCI_TRB_BYTES) | XHCI_DEQUEUE_BUSY);
  }
  /* Rearm only after the consumed events and ERDP are published. Every command,
   * control and bulk wait drains here; this progress issues no new commands. */
  if (!async_bulk_deadlines(controller)) {
    return false;
  }
  /* The class tick is bounded: it copies/collects and can publish transfers,
   * but cannot wait or recursively drain the event ring. */
  usb_hid_drain_progress(controller);
  bluetooth_hci_drain_progress(controller);
  if (!rearm_interrupts(controller) || !rearm_async_bulk(controller)) {
    return false;
  }
  return controller->running && !controller->failed;
}

static bool controller_healthy(struct usb_host_controller *controller)
{
  uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
  if (status & (XHCI_STATUS_HALTED | XHCI_STATUS_SYSTEM_ERROR | XHCI_STATUS_CONTROLLER_ERROR |
                XHCI_STATUS_NOT_READY)) {
    controller->failure = "controller stopped or reported an error";
    return diagnose_failed(controller);
  }
  return true;
}

static bool run_command_flags(struct usb_host_controller *controller, unsigned type, unsigned argument, phys_addr_t parameter, uint32_t flags, uint64_t deadline)
{
  KASSERT(!controller->command.pending);
  controller->command.timed_out = false;
  if (task_deadline_expired(deadline)) {
    controller->command.timed_out = true;
    controller->failure = "controller command deadline expired before admission";
    return diagnose_failed(controller);
  }
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)controller->command_ring.address;
  unsigned index = controller->command_enqueue;
  volatile struct xhci_trb *entry = &ring[index];
  unsigned control = (type << XHCI_TRB_TYPE_SHIFT) | flags;
  control |= argument << (type == XHCI_TRB_ENABLE_SLOT ? XHCI_TRB_SLOT_TYPE_SHIFT : XHCI_TRB_SLOT_SHIFT);
  controller->command.physical = controller->command_ring.physical + index * XHCI_TRB_BYTES;
  controller->command.type = type;
  controller->command.slot = type == XHCI_TRB_ENABLE_SLOT ? 0 : argument;
  controller->command.completion = 0;
  controller->command.pending = true;
  entry->parameter = parameter;
  entry->status = 0;
  dma_write_barrier();
  entry->control = control | (controller->command_cycle ? XHCI_TRB_CYCLE : 0);
  if (++controller->command_enqueue == XHCI_RING_TRBS - 1) {
    ring[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
      XHCI_TRB_TOGGLE_CYCLE | (controller->command_cycle ? XHCI_TRB_CYCLE : 0);
    controller->command_enqueue = 0;
    controller->command_cycle = !controller->command_cycle;
  }
  dma_write_barrier();
  uint64_t command_deadline = task_deadline_after_ms(USB_COMMAND_TIMEOUT_MS);
  if (deadline > command_deadline) {
    deadline = command_deadline;
  }
  write32(controller->registers.address, controller->doorbells, 0);
  while (controller->command.pending) {
    if (!controller_healthy(controller) || !drain_events(controller)) {
      return diagnose_failed(controller);
    }
    if (!controller->command.pending) {
      return true;
    }
    if (task_deadline_expired(deadline)) {
      controller->command.timed_out = true;
      controller->failure = "controller command deadline expired";
      return diagnose_failed(controller);
    }
    uint64_t poll = task_deadline_after_ms(USB_WORKER_POLL_MS);
    wait_activity(controller, poll < deadline ? poll : deadline);
  }
  return true;
}

static bool run_command(struct usb_host_controller *controller, unsigned type, unsigned argument,
                        phys_addr_t parameter, uint64_t deadline)
{
  return run_command_flags(controller, type, argument, parameter, 0, deadline);
}

static bool endpoint_command(struct usb_host_device *device, unsigned type, unsigned dci,
                             phys_addr_t parameter, uint64_t deadline)
{
  return run_command_flags(device->controller, type, device->slot, parameter,
                           dci << XHCI_TRB_ENDPOINT_SHIFT, deadline);
}

static bool root_port_live(uint32_t status)
{
  return (status & (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED)) ==
      (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED) && !(status & XHCI_PORT_OVER_CURRENT);
}

static uint32_t observe_port(struct usb_host_controller *controller, unsigned index)
{
  uint32_t status = read32(port_register(controller, index), 0);
  /* Never echo PED, reset, link-write strobe or unobserved W1C changes. */
  write32(port_register(controller, index), 0, (status & XHCI_PORT_NEUTRAL) | (status & XHCI_PORT_CHANGES));
  controller->ports[index].dirty = false;
  if ((status & XHCI_PORT_CONNECT_CHANGE) &&
      !(controller->ports[index].state == PORT_RESERVED && root_port_live(status))) {
    if (controller->ports[index].generation == UINT64_MAX) {
      controller->failure = "root connection generation exhausted";
    } else {
      ++controller->ports[index].generation;
    }
  }
  return status;
}

static void format_protocol_revision(const struct xhci_port *port, char *revision)
{
  /* Hex digits preserve the BCD fields; omit a zero subminor component. */
  unsigned minor = port->minor >> XHCI_REVISION_DIGIT_SHIFT;
  unsigned subminor = port->minor & XHCI_REVISION_DIGIT_MASK;
  if (subminor) {
    sprintf(revision, "%x.%x.%x", port->major, minor, subminor);
  } else {
    sprintf(revision, "%x.%x", port->major, minor);
  }
}

static bool root_ports_training(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < controller->port_count; ++i) {
    const struct xhci_port *port = &controller->ports[i];
    if (!port->protocol || (port->major != 2 && port->major != 3)) {
      continue;
    }
    uint32_t status = read32(port_register(controller, i), 0);
    if (!(status & XHCI_PORT_POWER)) {
      continue;
    }
    /* PR covers hot and warm reset; PRC/WRC are sticky completion reports.
     * WPR always reads zero. PLS is undefined while PR is asserted. */
    if (status & XHCI_PORT_RESET) {
      return true;
    }
    if (port->major == 3) {
      unsigned link = (status >> XHCI_PORT_LINK_SHIFT) & XHCI_PORT_LINK_MASK;
      if (link == XHCI_PORT_LINK_POLLING || link == XHCI_PORT_LINK_RECOVERY ||
          link == XHCI_PORT_LINK_HOT_RESET) {
        return true;
      }
    }
  }
  return false;
}

static bool settle_root_ports(struct usb_host_controller *controller, uint64_t deadline)
{
  uint64_t started = arch_monotonic_ns();
  uint64_t minimum = started + (uint64_t)USB_ROOT_SETTLE_MIN_MS * UINT64_C(1000000);
  uint64_t maximum = started + (uint64_t)USB_ROOT_SETTLE_MAX_MS * UINT64_C(1000000);
  const char *reason;
  bool settled = false;
  for (;;) {
    if (!controller_healthy(controller) || !drain_events(controller)) {
      reason = "controller failure";
      break;
    }
    uint64_t now = arch_monotonic_ns();
    if (now >= deadline) {
      controller->failure = "root-port settle deadline expired";
      reason = "enumeration deadline";
      break;
    }
    if (now >= minimum) {
      bool training = root_ports_training(controller);
      if (!training || now >= maximum) {
        reason = training ? "cap reached with training active" : "no active training";
        settled = true;
        break;
      }
    }
    uint64_t next = task_deadline_after_ms(USB_WORKER_POLL_MS);
    uint64_t boundary = now < minimum ? minimum : maximum;
    if (next > boundary) {
      next = boundary;
    }
    if (next > deadline) {
      next = deadline;
    }
    wait_activity(controller, next);
  }
  kdebug("xHCI %x:%x.%u: root-port settle %llu ms; %s\n",
         controller->address.bus, controller->address.device, controller->address.function,
         (unsigned long long)((arch_monotonic_ns() - started) / UINT64_C(1000000)), reason);
  return settled;
}

static bool prepare_ports(struct usb_host_controller *controller, uint64_t deadline)
{
  if (controller->port_power) {
    for (unsigned i = 0; i < controller->port_count; ++i) {
      uint32_t status = read32(port_register(controller, i), 0);
      if (!(status & XHCI_PORT_POWER)) {
        write32(port_register(controller, i), 0, (status & XHCI_PORT_NEUTRAL) | XHCI_PORT_POWER);
      }
    }
  }
  if (!settle_root_ports(controller, deadline)) {
    return false;
  }
  /* One boot-inventory snapshot. Later insertion needs a fresh runtime
   * topology context; it cannot acquire a startup device reservation. */
  for (unsigned i = 0; i < controller->port_count; ++i) {
    struct xhci_port *port = &controller->ports[i];
    uint32_t status = observe_port(controller, i);
    if (!(status & XHCI_PORT_CONNECTED)) {
      continue;
    }
    port->boot_present = true;
    port->state = port->protocol && (port->major == 2 || port->major == 3) ?
      PORT_CONNECTED : PORT_UNSUPPORTED;
  }
  for (unsigned i = 0; i < controller->port_count; ++i) {
    struct xhci_port *port = &controller->ports[i];
    if (port->state != PORT_CONNECTED) {
      if (port->state == PORT_UNSUPPORTED) {
        char revision[sizeof("ff.f.f")];
        format_protocol_revision(port, revision);
        kdebug("xHCI %x:%x.%u: port %u unsupported protocol %s\n",
               controller->address.bus, controller->address.device, controller->address.function,
               i + 1, revision);
      }
      continue;
    }
    uint32_t status = observe_port(controller, i);
    if (status & XHCI_PORT_CONNECT_CHANGE) {
      port->state = PORT_REMOVED;
      kwarn("xHCI %x:%x.%u: root port %u changed after startup snapshot; excluded from startup admission\n",
            controller->address.bus, controller->address.device, controller->address.function,
            i + 1);
      continue;
    }
    if (port->major == 2 && (status & XHCI_PORT_CONNECTED) && !(status & XHCI_PORT_ENABLED)) {
      write32(port_register(controller, i), 0, (status & XHCI_PORT_NEUTRAL) | XHCI_PORT_RESET);
    }
    while ((status & XHCI_PORT_CONNECTED) && !(status & XHCI_PORT_CONNECT_CHANGE) &&
           (!(status & XHCI_PORT_ENABLED) || (status & XHCI_PORT_RESET))) {
      if (!controller_healthy(controller) || !drain_events(controller)) {
        return false;
      }
      if (task_deadline_expired(deadline)) {
        controller->failure = "root-port setup deadline expired";
        return false;
      }
      wait_activity(controller, task_deadline_after_ms(USB_WORKER_POLL_MS));
      status = observe_port(controller, i);
    }
    if (port->major == XHCI_PROTOCOL_USB_2 && root_port_live(status) &&
        !(status & XHCI_PORT_CONNECT_CHANGE)) {
      if (usb_host_delay(controller, USB_RESET_RECOVERY_MS, deadline) != USB_OK) {
        if (!controller->failure) {
          controller->failure = "USB 2 root-port reset recovery failed";
        }
        return false;
      }
      status = observe_port(controller, i);
    }
    if (!(status & XHCI_PORT_CONNECTED) || (status & XHCI_PORT_CONNECT_CHANGE)) {
      port->state = PORT_REMOVED;
      continue;
    }
    if (status & XHCI_PORT_OVER_CURRENT) {
      controller->failure = "root-port over-current";
      return false;
    }
    port->speed = (status >> XHCI_PORT_SPEED_SHIFT) & XHCI_PORT_SPEED_MASK;
    if (!port->speed || !run_command(controller, XHCI_TRB_ENABLE_SLOT, port->slot_type, 0, deadline)) {
      if (!controller->failure) {
        controller->failure = "root port has no speed identity";
      }
      return false;
    }
    port->slot = controller->command.slot;
    port->device.slot = port->slot;
    port->device.link = port->speeds[port->speed].link;
    if (port->major == XHCI_PROTOCOL_USB_3 && !port->device.link.rx_lanes) {
      uint32_t lanes = read32(port_register(controller, i), XHCI_PORT_LINK_INFO);
      port->device.link.rx_lanes = ((lanes >> XHCI_PORT_RX_LANES_SHIFT) & XHCI_PORT_LANES_MASK) + 1;
      port->device.link.tx_lanes = ((lanes >> XHCI_PORT_TX_LANES_SHIFT) & XHCI_PORT_LANES_MASK) + 1;
    }
    port->device.raw_speed = port->speed;
    port->state = PORT_RESERVED;
    char revision[sizeof("ff.f.f")];
    format_protocol_revision(port, revision);
    kdebug("xHCI %x:%x.%u: root port %u USB %s speed-id=%u slot=%u enabled; addressing pending\n",
           controller->address.bus, controller->address.device, controller->address.function,
           i + 1, revision, port->speed, port->slot);
  }
  return true;
}

static bool retire_root_devices(struct usb_host_controller *controller, unsigned port)
{
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *device = device_at(controller, i);
    if (device->slot && !device->removed && device->port == port) {
      return usb_host_device_retire(device, task_deadline_after_ms(USB_COMMAND_TIMEOUT_MS)) == USB_OK;
    }
  }
  return true;
}

static bool update_ports(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < controller->port_count; ++i) {
    struct xhci_port *port = &controller->ports[i];
    if (!port->dirty) {
      continue;
    }
    uint64_t generation = port->generation;
    uint32_t status = observe_port(controller, i);
    if (port->state != PORT_RESERVED) {
      port->identity_hold = false;
      continue;
    }
    if (root_port_live(status)) {
      if (status & XHCI_PORT_CONNECT_CHANGE) {
        struct usb_host_device *device = port->active ? port->active : &port->device;
        bool same = device->slot && usb_host_identity_verify(device,
            task_deadline_after_ms(USB_COMMAND_TIMEOUT_MS));
        uint32_t current = read32(port_register(controller, i), 0);
        if (same && root_port_live(current) && !(current & XHCI_PORT_CONNECT_CHANGE)) {
          port->identity_hold = false;
          kdebug("xHCI %x:%x.%u: root port %u connect change verified PORTSC=0x%x\n",
                 controller->address.bus, controller->address.device, controller->address.function,
                 i + 1, status);
          continue;
        }
        status = current;
      } else {
        port->identity_hold = false;
        continue;
      }
    }
    if (port->generation == generation) {
      if (port->generation == UINT64_MAX) {
        controller->failure = "root connection generation exhausted";
        return false;
      }
      ++port->generation;
    }
    if (!retire_root_devices(controller, i)) {
      return false;
    }
    port->slot = 0;
    port->state = PORT_REMOVED;
    port->identity_hold = false;
    kdebug("xHCI %x:%x.%u: root port %u retired PORTSC=0x%x; old backing retained\n",
           controller->address.bus, controller->address.device, controller->address.function,
           i + 1, status);
  }
  uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
  if (status & XHCI_STATUS_PORT_CHANGE) {
    write32(operational(controller), XHCI_OP_STATUS, XHCI_STATUS_PORT_CHANGE);
  }
  return true;
}

static void stop_controller(struct usb_host_controller *controller)
{
  bool diagnostic = boot_options_get()->usb_xhci_diagnose && !controller->diagnostic_stopped;
  struct xhci_diagnostic_registers registers = {0};
  if (diagnostic) {
    controller->diagnostic_stopped = true;
    diagnose_failed(controller);
    registers = diagnostic_registers(controller);
  }
  uint64_t flags = cpu_save_interrupts();
  controller->interrupt_ready = false;
  set_interrupt_enable(controller, false);
  bool interrupts_disabled = interrupt_delivery(controller, false);
  cpu_restore_interrupts(flags);
  uintptr_t op = operational(controller);
  bool ready = !(read32(op, XHCI_OP_STATUS) & XHCI_STATUS_NOT_READY);
  if (ready) {
    uint32_t command = read32(op, XHCI_OP_COMMAND);
    write32(op, XHCI_OP_COMMAND, command & ~(XHCI_COMMAND_RUN | XHCI_COMMAND_INTERRUPT));
  }
  uint64_t deadline = task_deadline_after_ms(USB_STATE_TIMEOUT_MS);
  while (ready && !(read32(op, XHCI_OP_STATUS) & XHCI_STATUS_HALTED) &&
         !task_deadline_expired(deadline)) {
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  bool halted = ready && (read32(op, XHCI_OP_STATUS) & XHCI_STATUS_HALTED);
  flags = cpu_save_interrupts();
  uint16_t command = pci_read16(controller->claim.device->address, PCI_COMMAND);
  pci_write16(&controller->claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  cpu_restore_interrupts(flags);
  controller->running = false;
  controller->failed = true;
  usb_inventory_controller_failed(controller->inventory_index);
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *device = device_at(controller, i);
    if (device->bulk && device->bulk->state != CONTROL_IDLE) {
      device->bulk->state = CONTROL_HELD;
      device->bulk->result = USB_IO;
    }
    for (unsigned endpoint = 0; endpoint < USB_HID_ENDPOINTS_PER_DEVICE; ++endpoint) {
      struct usb_host_interrupt *interrupt = device->interrupt[endpoint];
      if (!interrupt) {
        continue;
      }
      if (interrupt->result == USB_OK) {
        interrupt->result = USB_IO;
      }
      for (unsigned j = 0; j < USB_INTERRUPT_RECEIVES; ++j) {
        if (interrupt->receive[j].state == INTERRUPT_POSTED) {
          interrupt->receive[j].state = INTERRUPT_HELD;
        }
      }
    }
    if (device->async_bulk) {
      struct xhci_async_bulk *bulk = device->async_bulk;
      if (bulk->result == USB_OK) {
        bulk->result = USB_IO;
      }
      if (bulk->tx.state == CONTROL_ACTIVE) {
        bulk->tx.state = CONTROL_HELD;
        bulk->tx.result = USB_IO;
      }
      for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
        if (bulk->receive[j].state == ASYNC_BULK_POSTED) {
          bulk->receive[j].state = ASYNC_BULK_HELD;
        }
      }
    }
    if (device->request.state == CONTROL_ACTIVE || device->request.state == CONTROL_HALTED) {
      device->request.state = CONTROL_HELD;
      device->request.result = USB_IO;
    }
  }
  bluetooth_hci_transport_failed(controller);
  usb_hid_controller_failed(controller);
  if (diagnostic) {
    /* Quarantine first. Endpoint/software metadata is post-stop; only these
     * saved controller registers describe the state before containment. */
    diagnose_controller(controller, true, &registers);
  }
  kerror("xHCI %x:%x.%u: %s; halt=%u interrupts-disabled=%u, all resources retained until reboot\n",
         controller->address.bus, controller->address.device, controller->address.function,
         controller->failure, halted, interrupts_disabled);
}

void usb_host_assert_worker(struct usb_host_controller *controller)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && kernel_task_is_current(controller_worker, controller));
  cpu_restore_interrupts(flags);
}

static void assert_device_owner(const struct usb_host_device *device)
{
  struct usb_host_controller *controller = device->controller;
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && kernel_task_is_current(controller_worker, controller));
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
  bool owned = false;
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    owned |= device == device_at(controller, i);
  }
  KASSERT(owned);
}

unsigned usb_host_descendant_capacity(const struct usb_host_controller *controller)
{
  return controller->descendant_capacity;
}

unsigned usb_host_port_count(const struct usb_host_controller *controller)
{
  return controller->port_count;
}

size_t usb_host_control_capacity(void)
{
  return USB_CONTROL_BYTES;
}

bool usb_host_inventory_complete(const struct usb_host_controller *controller)
{
  if (!controller->running || controller->failed) {
    return false;
  }
  for (unsigned i = 0; i < controller->port_count; ++i) {
    struct xhci_port *port = &controller->ports[i];
    if (port->boot_present && (port->state != PORT_RESERVED || port->device.link.speed == USB_SPEED_UNKNOWN)) {
      return false;
    }
    if (port->boot_present) {
      uint32_t status = read32(port_register(controller, i), 0);
      if (!root_port_live(status)) {
        return false;
      }
    }
  }
  return true;
}

struct usb_host_device *usb_host_device_at(struct usb_host_controller *controller, unsigned index)
{
  if (!controller->ports || index >= controller->port_count ||
      controller->ports[index].state != PORT_RESERVED) {
    return NULL;
  }
  return controller->ports[index].active ? controller->ports[index].active : &controller->ports[index].device;
}

bool usb_host_port_present(const struct usb_host_controller *controller, unsigned index)
{
  return controller->ports && index < controller->port_count && controller->ports[index].boot_present;
}

unsigned usb_host_device_depth(const struct usb_host_device *device)
{
  return device->depth;
}

enum usb_speed usb_host_device_speed(const struct usb_host_device *device)
{
  return device->link.speed;
}

static bool device_present(const struct usb_host_device *device)
{
  struct usb_host_controller *controller = device->controller;
  uint32_t status = read32(port_register(controller, device->port), 0);
  return !device->removed && device->slot &&
    controller->ports[device->port].state == PORT_RESERVED &&
    root_port_live(status);
}

static bool device_identity_held(const struct usb_host_device *device)
{
  if (device->removed || device->retiring) {
    return false;
  }
  if (device->controller->identity_probe == device) {
    return false;
  }
  if (device->controller->ports[device->port].identity_hold) {
    return true;
  }
  for (const struct usb_host_device *ancestor = device; ancestor; ancestor = ancestor->parent) {
    if (ancestor->identity_hold) {
      return true;
    }
  }
  return false;
}

static bool device_ready(const struct usb_host_device *device)
{
  struct usb_host_controller *controller = device->controller;
  return controller->running && !controller->failed && !device->retiring && device_present(device);
}

static uint32_t *input_slot(const struct usb_host_device *device)
{
  struct usb_host_controller *controller = device->controller;
  return (uint32_t *)(device->input.address + controller->context_bytes);
}

static uint32_t *input_endpoint(const struct usb_host_device *device, unsigned dci)
{
  struct usb_host_controller *controller = device->controller;
  return (uint32_t *)(device->input.address + (dci + 1) * controller->context_bytes);
}

static void set_control_endpoint(uint32_t *context, unsigned packet, phys_addr_t ring)
{
  context[1] = (packet << XHCI_ENDPOINT_PACKET_SHIFT) |
    (XHCI_ENDPOINT_CONTROL << XHCI_ENDPOINT_TYPE_SHIFT) | XHCI_ENDPOINT_ERRORS;
  context[2] = (uint32_t)ring | XHCI_TRB_CYCLE;
  context[3] = ring >> 32;
  context[4] = XHCI_CONTROL_AVERAGE_TRB;
}

static enum usb_result context_command(struct usb_host_device *device, unsigned type, uint64_t deadline)
{
  struct usb_host_controller *controller = device->controller;
  if (controller->command.admission && task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  dma_write_barrier();
  if (!run_command(controller, type, device->slot, device->input.physical, deadline)) {
    enum usb_result result = controller->command.timed_out ? USB_TIMEOUT : USB_IO;
    stop_controller(controller);
    return result;
  }
  return controller->command.completion == XHCI_EVENT_SUCCESS ? USB_OK : USB_BUSY;
}

enum usb_result usb_host_address(struct usb_host_device *device, uint64_t deadline)
{
  struct usb_host_controller *controller = device->controller;
  assert_device_owner(device);
  if (!device_ready(device)) {
    return USB_IO;
  }
  if (device->addressed || device->request.state != CONTROL_IDLE) {
    return USB_BUSY;
  }
  if (device->link.speed == USB_SPEED_UNKNOWN) {
    return USB_UNSUPPORTED;
  }
  device->packet = usb_speed_is_enhanced(device->link.speed) ? 512 : device->link.speed == USB_SPEED_HIGH ? 64 : 8;
  memset((void *)device->input.address, 0, device->input.bytes);
  uint32_t *input = (uint32_t *)device->input.address;
  input[1] = 3; /* Slot and EP0 only. */
  uint32_t *slot = input_slot(device);
  slot[0] = device->route | (1u << XHCI_SLOT_ENTRIES_SHIFT) |
    ((unsigned)device->raw_speed << XHCI_SLOT_SPEED_SHIFT) |
    (device->multi_tt ? XHCI_SLOT_MULTI_TT : 0);
  slot[1] = (device->port + 1) << XHCI_SLOT_ROOT_PORT_SHIFT;
  slot[2] = device->parent_slot | ((unsigned)device->parent_port << XHCI_SLOT_PARENT_PORT_SHIFT);
  set_control_endpoint(input_endpoint(device, XHCI_ENDPOINT_ZERO),
                       device->packet, device->control_ring.physical);
  volatile uint64_t *dcbaa = (volatile uint64_t *)controller->dcbaa.address;
  dcbaa[device->slot] = device->output.physical;
  controller->command.admission = device->hid || device->topology;
  enum usb_result result = context_command(device, XHCI_TRB_ADDRESS_DEVICE, deadline);
  controller->command.admission = false;
  if (result == USB_OK) {
    device->addressed = true;
    /* AddressDevice completion does not include the USB 2 recovery interval. */
    if (!usb_speed_is_enhanced(device->link.speed)) {
      result = usb_host_delay(controller, USB_ADDRESS_RECOVERY_MS, deadline);
    }
  }
  return result;
}

enum usb_result usb_host_configure_hub(struct usb_host_device *device, unsigned ports,
                                       unsigned tt_think_time, bool multi_tt, uint64_t deadline)
{
  assert_device_owner(device);
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  if ((!device->controller->enumerating && !device->topology) || device->hub_ports || device->async_bulk ||
      device->request.state != CONTROL_IDLE) {
    return USB_BUSY;
  }
  if (!ports || ports > UINT8_MAX || tt_think_time > XHCI_SLOT_TT_THINK_MAX ||
      (device->link.speed != USB_SPEED_FULL && device->link.speed != USB_SPEED_HIGH &&
       !usb_speed_is_enhanced(device->link.speed)) ||
      (usb_speed_is_enhanced(device->link.speed) && ports > XHCI_SLOT_ROUTE_PORT_MAX) ||
      (device->link.speed != USB_SPEED_HIGH && (tt_think_time || multi_tt))) {
    return USB_UNSUPPORTED;
  }
  memset((void *)device->input.address, 0, device->input.bytes);
  ((uint32_t *)device->input.address)[1] = 1; /* Slot only; EP0 remains enabled. */
  uint32_t *slot = input_slot(device);
  slot[0] = (1u << XHCI_SLOT_ENTRIES_SHIFT) | XHCI_SLOT_HUB |
    (multi_tt ? XHCI_SLOT_MULTI_TT : 0);
  slot[1] = ports << XHCI_SLOT_PORTS_SHIFT;
  slot[2] = tt_think_time << XHCI_SLOT_TT_THINK_SHIFT;
  /* Evaluate Context cannot update Hub, Number of Ports, MTT or TTT. */
  enum usb_result result = context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
  if (result == USB_OK) {
    device->hub_ports = ports;
    if (device->link.speed == USB_SPEED_HIGH) {
      device->multi_tt = multi_tt;
    }
  }
  return result;
}

static unsigned link_rank(const struct usb_link *link)
{
  if (!usb_speed_is_enhanced(link->speed) || !link->rx_lanes ||
      link->rx_lanes != link->tx_lanes || link->rx_bps != link->tx_bps) {
    return 0;
  }
  uint64_t lane_bps = link->rx_bps / link->rx_lanes;
  if (link->speed == USB_SPEED_SUPER && lane_bps == USB_SUPER_LANE_BPS && link->rx_lanes == 1) {
    return 1;
  }
  if (link->speed != USB_SPEED_SUPER_PLUS) {
    return 0;
  }
  if (lane_bps == USB_SUPER_LANE_BPS && link->rx_lanes == 2) {
    return 2;
  }
  if (lane_bps == USB_GEN2_LANE_BPS && link->rx_lanes == 1) {
    return 3;
  }
  return lane_bps == USB_GEN2_LANE_BPS && link->rx_lanes == 2 ? 4 : 0;
}

static unsigned child_speed_id(const struct xhci_port *root, const struct usb_link *link)
{
  unsigned match = 0;
  for (unsigned i = 1; i <= XHCI_PORT_SPEED_MASK; ++i) {
    const struct xhci_speed *profile = &root->speeds[i];
    if (profile->link.speed != link->speed) {
      continue;
    }
    if (!usb_speed_is_enhanced(link->speed)) {
      return i;
    }
    if (!profile->symmetric || profile->link.rx_bps != link->rx_bps ||
        profile->link.tx_bps != link->tx_bps ||
        (profile->link.rx_lanes && profile->link.rx_lanes != link->rx_lanes) ||
        (profile->link.tx_lanes && profile->link.tx_lanes != link->tx_lanes)) {
      continue;
    }
    if (match) {
      return 0;
    }
    match = i;
  }
  return match;
}

static struct usb_host_device *reserve_topology_device(struct usb_host_controller *controller)
{
  if (!usb_host_topology_available(controller)) {
    if (!controller->topology_exhaustion_reported) {
      controller->topology_exhaustion_reported = true;
      kwarn("xHCI %x:%x.%u: retained topology context budget exhausted (%u/%u protected=%u)\n",
            controller->address.bus, controller->address.device, controller->address.function,
            controller->topology_used, controller->topology_capacity, controller->topology_reserved);
    }
    return NULL;
  }
  struct usb_host_device *device = &controller->topology_devices[controller->topology_used++];
  device->topology = true;
  return device;
}

unsigned usb_host_topology_capacity(const struct usb_host_controller *controller)
{
  return controller->topology_capacity;
}

void usb_host_topology_reserve(struct usb_host_controller *controller, unsigned count)
{
  usb_host_assert_worker(controller);
  KASSERT(controller->topology_used <= controller->topology_capacity);
  KASSERT(count <= controller->topology_capacity - controller->topology_used);
  controller->topology_reserved = count;
}

unsigned usb_host_topology_available(const struct usb_host_controller *controller)
{
  KASSERT(controller->topology_used <= controller->topology_capacity);
  KASSERT(controller->topology_reserved <= controller->topology_capacity - controller->topology_used);
  return controller->topology_capacity - controller->topology_used - controller->topology_reserved;
}

bool usb_host_running(const struct usb_host_controller *controller)
{
  return controller->running && !controller->failed;
}

bool usb_host_topology_root_present(const struct usb_host_controller *controller, unsigned port)
{
  if (!usb_host_running(controller) || port >= controller->port_count ||
      !controller->ports[port].protocol) {
    return false;
  }
  uint32_t status = read32(port_register(controller, port), 0);
  return (status & XHCI_PORT_CONNECTED) && !(status & XHCI_PORT_OVER_CURRENT);
}

bool usb_host_topology_root_disconnected(const struct usb_host_controller *controller, unsigned port)
{
  if (!usb_host_running(controller) || port >= controller->port_count ||
      !controller->ports[port].protocol) {
    return false;
  }
  uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
  if (status & (XHCI_STATUS_HALTED | XHCI_STATUS_SYSTEM_ERROR |
                XHCI_STATUS_CONTROLLER_ERROR | XHCI_STATUS_NOT_READY)) {
    return false;
  }
  return !(read32(port_register(controller, port), 0) & XHCI_PORT_CONNECTED);
}

static enum usb_result enable_admitted_slot(struct usb_host_device *device, uint64_t deadline);

static enum usb_result validate_topology_attachment(struct usb_host_device *device,
                                                     uint64_t generation)
{
  struct usb_host_controller *controller = device->controller;
  struct xhci_port *root = &controller->ports[device->port];
  uint32_t status = read32(port_register(controller, device->port), 0);
  if (usb_host_running(controller) && root->generation == generation &&
      (status & (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED)) == (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED) &&
      !(status & (XHCI_PORT_CONNECT_CHANGE | XHCI_PORT_OVER_CURRENT | XHCI_PORT_RESET)) &&
      (root->major != XHCI_PROTOCOL_USB_3 ||
       ((status >> XHCI_PORT_LINK_SHIFT) & XHCI_PORT_LINK_MASK) == XHCI_PORT_LINK_U0) &&
      (!device->parent || device_ready(device->parent))) {
    return USB_OK;
  }
  /* Enable Slot already transferred ownership. Fence it before withholding the
   * pointer; its fresh contexts and numeric identity remain retained. */
  enum usb_result result = usb_host_device_retire(device,
      task_deadline_after_ms(USB_COMMAND_TIMEOUT_MS));
  return result == USB_OK ? USB_STALE : result;
}

static enum usb_result attach_child(struct usb_host_device *parent, unsigned port,
                                      const struct usb_link *link, bool topology, uint64_t deadline,
                                      struct usb_host_device **child)
{
  assert_device_owner(parent);
  if (!child || !link) {
    return USB_INVALID;
  }
  *child = NULL;
  struct usb_host_controller *controller = parent->controller;
  if (!device_ready(parent) || !parent->addressed) {
    return USB_IO;
  }
  if ((!controller->enumerating && !topology) || parent->request.state != CONTROL_IDLE || parent->request.client) {
    return USB_BUSY;
  }
  enum usb_speed speed = link->speed;
  bool enhanced = usb_speed_is_enhanced(speed);
  if (!parent->hub_ports || !port || port > parent->hub_ports ||
      parent->depth >= XHCI_SLOT_ROUTE_DEPTH ||
      (enhanced ? !usb_speed_is_enhanced(parent->link.speed) || !link_rank(link) :
        (speed != USB_SPEED_LOW && speed != USB_SPEED_FULL && speed != USB_SPEED_HIGH) ||
        usb_speed_is_enhanced(parent->link.speed) ||
        (parent->link.speed != USB_SPEED_HIGH && speed == USB_SPEED_HIGH)) ||
      (!topology && controller->descendants_used == controller->descendant_capacity)) {
    return USB_UNSUPPORTED;
  }
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *old = device_at(controller, i);
    if (old->parent == parent && old->downstream_port == port && old->slot && !old->removed) {
      return USB_UNSUPPORTED;
    }
  }
  struct xhci_port *root = &controller->ports[parent->port];
  unsigned raw_speed = child_speed_id(root, link);
  if (!raw_speed || root->major != (enhanced ? XHCI_PROTOCOL_USB_3 : XHCI_PROTOCOL_USB_2)) {
    return USB_UNSUPPORTED;
  }
  if (enhanced) {
    for (const struct usb_host_device *hub = parent; hub; hub = hub->parent) {
      if (!link_rank(&hub->link)) {
        return USB_UNSUPPORTED;
      }
    }
  }
  if (task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  struct usb_host_device *device = topology ? reserve_topology_device(controller) :
    &controller->descendants[controller->descendants_used++];
  if (!device) {
    return USB_BUSY;
  }
  device->parent = parent;
  device->port = parent->port;
  device->downstream_port = port;
  device->depth = parent->depth + 1;
  unsigned route_port = port < XHCI_SLOT_ROUTE_PORT_MAX ? port : XHCI_SLOT_ROUTE_PORT_MAX;
  device->route = parent->route | (route_port << (parent->depth * XHCI_SLOT_ROUTE_PORT_BITS));
  device->link = *link;
  device->raw_speed = raw_speed;
  if (speed == USB_SPEED_LOW || speed == USB_SPEED_FULL) {
    if (parent->link.speed == USB_SPEED_HIGH) {
      device->parent_slot = parent->slot;
      device->parent_port = port;
      device->multi_tt = parent->multi_tt;
    } else {
      device->parent_slot = parent->parent_slot;
      device->parent_port = parent->parent_port;
      device->multi_tt = parent->multi_tt;
    }
  }
  if (enhanced) {
    const struct usb_host_device *below = device;
    for (const struct usb_host_device *hub = parent; hub; hub = hub->parent) {
      if (link_rank(&hub->link) > link_rank(link)) {
        device->parent_slot = hub->slot;
        device->parent_port = below->downstream_port;
        break;
      }
      below = hub;
    }
  }
  uint64_t generation = root->generation;
  enum usb_result result = enable_admitted_slot(device, deadline);
  if (result == USB_OK && topology) {
    result = validate_topology_attachment(device, generation);
  }
  if (result != USB_OK) {
    return result;
  }
  *child = device;
  return USB_OK;
}

enum usb_result usb_host_attach_child(struct usb_host_device *parent, unsigned port,
    const struct usb_link *link, uint64_t deadline, struct usb_host_device **child)
{
  return attach_child(parent, port, link, false, deadline, child);
}

enum usb_result usb_host_topology_attach_child(struct usb_host_device *parent, unsigned port,
    const struct usb_link *link, uint64_t deadline, struct usb_host_device **child)
{
  return attach_child(parent, port, link, true, deadline, child);
}

enum usb_result usb_host_update_packet(struct usb_host_device *device, uint16_t packet, uint64_t deadline)
{
  assert_device_owner(device);
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  if (device->request.state != CONTROL_IDLE) {
    return USB_BUSY;
  }
  if (device->link.speed != USB_SPEED_FULL || (packet != 8 && packet != 16 && packet != 32 && packet != 64)) {
    return USB_INVALID;
  }
  if (packet == device->packet) {
    return USB_OK;
  }
  memset((void *)device->input.address, 0, device->input.bytes);
  ((uint32_t *)device->input.address)[1] = 1u << XHCI_ENDPOINT_ZERO;
  input_endpoint(device, XHCI_ENDPOINT_ZERO)[1] = (unsigned)packet << XHCI_ENDPOINT_PACKET_SHIFT;
  enum usb_result result = context_command(device, XHCI_TRB_EVALUATE_CONTEXT, deadline);
  if (result == USB_OK) {
    device->packet = packet;
  }
  return result;
}

bool usb_host_device_present(const struct usb_host_device *device)
{
  return device && device->controller->running && !device->controller->failed &&
    !device->retiring && device_present(device);
}

void usb_host_identity_hold(struct usb_host_device *device)
{
  if (!device || device->removed || device->retiring) {
    return;
  }
  assert_device_owner(device);
  device->identity_hold = true;
}

bool usb_host_identity_pending(const struct usb_host_device *device)
{
  return device && device_identity_held(device);
}

uint64_t usb_host_port_generation(const struct usb_host_controller *controller, unsigned port)
{
  return port < controller->port_count ? controller->ports[port].generation : 0;
}

enum usb_result usb_host_hid_claim(struct usb_host_device *device)
{
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  if (device->hid) {
    return USB_OK;
  }
  if ((!controller->enumerating && !device->topology) || !device_ready(device) ||
      !device->addressed || device->hub_ports || device->bulk || device->async_bulk || device->interrupt[0] ||
      (device->link.speed != USB_SPEED_LOW && device->link.speed != USB_SPEED_FULL &&
       device->link.speed != USB_SPEED_HIGH)) {
    return USB_UNSUPPORTED;
  }
  if (controller->hid_used == USB_HID_DEVICE_BUDGET) {
    if (!controller->hid_exhaustion_reported) {
      controller->hid_exhaustion_reported = true;
      kwarn("xHCI %x:%x.%u: retained HID class budget exhausted (%u/%u)\n",
            controller->address.bus, controller->address.device, controller->address.function,
            controller->hid_used, (unsigned)USB_HID_DEVICE_BUDGET);
    }
    return USB_BUSY;
  }
  ++controller->hid_used;
  device->hid = true;
  return USB_OK;
}

static enum usb_result enable_admitted_slot(struct usb_host_device *device, uint64_t deadline)
{
  struct usb_host_controller *controller = device->controller;
  if (task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  controller->command.admission = true;
  bool completed = run_command(controller, XHCI_TRB_ENABLE_SLOT,
                               controller->ports[device->port].slot_type, 0, deadline);
  controller->command.admission = false;
  if (!completed) {
    stop_controller(controller);
    return USB_IO;
  }
  if (controller->command.completion != XHCI_EVENT_SUCCESS) {
    return USB_BUSY;
  }
  device->slot = controller->command.slot;
  return USB_OK;
}

enum usb_result usb_host_topology_attach_root(struct usb_host_controller *controller,
    unsigned port_index, uint64_t generation, uint64_t deadline, struct usb_host_device **output)
{
  usb_host_assert_worker(controller);
  if (!output || port_index >= controller->port_count) {
    return USB_INVALID;
  }
  *output = NULL;
  struct xhci_port *port = &controller->ports[port_index];
  uint32_t status = observe_port(controller, port_index);
  if (!controller->running || controller->failed || !port->protocol ||
      (port->major != XHCI_PROTOCOL_USB_2 && port->major != XHCI_PROTOCOL_USB_3) ||
      !(status & XHCI_PORT_CONNECTED) || (status & XHCI_PORT_OVER_CURRENT)) {
    return USB_UNSUPPORTED;
  }
  if (port->generation != generation) {
    return USB_STALE;
  }
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *old = device_at(controller, i);
    if (old->slot && !old->removed && old->port == port_index) {
      return USB_BUSY;
    }
  }
  struct usb_host_device *device = reserve_topology_device(controller);
  if (!device) {
    return USB_BUSY;
  }
  device->port = port_index;
  /* Disable Slot fences ownership, but does not reset the device's address.
   * PR is USB 2 reset or USB 3 hot reset; prove completion before addressing. */
  write32(port_register(controller, port_index), 0, (status & XHCI_PORT_NEUTRAL) | XHCI_PORT_RESET);
  status = observe_port(controller, port_index);
  while ((status & XHCI_PORT_CONNECTED) &&
         (!(status & XHCI_PORT_ENABLED) || (status & XHCI_PORT_RESET))) {
    if (task_deadline_expired(deadline)) {
      return USB_TIMEOUT;
    }
    if (!controller_healthy(controller) || !drain_events(controller)) {
      stop_controller(controller);
      return USB_IO;
    }
    wait_activity(controller, task_deadline_after_ms(USB_WORKER_POLL_MS));
    status = observe_port(controller, port_index);
  }
  if (port->major == XHCI_PROTOCOL_USB_2 && root_port_live(status)) {
    enum usb_result recovery = usb_host_delay(controller, USB_RESET_RECOVERY_MS, deadline);
    if (recovery != USB_OK) {
      return recovery;
    }
    status = observe_port(controller, port_index);
  }
  if (!(status & XHCI_PORT_CONNECTED) || (status & XHCI_PORT_OVER_CURRENT) ||
      port->generation != generation ||
      (port->major == XHCI_PROTOCOL_USB_3 &&
       ((status >> XHCI_PORT_LINK_SHIFT) & XHCI_PORT_LINK_MASK) != XHCI_PORT_LINK_U0)) {
    return USB_STALE;
  }
  port->speed = (status >> XHCI_PORT_SPEED_SHIFT) & XHCI_PORT_SPEED_MASK;
  device->link = port->speeds[port->speed].link;
  device->raw_speed = port->speed;
  if (device->link.speed == USB_SPEED_UNKNOWN) {
    return USB_UNSUPPORTED;
  }
  if (usb_speed_is_enhanced(device->link.speed)) {
    uint32_t lanes = read32(port_register(controller, port_index), XHCI_PORT_LINK_INFO);
    device->link.rx_lanes = ((lanes >> XHCI_PORT_RX_LANES_SHIFT) & XHCI_PORT_LANES_MASK) + 1;
    device->link.tx_lanes = ((lanes >> XHCI_PORT_TX_LANES_SHIFT) & XHCI_PORT_LANES_MASK) + 1;
  }
  enum usb_result result = enable_admitted_slot(device, deadline);
  if (result == USB_OK) {
    result = validate_topology_attachment(device, generation);
  }
  if (result == USB_OK) {
    port->slot = device->slot;
    port->active = device;
    port->state = PORT_RESERVED;
    *output = device;
  }
  return result;
}

static const uint32_t *output_endpoint(const struct usb_host_device *device, unsigned dci)
{
  return (const uint32_t *)(device->output.address + dci * device->controller->context_bytes);
}

static bool fence_endpoint(struct usb_host_device *device, unsigned dci, uint64_t deadline)
{
  struct usb_host_controller *controller = device->controller;
  dma_read_barrier();
  const uint32_t *context = output_endpoint(device, dci);
  unsigned state = context[0] & XHCI_ENDPOINT_STATE_MASK;
  if (state == XHCI_ENDPOINT_STATE_HALTED || state == XHCI_ENDPOINT_STATE_ERROR ||
      state == XHCI_ENDPOINT_STATE_STOPPED) {
    return true;
  }
  if (state != XHCI_ENDPOINT_STATE_RUNNING) {
    controller->failure = "endpoint retirement found invalid state";
    return false;
  }
  device->stopping_dci = dci;
  device->stopped_trb = 0;
  if (!endpoint_command(device, XHCI_TRB_STOP_ENDPOINT, dci, 0, deadline)) {
    controller->failure = "Stop Endpoint fence failed";
    return false;
  }
  dma_read_barrier();
  state = context[0] & XHCI_ENDPOINT_STATE_MASK;
  bool success = controller->command.completion == XHCI_EVENT_SUCCESS && state == XHCI_ENDPOINT_STATE_STOPPED;
  bool halted_race = controller->command.completion == XHCI_EVENT_CONTEXT_STATE &&
    (state == XHCI_ENDPOINT_STATE_HALTED || state == XHCI_ENDPOINT_STATE_ERROR);
  phys_addr_t dequeue = ((uint64_t)context[3] << 32) | (context[2] & ~0xfu);
  if ((!success && !halted_race) || (device->stopped_trb && device->stopped_trb != dequeue)) {
    controller->failure = "Stop Endpoint completion did not prove retirement";
    return false;
  }
  device->stopping_dci = 0;
  return true;
}

static bool retire_device(struct usb_host_device *device, uint64_t deadline)
{
  struct usb_host_controller *controller = device->controller;
  if (!device->slot) {
    device->removed = true;
    return true;
  }
  for (unsigned dci = XHCI_ENDPOINT_ZERO; dci <= XHCI_EVENT_ENDPOINT_MASK; ++dci) {
    if (endpoint_ring(device, dci) && !fence_endpoint(device, dci, deadline)) {
      return false;
    }
  }
  if (!run_command(controller, XHCI_TRB_DISABLE_SLOT, device->slot, 0, deadline) ||
      controller->command.completion != XHCI_EVENT_SUCCESS) {
    controller->failure = "Disable Slot fence failed";
    return false;
  }
  /* Command waits publish ERDP before returning. Numeric slot ownership ends
   * here; old contexts, rings, buffers and TD identities remain retained. */
  device->removed = true;
  for (unsigned i = 0; i < USB_HID_ENDPOINTS_PER_DEVICE; ++i) {
    struct usb_host_interrupt *interrupt = device->interrupt[i];
    if (interrupt) {
      for (unsigned j = 0; j < USB_INTERRUPT_RECEIVES; ++j) {
        if (interrupt->receive[j].state == INTERRUPT_POSTED) {
          interrupt->receive[j].state = INTERRUPT_HELD;
        }
      }
    }
  }
  if (device->async_bulk) {
    struct xhci_async_bulk *bulk = device->async_bulk;
    for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
      if (bulk->receive[j].state == ASYNC_BULK_POSTED) {
        bulk->receive[j].state = ASYNC_BULK_HELD;
      }
    }
  }
  if (!device->parent && controller->ports[device->port].slot == device->slot) {
    controller->ports[device->port].slot = 0;
    controller->ports[device->port].state = PORT_REMOVED;
  }
  kdebug("xHCI %x:%x.%u: device slot %u fenced; backing retained until reboot\n",
         controller->address.bus, controller->address.device, controller->address.function, device->slot);
  return true;
}

enum usb_result usb_host_device_retire(struct usb_host_device *device, uint64_t deadline)
{
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  if (device->removed) {
    return USB_OK;
  }
  if (!controller->running || controller->failed) {
    return USB_IO;
  }
  mark_subtree_retiring(device);
  /* Pool order is not topology order once fresh topology contexts exist. */
  for (unsigned depth = XHCI_SLOT_ROUTE_DEPTH + 1; depth; --depth) {
    for (unsigned i = 0; i < device_capacity(controller); ++i) {
      struct usb_host_device *child = device_at(controller, i);
      if (child->depth == depth - 1 && !child->removed && child->retiring &&
          in_subtree(child, device) && !retire_device(child, deadline)) {
        stop_controller(controller);
        return USB_IO;
      }
    }
  }
  return USB_OK;
}

static bool retire_pending_devices(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *device = device_at(controller, i);
    if (device->retiring && !device->removed &&
        usb_host_device_retire(device, task_deadline_after_ms(USB_COMMAND_TIMEOUT_MS)) != USB_OK) {
      return false;
    }
  }
  return true;
}

void usb_host_interrupt_ack_loss(struct usb_host_interrupt *stream)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && stream && stream->device);
  KASSERT(kernel_task_is_current(controller_worker, stream->device->controller));
  stream->loss_acknowledged = true;
  cpu_restore_interrupts(flags);
}

bool usb_host_hid_input_complete(const struct usb_host_controller *controller)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!controller->running || controller->failed) {
    return true;
  }
  volatile const struct xhci_trb *ring = (const volatile struct xhci_trb *)controller->event_ring.address;
  if ((bool)(ring[controller->event_dequeue].control & XHCI_TRB_CYCLE) == controller->event_cycle) {
    return false;
  }
  for (unsigned i = 0; i < controller->port_count; ++i) {
    if (controller->ports[i].dirty) {
      return false;
    }
  }
  for (unsigned i = 0; i < controller->interrupt_count; ++i) {
    const struct usb_host_interrupt *stream = &controller->interrupt_streams[i];
    if (!stream->started || stream->kind != USB_INTERRUPT_HID || stream->loss_acknowledged) {
      continue;
    }
    if (stream->queue_count || stream->result != USB_OK || stream->device->retiring) {
      return false;
    }
  }
  return true;
}

static bool ticket_owned(const struct usb_host_device *device, struct usb_ticket ticket)
{
  return ticket.generation && device->request.client && device->request.generation == ticket.generation;
}

static void publish_control(struct usb_host_device *device, const struct xhci_trb *stages, unsigned count)
{
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)device->control_ring.address;
  unsigned first = device->enqueue;
  bool first_cycle = device->cycle;
  phys_addr_t positions[3];
  for (unsigned i = 0; i < count; ++i) {
    unsigned index = device->enqueue;
    bool cycle = device->cycle;
    positions[i] = device->control_ring.physical + index * XHCI_TRB_BYTES;
    ring[index].parameter = stages[i].parameter;
    ring[index].status = stages[i].status;
    /* The first stage stays invisible until every later stage is ready. */
    ring[index].control = stages[i].control | ((i ? cycle : !cycle) ? XHCI_TRB_CYCLE : 0);
    if (++device->enqueue == XHCI_RING_TRBS - 1) {
      ring[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
        XHCI_TRB_TOGGLE_CYCLE | (cycle ? XHCI_TRB_CYCLE : 0);
      device->enqueue = 0;
      device->cycle = !cycle;
    }
  }
  device->request.setup = positions[0];
  device->request.data = count == 3 ? positions[1] : 0;
  device->request.status = positions[count - 1];
  dma_write_barrier();
  ring[first].control = stages[0].control | (first_cycle ? XHCI_TRB_CYCLE : 0);
  dma_write_barrier();
}

enum usb_result usb_host_control_submit(struct usb_host_device *device, const struct usb_setup *setup,
                                        const void *outbound, uint64_t deadline, struct usb_ticket *ticket)
{
  struct usb_host_controller *controller = device->controller;
  assert_device_owner(device);
  if (device_identity_held(device)) {
    return USB_BUSY;
  }
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  if (device->request.state != CONTROL_IDLE || device->request.client) {
    return USB_BUSY;
  }
  if (!setup || !ticket || setup->length > usb_host_control_capacity() ||
      (setup->length && !(setup->request_type & 0x80) && !outbound) ||
      device->request.generation == UINT64_MAX) {
    return USB_INVALID;
  }
  if (task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  bool inbound = (setup->request_type & 0x80) != 0;
  uint64_t generation = device->request.generation + 1;
  device->request = (typeof(device->request)){
    .state = CONTROL_ACTIVE, .generation = generation, .deadline = deadline,
    .requested = setup->length, .actual = setup->length, .result = USB_BUSY,
    .client = true, .inbound = inbound, .setup_packet = *setup,
  };
  if (setup->length && !inbound) {
    memcpy((void *)device->data_address, outbound, setup->length);
  }
  uint64_t immediate = setup->request_type | ((uint64_t)setup->request << 8) |
    ((uint64_t)setup->value << 16) | ((uint64_t)setup->index << 32) | ((uint64_t)setup->length << 48);
  unsigned transfer = !setup->length ? XHCI_SETUP_NO_DATA : inbound ? XHCI_SETUP_IN : XHCI_SETUP_OUT;
  struct xhci_trb stages[3] = {{
    .parameter = immediate, .status = XHCI_CONTROL_AVERAGE_TRB,
    .control = (XHCI_TRB_SETUP << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IMMEDIATE |
      (transfer << XHCI_TRB_SETUP_TRANSFER_SHIFT),
  }};
  unsigned count = 1;
  if (setup->length) {
    stages[count++] = (struct xhci_trb){
      .parameter = device->data_physical, .status = setup->length,
      .control = (XHCI_TRB_DATA << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_SHORT_INTERRUPT |
        (inbound ? XHCI_TRB_DIRECTION_IN : 0),
    };
  }
  stages[count++] = (struct xhci_trb){
    .control = (XHCI_TRB_STATUS << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_COMPLETION_INTERRUPT |
      ((!setup->length || !inbound) ? XHCI_TRB_DIRECTION_IN : 0),
  };
  publish_control(device, stages, count);
  *ticket = (struct usb_ticket){.generation = generation};
  write32(controller->registers.address, controller->doorbells + device->slot * XHCI_DOORBELL_BYTES,
          XHCI_ENDPOINT_ZERO);
  return USB_OK;
}

static bool control_deadlines(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *device = device_at(controller, i);
    if (device->slot && !device->retiring && !device->removed && !device_present(device)) {
      mark_disconnected(device);
    }
    if (!device->retiring && device->request.state == CONTROL_ACTIVE &&
        task_deadline_expired(device->request.deadline)) {
      device->request.state = CONTROL_HELD;
      device->request.result = USB_TIMEOUT;
      mark_subtree_retiring(device);
    }
  }
  return async_bulk_deadlines(controller);
}

enum usb_result usb_host_delay(struct usb_host_controller *controller, unsigned milliseconds,
                                uint64_t deadline)
{
  usb_host_assert_worker(controller);
  uint64_t end = task_deadline_after_ms(milliseconds);
  if (end > deadline) {
    return USB_TIMEOUT;
  }
  for (;;) {
    if (!controller_healthy(controller) || !drain_events(controller) || !control_deadlines(controller)) {
      stop_controller(controller);
      return USB_IO;
    }
    usb_network_drain_progress(controller->discovery);
    if (!usb_host_running(controller)) {
      return USB_IO;
    }
    if (task_deadline_expired(deadline)) {
      return USB_TIMEOUT;
    }
    if (task_deadline_expired(end)) {
      return USB_OK;
    }
    uint64_t wake = task_deadline_after_ms(USB_WORKER_POLL_MS);
    wait_activity(controller, wake < end ? wake : end);
  }
}

enum usb_result usb_host_control_poll(struct usb_host_device *device, struct usb_ticket ticket,
                                      uint64_t deadline)
{
  assert_device_owner(device);
  if (!ticket_owned(device, ticket)) {
    return USB_STALE;
  }
  if (device->request.state == CONTROL_HALTED && recover_control_stall(device, deadline) != USB_OK) {
    stop_controller(device->controller);
  }
  return device->request.state == CONTROL_ACTIVE ? USB_BUSY : USB_OK;
}

enum usb_result usb_host_control_wait(struct usb_host_device *device, struct usb_ticket ticket, uint64_t deadline)
{
  struct usb_host_controller *controller = device->controller;
  assert_device_owner(device);
  for (;;) {
    if (!ticket_owned(device, ticket)) {
      return USB_STALE;
    }
    if (device->request.state == CONTROL_HALTED) {
      if (recover_control_stall(device, deadline) != USB_OK) {
        stop_controller(controller);
      }
    }
    if (device->request.state == CONTROL_DONE || device->request.state == CONTROL_HELD) {
      return USB_OK;
    }
    if (!controller_healthy(controller) || !drain_events(controller) || !control_deadlines(controller)) {
      stop_controller(controller);
      return USB_OK; /* The retained ticket now carries the failure. */
    }
    if (device->request.state != CONTROL_ACTIVE) {
      continue;
    }
    if (task_deadline_expired(deadline)) {
      return USB_TIMEOUT;
    }
    uint64_t poll = task_deadline_after_ms(USB_WORKER_POLL_MS);
    if (poll > deadline) {
      poll = deadline;
    }
    if (poll > device->request.deadline) {
      poll = device->request.deadline;
    }
    wait_activity(controller, poll);
  }
}

enum usb_result usb_host_control_take(struct usb_host_device *device, struct usb_ticket ticket,
                                      void *destination, size_t capacity, struct usb_completion *completion)
{
  assert_device_owner(device);
  if (!ticket_owned(device, ticket)) {
    return USB_STALE;
  }
  if (device_identity_held(device)) {
    return USB_BUSY;
  }
  if (device->request.state == CONTROL_ACTIVE) {
    return USB_BUSY;
  }
  if (!completion || (device->request.result == USB_OK && device->request.inbound &&
      device->request.actual && (!destination || capacity < device->request.actual))) {
    return USB_INVALID;
  }
  if (device->request.result == USB_OK && device->request.inbound && device->request.actual) {
    dma_read_barrier();
    memcpy(destination, (const void *)device->data_address, device->request.actual);
  }
  if (device->request.result == USB_OK &&
      device->request.setup_packet.request_type == USB_REQUEST_DEVICE_IN &&
      device->request.setup_packet.request == USB_REQUEST_GET_DESCRIPTOR &&
      device->request.setup_packet.value == (USB_DESCRIPTOR_DEVICE << 8) &&
      device->request.actual == USB_DEVICE_DESCRIPTOR_BYTES && !device->identity_descriptor_valid) {
    memcpy(device->identity_descriptor, (const void *)device->data_address, USB_DEVICE_DESCRIPTOR_BYTES);
    device->identity_descriptor_valid = true;
  }
  if (device->request.result == USB_OK &&
      device->request.setup_packet.request_type == USB_REQUEST_DEVICE_OUT &&
      device->request.setup_packet.request == USB_REQUEST_SET_CONFIGURATION &&
      !device->request.setup_packet.length && device->request.setup_packet.value <= UINT8_MAX) {
    device->identity_configuration = device->request.setup_packet.value;
    device->identity_configuration_valid = true;
  }
  *completion = (struct usb_completion){
    .result = device->request.result,
    .bytes = device->request.result == USB_OK ? device->request.actual : 0,
  };
  device->request.client = false;
  if (device->request.state == CONTROL_DONE) {
    device->request.state = CONTROL_IDLE;
  }
  return USB_OK;
}

void usb_host_control_abandon(struct usb_host_device *device, struct usb_ticket ticket)
{
  assert_device_owner(device);
  if (ticket_owned(device, ticket)) {
    device->request.client = false;
    if (device->request.state == CONTROL_DONE) {
      device->request.state = CONTROL_IDLE;
    }
  }
}

static bool identity_read(struct usb_host_device *device, struct usb_setup setup,
                          void *bytes, uint64_t deadline)
{
  struct usb_ticket ticket;
  if (usb_host_control_submit(device, &setup, NULL, deadline, &ticket) != USB_OK) {
    return false;
  }
  if (usb_host_control_wait(device, ticket, deadline) != USB_OK) {
    usb_host_control_abandon(device, ticket);
    return false;
  }
  struct usb_completion completion;
  if (usb_host_control_take(device, ticket, bytes, setup.length, &completion) != USB_OK) {
    usb_host_control_abandon(device, ticket);
    return false;
  }
  return completion.result == USB_OK && completion.bytes == setup.length;
}

bool usb_host_identity_verify(struct usb_host_device *device, uint64_t deadline)
{
  if (!device) {
    return false;
  }
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  uint64_t limit = task_deadline_after_ms(USB_COMMAND_TIMEOUT_MS);
  if (deadline > limit) {
    deadline = limit;
  }
  if (controller->identity_probe || !device->identity_descriptor_valid ||
      !device->identity_configuration_valid || !device->identity_configuration ||
      device->request.state != CONTROL_IDLE || device->request.client ||
      !device->addressed || !device_present(device)) {
    return false;
  }
  uint8_t description[USB_DEVICE_DESCRIPTOR_BYTES], configuration;
  controller->identity_probe = device;
  bool matched = identity_read(device, (struct usb_setup){
      .request_type = USB_REQUEST_DEVICE_IN, .request = USB_REQUEST_GET_DESCRIPTOR,
      .value = USB_DESCRIPTOR_DEVICE << 8, .length = sizeof(description),
    }, description, deadline) &&
    !memcmp(description, device->identity_descriptor, sizeof(description)) &&
    identity_read(device, (struct usb_setup){
      .request_type = USB_REQUEST_DEVICE_IN, .request = USB_REQUEST_GET_CONFIGURATION,
      .length = sizeof(configuration),
    }, &configuration, deadline) && configuration == device->identity_configuration;
  controller->identity_probe = NULL;
  if (matched) {
    device->identity_hold = false;
  }
  return matched;
}

static enum usb_result host_control(struct usb_host_device *device, const struct usb_setup *setup,
                                    uint64_t deadline)
{
  struct usb_ticket ticket;
  enum usb_result result = usb_host_control_submit(device, setup, NULL, deadline, &ticket);
  if (result != USB_OK) {
    return result;
  }
  result = usb_host_control_wait(device, ticket, deadline);
  if (result != USB_OK) {
    usb_host_control_abandon(device, ticket);
    return result;
  }
  struct usb_completion completion;
  result = usb_host_control_take(device, ticket, NULL, 0, &completion);
  if (result != USB_OK) {
    usb_host_control_abandon(device, ticket);
    return result;
  }
  return completion.result;
}

static bool clear_tt(struct usb_host_device *device, uint8_t endpoint, bool control, uint64_t deadline)
{
  if ((device->link.speed != USB_SPEED_LOW && device->link.speed != USB_SPEED_FULL) ||
      !device->parent_slot) {
    return true;
  }
  struct usb_host_controller *controller = device->controller;
  struct usb_host_device *hub = NULL;
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *candidate = device_at(controller, i);
    if (candidate->slot == device->parent_slot && !candidate->removed) {
      hub = candidate;
      break;
    }
  }
  dma_read_barrier();
  const uint32_t *slot = (const uint32_t *)device->output.address;
  unsigned address = slot[3] & USB_ADDRESS_MASK;
  if (!hub || hub->link.speed != USB_SPEED_HIGH || !hub->hub_ports || !device_present(hub) ||
      !address || address > USB_ADDRESS_MAX || hub->request.state != CONTROL_IDLE || hub->request.client) {
    controller->failure = "cannot identify idle owning transaction translator";
    return false;
  }
  uint16_t value = (endpoint & USB_ENDPOINT_NUMBER) | (address << USB_TT_ADDRESS_SHIFT) |
    (control ? 0 : USB_TT_BULK_TYPE << USB_TT_TYPE_SHIFT);
  unsigned count = control ? 2 : 1;
  for (unsigned direction = 0; direction < count; ++direction) {
    struct usb_setup setup = {
      .request_type = USB_REQUEST_TT_OUT,
      .request = USB_REQUEST_CLEAR_TT_BUFFER,
      .value = value | ((control ? direction : !!(endpoint & USB_ENDPOINT_DIRECTION_IN)) ?
                       USB_TT_DIRECTION_IN : 0),
      .index = hub->multi_tt ? device->parent_port : 1,
    };
    if (host_control(hub, &setup, deadline) != USB_OK) {
      controller->failure = "transaction translator cleanup failed";
      return false;
    }
  }
  return true;
}

static enum usb_result recover_control_stall(struct usb_host_device *device, uint64_t deadline)
{
  struct usb_host_controller *controller = device->controller;
  if (!device_ready(device) ||
      !endpoint_command(device, XHCI_TRB_RESET_ENDPOINT, XHCI_ENDPOINT_ZERO, 0, deadline) ||
      !clear_tt(device, 0, true, deadline) ||
      !endpoint_command(device, XHCI_TRB_SET_DEQUEUE, XHCI_ENDPOINT_ZERO,
                         (device->control_ring.physical + device->enqueue * XHCI_TRB_BYTES) |
                         (device->cycle ? XHCI_TRB_CYCLE : 0), deadline)) {
    controller->failure = "control stall retirement failed";
    return USB_IO;
  }
  /* The old data/status stages are skipped; the next publication is a SETUP.
   * Recovery runs only after its stall event has left the event ring. */
  device->request.state = device->request.client ? CONTROL_DONE : CONTROL_IDLE;
  return USB_OK;
}

size_t usb_host_interrupt_capacity(void)
{
  return USB_INTERRUPT_BYTES;
}

size_t usb_host_hid_interrupt_capacity(void)
{
  return USB_HID_INTERRUPT_BYTES;
}

static struct usb_host_interrupt *device_interrupt(const struct usb_host_device *device, unsigned dci)
{
  for (unsigned i = 0; i < USB_HID_ENDPOINTS_PER_DEVICE; ++i) {
    if (device->interrupt[i] && device->interrupt[i]->dci == dci) {
      return device->interrupt[i];
    }
  }
  return NULL;
}

enum usb_result usb_host_configure_interrupt_in(struct usb_host_device *device,
                                                const struct usb_interrupt_endpoint *endpoint,
                                                size_t receive_bytes, enum usb_interrupt_kind kind,
                                                uint64_t deadline, struct usb_host_interrupt **stream)
{
  assert_device_owner(device);
  if (!stream) {
    return USB_INVALID;
  }
  *stream = NULL;
  struct usb_host_controller *controller = device->controller;
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  enum usb_speed speed = device->link.speed;
  bool enhanced = usb_speed_is_enhanced(speed);
  if ((kind == USB_INTERRUPT_HCI && (device->parent || speed != USB_SPEED_FULL)) ||
      (kind == USB_INTERRUPT_HID && !device->hid) ||
      (kind == USB_INTERRUPT_HUB && ((!controller->enumerating && !device->topology) || !device->hub_ports)) ||
      (kind == USB_INTERRUPT_NETWORK && speed != USB_SPEED_HIGH && !enhanced) ||
      (enhanced ? kind != USB_INTERRUPT_NETWORK && kind != USB_INTERRUPT_HUB :
        speed != USB_SPEED_LOW && speed != USB_SPEED_FULL && speed != USB_SPEED_HIGH)) {
    return USB_UNSUPPORTED;
  }
  unsigned packet_max = speed == USB_SPEED_LOW ? 8 : speed == USB_SPEED_FULL ? 64 : 1024;
  size_t receive_capacity = kind == USB_INTERRUPT_HID ? USB_HID_INTERRUPT_BYTES :
    kind == USB_INTERRUPT_NETWORK ? USB_NETWORK_INTERRUPT_BYTES : USB_INTERRUPT_BYTES;
  if (!endpoint || !(endpoint->address & USB_ENDPOINT_DIRECTION_IN) ||
      !(endpoint->address & USB_ENDPOINT_NUMBER) || (endpoint->address & USB_ENDPOINT_RESERVED) ||
      !endpoint->interval || !endpoint->packet || endpoint->packet > packet_max ||
      (enhanced ? endpoint->interval > 16 || endpoint->transactions || endpoint->burst > 15 ||
        !endpoint->bytes_per_interval ||
        endpoint->bytes_per_interval > endpoint->packet * (endpoint->burst + 1) :
        endpoint->burst || endpoint->bytes_per_interval ||
        (speed == USB_SPEED_HIGH ? endpoint->interval > 16 || endpoint->transactions > 2 :
                                  endpoint->transactions != 0)) ||
      !receive_bytes || receive_bytes > receive_capacity || kind > USB_INTERRUPT_NETWORK) {
    return USB_INVALID;
  }
  if ((!controller->enumerating && kind != USB_INTERRUPT_HID && !device->topology) ||
      device->request.state != CONTROL_IDLE || device->request.client) {
    return USB_BUSY;
  }
  unsigned dci = ((endpoint->address & USB_ENDPOINT_NUMBER) << 1) | 1;
  if (device_interrupt(device, dci) ||
      (device->bulk && (device->bulk->in.dci == dci || device->bulk->out.dci == dci)) ||
      (device->async_bulk && (device->async_bulk->in.dci == dci || device->async_bulk->out.dci == dci))) {
    return USB_BUSY;
  }
  unsigned device_index = 0;
  while (device_index < USB_HID_ENDPOINTS_PER_DEVICE && device->interrupt[device_index]) {
    ++device_index;
  }
  if (device_index == USB_HID_ENDPOINTS_PER_DEVICE) {
    return USB_BUSY;
  }
  unsigned first = kind == USB_INTERRUPT_HCI ? device->port : controller->port_count;
  unsigned count = kind == USB_INTERRUPT_HCI ? 1 : USB_HID_DEVICE_BUDGET * USB_HID_ENDPOINTS_PER_DEVICE;
  if (kind == USB_INTERRUPT_HUB || kind == USB_INTERRUPT_NETWORK) {
    first += count;
    count = USB_HUB_INTERRUPT_DEVICE_BUDGET;
    if (device->topology || kind == USB_INTERRUPT_NETWORK) {
      first += count;
      count = controller->topology_capacity;
    }
    if (kind == USB_INTERRUPT_NETWORK) {
      first += count;
      count = USB_NETWORK_STREAMS;
    }
  }
  struct usb_host_interrupt *interrupt = NULL;
  for (unsigned i = first; i < first + count; ++i) {
    if (!controller->interrupt_streams[i].device) {
      interrupt = &controller->interrupt_streams[i];
      break;
    }
  }
  if (!interrupt) {
    return USB_BUSY;
  }
  /* Admission retains its prepared entry even when context/class setup fails. */
  device->interrupt[device_index] = interrupt;
  interrupt->device = device;
  interrupt->kind = kind;
  interrupt->receive_bytes = receive_bytes;
  interrupt->dci = dci;
  memset((void *)device->input.address, 0, device->input.bytes);
  uint32_t *input = (uint32_t *)device->input.address;
  input[1] = 1u | (1u << dci);
  dma_read_barrier();
  memcpy(input_slot(device), (const void *)device->output.address, controller->context_bytes);
  unsigned entries = (input_slot(device)[0] & XHCI_SLOT_ENTRIES_MASK) >> XHCI_SLOT_ENTRIES_SHIFT;
  if (entries < dci) {
    input_slot(device)[0] = (input_slot(device)[0] & ~XHCI_SLOT_ENTRIES_MASK) |
      (dci << XHCI_SLOT_ENTRIES_SHIFT);
  }
  unsigned interval = endpoint->interval - 1;
  if (speed != USB_SPEED_HIGH && !enhanced) {
    unsigned microframes = endpoint->interval * USB_MICROFRAMES_PER_FRAME;
    interval = 0;
    while (microframes > 1) {
      microframes >>= 1;
      ++interval;
    }
  }
  unsigned payload = enhanced ? endpoint->bytes_per_interval : endpoint->packet * (endpoint->transactions + 1);
  unsigned burst = enhanced ? endpoint->burst : endpoint->transactions;
  uint32_t *context = input_endpoint(device, dci);
  context[0] = interval << XHCI_ENDPOINT_INTERVAL_SHIFT;
  context[1] = ((unsigned)endpoint->packet << XHCI_ENDPOINT_PACKET_SHIFT) |
    (burst << XHCI_ENDPOINT_BURST_SHIFT) |
    (XHCI_ENDPOINT_INTERRUPT_IN << XHCI_ENDPOINT_TYPE_SHIFT) | XHCI_ENDPOINT_ERRORS;
  context[2] = (uint32_t)interrupt->ring.physical | XHCI_TRB_CYCLE;
  context[3] = interrupt->ring.physical >> 32;
  context[4] = receive_bytes | (payload << XHCI_ENDPOINT_ESIT_SHIFT);
  controller->command.admission = true;
  enum usb_result result = context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
  controller->command.admission = false;
  if (result == USB_OK) {
    interrupt->configured = true;
    *stream = interrupt;
  }
  return result;
}

static enum usb_result interrupt_result(struct usb_host_interrupt *interrupt)
{
  struct usb_host_device *device = interrupt->device;
  struct usb_host_controller *controller = device->controller;
  if (device_identity_held(device)) {
    return USB_BUSY;
  }
  bool ready = device_ready(device);
  if (!ready && !device->retiring && !device->removed && controller->running && !controller->failed) {
    mark_disconnected(device);
  }
  return interrupt->result != USB_OK ? interrupt->result : ready ? USB_OK : USB_IO;
}

enum usb_result usb_host_hid_interrupt_length(struct usb_host_interrupt *interrupt, size_t bytes)
{
  if (!interrupt || !interrupt->device) {
    return USB_INVALID;
  }
  assert_device_owner(interrupt->device);
  if (!interrupt->configured || interrupt->started || interrupt->kind != USB_INTERRUPT_HID ||
      !bytes || bytes > USB_HID_INTERRUPT_BYTES) {
    return USB_INVALID;
  }
  enum usb_result result = interrupt_result(interrupt);
  if (result != USB_OK) {
    return result;
  }
  interrupt->receive_bytes = bytes;
  return USB_OK;
}

enum usb_result usb_host_interrupt_start(struct usb_host_interrupt *interrupt)
{
  if (!interrupt || !interrupt->device) {
    return USB_INVALID;
  }
  struct usb_host_device *device = interrupt->device;
  assert_device_owner(device);
  if (!interrupt || !interrupt->configured) {
    return USB_INVALID;
  }
  enum usb_result result = interrupt_result(interrupt);
  if (result != USB_OK) {
    return result;
  }
  if (interrupt->started) {
    return USB_BUSY;
  }
  interrupt->started = true;
  if (!rearm_interrupts(device->controller)) {
    stop_controller(device->controller);
    return USB_IO;
  }
  return USB_OK;
}

enum usb_result usb_host_interrupt_wait(struct usb_host_interrupt *interrupt, uint64_t deadline)
{
  if (!interrupt || !interrupt->device) {
    return USB_INVALID;
  }
  struct usb_host_device *device = interrupt->device;
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  if (!interrupt || !interrupt->started) {
    return USB_INVALID;
  }
  for (;;) {
    enum usb_result result = interrupt_result(interrupt);
    if (result != USB_OK) {
      return result;
    }
    if (!controller_healthy(controller) || !drain_events(controller) || !control_deadlines(controller)) {
      stop_controller(controller);
    }
    if (interrupt->result != USB_OK) {
      return interrupt->result;
    }
    if (interrupt->queue_count) {
      return USB_OK;
    }
    if (task_deadline_expired(deadline)) {
      return USB_TIMEOUT;
    }
    uint64_t poll = task_deadline_after_ms(USB_WORKER_POLL_MS);
    wait_activity(controller, poll < deadline ? poll : deadline);
  }
}

enum usb_result usb_host_interrupt_take(struct usb_host_interrupt *interrupt, void *destination,
                                        size_t capacity, struct usb_interrupt_completion *completion)
{
  if (!interrupt || !interrupt->device) {
    return USB_INVALID;
  }
  struct usb_host_device *device = interrupt->device;
  assert_device_owner(device);
  if (!interrupt->started) {
    return USB_INVALID;
  }
  enum usb_result result = interrupt_result(interrupt);
  if (result != USB_OK) {
    return result;
  }
  if (!interrupt->queue_count) {
    return USB_BUSY;
  }
  struct xhci_interrupt_record *record = &interrupt->queue[interrupt->queue_head];
  if (!completion || capacity < record->completion.bytes || (record->completion.bytes && !destination)) {
    return USB_INVALID;
  }
  if (record->completion.bytes) {
    memcpy(destination, record->bytes, record->completion.bytes);
  }
  *completion = record->completion;
  interrupt->queue_head = (interrupt->queue_head + 1) % USB_INTERRUPT_COMPLETIONS;
  --interrupt->queue_count;
  return USB_OK;
}

void usb_host_interrupt_diagnose_loss(const struct usb_host_interrupt *stream, unsigned interface)
{
  if ((!boot_options_get()->usb_xhci_diagnose && !boot_options_get()->usb_hid_describe) ||
      !stream->diagnostic_event.control) {
    return;
  }
  const struct xhci_trb *event = &stream->diagnostic_event;
  const struct pci_address *address = &stream->device->controller->address;
  kinfo("usb xhci diag %x:%x.%u: lost stream port=%u slot=%u interface=%u dci=%u cc=%u "
      "requested=%zu residual=%u pointer=%llx status=%x control=%x\n",
      address->bus, address->device, address->function, stream->device->port + 1,
      stream->device->slot, interface, stream->dci, event->status >> XHCI_EVENT_COMPLETION_SHIFT,
      stream->receive_bytes, event->status & XHCI_EVENT_RESIDUE_MASK,
      (unsigned long long)event->parameter, event->status, event->control);
}

static void set_bulk_endpoint(struct usb_host_device *device, struct xhci_bulk_endpoint *endpoint,
                              size_t average_bytes)
{
  uint32_t *context = input_endpoint(device, endpoint->dci);
  context[1] = ((unsigned)endpoint->descriptor.packet << XHCI_ENDPOINT_PACKET_SHIFT) |
    ((unsigned)endpoint->descriptor.burst << XHCI_ENDPOINT_BURST_SHIFT) |
    ((endpoint->descriptor.address & USB_ENDPOINT_DIRECTION_IN ? XHCI_ENDPOINT_BULK_IN :
      XHCI_ENDPOINT_BULK_OUT) << XHCI_ENDPOINT_TYPE_SHIFT) | XHCI_ENDPOINT_ERRORS;
  phys_addr_t dequeue = endpoint->ring.physical + endpoint->enqueue * XHCI_TRB_BYTES;
  context[2] = (uint32_t)dequeue | (endpoint->cycle ? XHCI_TRB_CYCLE : 0);
  context[3] = dequeue >> 32;
  context[4] = average_bytes > UINT16_MAX ? UINT16_MAX : average_bytes;
}

static bool valid_bulk_endpoint(const struct usb_host_device *device,
                                const struct usb_bulk_endpoint *endpoint, bool inbound)
{
  if (!endpoint || !(endpoint->address & USB_ENDPOINT_NUMBER) ||
      (endpoint->address & USB_ENDPOINT_RESERVED) ||
      !!(endpoint->address & USB_ENDPOINT_DIRECTION_IN) != inbound || endpoint->burst > 15) {
    return false;
  }
  if (usb_speed_is_enhanced(device->link.speed)) {
    return endpoint->packet == 1024;
  }
  if (endpoint->burst) {
    return false;
  }
  return device->link.speed == USB_SPEED_HIGH ? endpoint->packet == 512 :
    device->link.speed == USB_SPEED_FULL &&
    (endpoint->packet == 8 || endpoint->packet == 16 || endpoint->packet == 32 || endpoint->packet == 64);
}

size_t usb_host_async_bulk_capacity(void)
{
  return USB_ASYNC_BULK_BYTES;
}

static enum usb_result configure_async_bulk(struct usb_host_device *device,
                                             const struct usb_bulk_endpoint *in,
                                             const struct usb_bulk_endpoint *out,
                                             size_t receive_bytes, bool network, uint64_t deadline)
{
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  if (network ? device->link.speed != USB_SPEED_HIGH && !usb_speed_is_enhanced(device->link.speed) :
      device->parent || device->link.speed != USB_SPEED_FULL) {
    return USB_UNSUPPORTED;
  }
  if ((!controller->enumerating && !(network && device->topology)) || device->async_bulk || device->bulk || device->hub_ports ||
      device->request.state != CONTROL_IDLE || device->request.client) {
    return USB_BUSY;
  }
  if (!valid_bulk_endpoint(device, in, true) || !valid_bulk_endpoint(device, out, false)) {
    return USB_UNSUPPORTED;
  }
  if (!receive_bytes || receive_bytes > (network ? USB_NETWORK_BULK_BYTES : USB_ASYNC_BULK_BYTES)) {
    return USB_INVALID;
  }
  unsigned in_dci = ((in->address & USB_ENDPOINT_NUMBER) << 1) | 1;
  unsigned out_dci = (out->address & USB_ENDPOINT_NUMBER) << 1;
  if (device_interrupt(device, in_dci) || device_interrupt(device, out_dci)) {
    return USB_BUSY;
  }
  struct xhci_async_bulk *bulk = NULL;
  for (unsigned i = 0; i < controller->async_bulk_count; ++i) {
    if (controller->async_bulk_streams[i].network == network && !controller->async_bulk_streams[i].device) {
      bulk = &controller->async_bulk_streams[i];
      break;
    }
  }
  if (!bulk) {
    return USB_UNSUPPORTED;
  }
  /* Admission consumes its prepared entry even if class configuration fails. */
  device->async_bulk = bulk;
  bulk->device = device;
  bulk->receive_bytes = receive_bytes;
  bulk->in.descriptor = *in;
  bulk->out.descriptor = *out;
  bulk->in.dci = in_dci;
  bulk->out.dci = out_dci;
  memset((void *)device->input.address, 0, device->input.bytes);
  uint32_t *input = (uint32_t *)device->input.address;
  input[1] = 1u | (1u << in_dci) | (1u << out_dci);
  dma_read_barrier();
  memcpy(input_slot(device), (const void *)device->output.address, controller->context_bytes);
  unsigned entries = in_dci > out_dci ? in_dci : out_dci;
  unsigned previous = (input_slot(device)[0] & XHCI_SLOT_ENTRIES_MASK) >> XHCI_SLOT_ENTRIES_SHIFT;
  if (entries < previous) {
    entries = previous;
  }
  input_slot(device)[0] = (input_slot(device)[0] & ~XHCI_SLOT_ENTRIES_MASK) |
    (entries << XHCI_SLOT_ENTRIES_SHIFT);
  set_bulk_endpoint(device, &bulk->in, receive_bytes);
  set_bulk_endpoint(device, &bulk->out, bulk->capacity);
  controller->command.admission = true;
  enum usb_result result = context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
  controller->command.admission = false;
  if (result == USB_OK) {
    bulk->configured = true;
  }
  return result;
}

enum usb_result usb_host_configure_async_bulk(struct usb_host_device *device,
    const struct usb_bulk_endpoint *in, const struct usb_bulk_endpoint *out,
    size_t receive_bytes, uint64_t deadline)
{
  return configure_async_bulk(device, in, out, receive_bytes, false, deadline);
}

enum usb_result usb_host_configure_network_bulk(struct usb_host_device *device,
    const struct usb_bulk_endpoint *in, const struct usb_bulk_endpoint *out,
    size_t receive_bytes, uint64_t deadline)
{
  return configure_async_bulk(device, in, out, receive_bytes, true, deadline);
}

static enum usb_result async_bulk_result(struct usb_host_device *device)
{
  struct xhci_async_bulk *bulk = device->async_bulk;
  struct usb_host_controller *controller = device->controller;
  if (device_identity_held(device)) {
    return USB_BUSY;
  }
  bool ready = device_ready(device);
  if (controller->running && !controller->failed) {
    if (!async_bulk_deadlines(controller)) {
      stop_controller(controller);
    } else if (!ready && !device->retiring && !device->removed) {
      mark_disconnected(device);
    }
  }
  return bulk->result != USB_OK ? bulk->result : ready ? USB_OK : USB_IO;
}

enum usb_result usb_host_async_bulk_start(struct usb_host_device *device)
{
  assert_device_owner(device);
  struct xhci_async_bulk *bulk = device->async_bulk;
  if (!bulk || !bulk->configured) {
    return USB_INVALID;
  }
  enum usb_result result = async_bulk_result(device);
  if (result != USB_OK) {
    return result;
  }
  if (bulk->started) {
    return USB_BUSY;
  }
  bulk->started = true;
  if (!rearm_async_bulk(device->controller)) {
    stop_controller(device->controller);
    return USB_IO;
  }
  return USB_OK;
}

enum usb_result usb_host_async_bulk_take(struct usb_host_device *device, void *destination,
                                        size_t capacity, struct usb_interrupt_completion *completion)
{
  assert_device_owner(device);
  struct xhci_async_bulk *bulk = device->async_bulk;
  if (!bulk || !bulk->started) {
    return USB_INVALID;
  }
  enum usb_result result = async_bulk_result(device);
  if (result != USB_OK) {
    return result;
  }
  if (bulk->boot_state != ASYNC_BULK_BOOT_NONE) {
    return USB_BUSY;
  }
  if (!bulk->queue_count) {
    return USB_BUSY;
  }
  struct xhci_async_bulk_record *record = &bulk->queue[bulk->queue_head];
  if (!completion || capacity < record->completion.bytes || (record->completion.bytes && !destination)) {
    return USB_INVALID;
  }
  if (record->completion.bytes) {
    memcpy(destination, record->bytes, record->completion.bytes);
  }
  *completion = record->completion;
  bulk->queue_head = (bulk->queue_head + 1) % USB_ASYNC_BULK_COMPLETIONS;
  --bulk->queue_count;
  if (bulk->network) {
    /* Only withheld IN TDs need a new event-drain/rearm pass. */
    for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
      if (bulk->receive[j].state == ASYNC_BULK_FREE) {
        uint64_t flags = cpu_save_interrupts();
        usb_host_notify(device->controller);
        cpu_restore_interrupts(flags);
        break;
      }
    }
  }
  return USB_OK;
}

bool usb_host_async_bulk_in_ready(const struct usb_host_device *device)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  const struct xhci_async_bulk *bulk = device->async_bulk;
  return bulk && bulk->started && bulk->result == USB_OK &&
      bulk->boot_state == ASYNC_BULK_BOOT_NONE && !bulk->in.halted &&
      !device_identity_held(device) && device->controller->running && !device->controller->failed;
}

static bool recover_boot_bulk(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < controller->async_bulk_count; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    if (bulk->network || bulk->boot_state == ASYNC_BULK_BOOT_NONE) {
      continue;
    }
    struct usb_host_device *device = bulk->device;
    if (device->retiring || device->removed) {
      continue;
    }
    uint64_t deadline;
    bool resume;
    if (!device_ready(device) || bulk->result != USB_OK || bulk->queue_count ||
        !bluetooth_hci_boot_bulk_recovery(device, &deadline, &resume) ||
        task_deadline_expired(deadline)) {
      controller->failure = "firmware bulk IN retirement lost boot authority or deadline";
      return false;
    }
    if (bulk->boot_state == ASYNC_BULK_BOOT_HALTED) {
      struct xhci_bulk_endpoint *endpoint = &bulk->in;
      /* Preserve the USB toggle: a transaction error is not a device STALL.
       * Set Dequeue skips both boot TDs and invalidates their cached state. */
      if (!run_command_flags(controller, XHCI_TRB_RESET_ENDPOINT, device->slot, 0,
          (endpoint->dci << XHCI_TRB_ENDPOINT_SHIFT) | XHCI_TRB_PRESERVE_TRANSFER_STATE,
          deadline) ||
          !endpoint_command(device, XHCI_TRB_SET_DEQUEUE, endpoint->dci,
              (endpoint->ring.physical + endpoint->enqueue * XHCI_TRB_BYTES) |
              (endpoint->cycle ? XHCI_TRB_CYCLE : 0), deadline)) {
        controller->failure = "firmware bulk IN reset/dequeue retirement failed";
        return false;
      }
      for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
        bulk->receive[j].state = ASYNC_BULK_FREE;
      }
      endpoint->halted = false;
      bulk->boot_state = ASYNC_BULK_BOOT_RETIRED;
      kdebug("xHCI %x:%x.%u: firmware bulk IN reset/dequeue fence complete, slot %u dci %u\n",
          controller->address.bus, controller->address.device, controller->address.function,
          device->slot, endpoint->dci);
      if (!bluetooth_hci_boot_bulk_recovery(device, &deadline, &resume) ||
          task_deadline_expired(deadline)) {
        controller->failure = "firmware bulk IN retirement lost boot authority or deadline";
        return false;
      }
    }
    if (resume) {
      bulk->boot_state = ASYNC_BULK_BOOT_NONE;
      if (!rearm_async_bulk(controller)) {
        return false;
      }
      kdebug("xHCI %x:%x.%u: firmware bulk IN operational receives posted\n",
          controller->address.bus, controller->address.device, controller->address.function);
    }
  }
  return true;
}

enum usb_result usb_host_async_bulk_out_submit(struct usb_host_device *device, const void *bytes,
                                              size_t length, uint64_t deadline, struct usb_ticket *ticket)
{
  assert_device_owner(device);
  struct xhci_async_bulk *bulk = device->async_bulk;
  struct usb_host_controller *controller = device->controller;
  if (!bulk || !bulk->started) {
    return USB_INVALID;
  }
  enum usb_result result = async_bulk_result(device);
  if (result != USB_OK) {
    return result;
  }
  if (bulk->tx.state == CONTROL_HELD) {
    return bulk->tx.result;
  }
  if (bulk->tx.state != CONTROL_IDLE || bulk->tx.client) {
    return USB_BUSY;
  }
  if (!bytes || !length || length > bulk->capacity || !ticket || bulk->tx.generation == UINT64_MAX) {
    return USB_INVALID;
  }
  if (task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  memcpy((void *)bulk->tx.data_address, bytes, length);
  uint64_t flags = cpu_save_interrupts();
  struct xhci_bulk_endpoint *endpoint = &bulk->out;
  unsigned index = endpoint->enqueue;
  bool cycle = endpoint->cycle;
  bulk->tx.trb = endpoint->ring.physical + index * XHCI_TRB_BYTES;
  bulk->tx.requested = length;
  bulk->tx.actual = 0;
  ++bulk->tx.generation;
  bulk->tx.deadline = deadline;
  bulk->tx.result = USB_BUSY;
  bulk->tx.state = CONTROL_ACTIVE;
  bulk->tx.client = true;
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)endpoint->ring.address;
  ring[index].parameter = bulk->tx.data_physical;
  ring[index].status = length;
  if (++endpoint->enqueue == XHCI_RING_TRBS - 1) {
    ring[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
      XHCI_TRB_TOGGLE_CYCLE | (cycle ? XHCI_TRB_CYCLE : 0);
    endpoint->enqueue = 0;
    endpoint->cycle = !cycle;
  }
  dma_write_barrier();
  ring[index].control = (XHCI_TRB_NORMAL << XHCI_TRB_TYPE_SHIFT) |
    XHCI_TRB_COMPLETION_INTERRUPT | (cycle ? XHCI_TRB_CYCLE : 0);
  *ticket = (struct usb_ticket){.generation = bulk->tx.generation};
  dma_write_barrier();
  write32(controller->registers.address, controller->doorbells + device->slot * XHCI_DOORBELL_BYTES,
          endpoint->dci);
  cpu_restore_interrupts(flags);
  return USB_OK;
}

enum usb_result usb_host_async_bulk_out_take(struct usb_host_device *device, struct usb_ticket ticket,
                                            struct usb_completion *completion)
{
  assert_device_owner(device);
  struct xhci_async_bulk *bulk = device->async_bulk;
  if (!bulk || !ticket.generation || !bulk->tx.client || ticket.generation != bulk->tx.generation) {
    return USB_STALE;
  }
  if (device_identity_held(device)) {
    return USB_BUSY;
  }
  (void)async_bulk_result(device);
  if (bulk->tx.state == CONTROL_ACTIVE) {
    return USB_BUSY;
  }
  if (!completion) {
    return USB_INVALID;
  }
  *completion = (struct usb_completion){
    .result = bulk->tx.result,
    .bytes = bulk->tx.result == USB_OK ? bulk->tx.actual : 0,
  };
  bulk->tx.client = false;
  if (bulk->tx.state == CONTROL_DONE) {
    bulk->tx.state = CONTROL_IDLE;
  }
  return USB_OK;
}

enum usb_result usb_host_configure_bulk(struct usb_host_device *device,
                                        const struct usb_bulk_endpoint *in,
                                        const struct usb_bulk_endpoint *out, uint64_t deadline)
{
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  if (!controller->enumerating || device->bulk || device->async_bulk || device->hub_ports ||
      device->request.state != CONTROL_IDLE || device->request.client) {
    return USB_BUSY;
  }
  if (!valid_bulk_endpoint(device, in, true) || !valid_bulk_endpoint(device, out, false)) {
    return USB_UNSUPPORTED;
  }
  unsigned in_dci = ((in->address & USB_ENDPOINT_NUMBER) << 1) | 1;
  unsigned out_dci = (out->address & USB_ENDPOINT_NUMBER) << 1;
  if (device_interrupt(device, in_dci) || device_interrupt(device, out_dci)) {
    return USB_BUSY;
  }
  struct xhci_bulk *bulk = NULL;
  for (unsigned i = 0; i < USB_STORAGE_DEVICE_BUDGET; ++i) {
    if (!controller->bulk[i].device) {
      bulk = &controller->bulk[i];
      break;
    }
  }
  if (!bulk) {
    return USB_UNSUPPORTED;
  }
  /* Admission consumes a retained pool entry even if later media setup fails. */
  device->bulk = bulk;
  bulk->device = device;
  bulk->in.descriptor = *in;
  bulk->out.descriptor = *out;
  bulk->in.dci = in_dci;
  bulk->out.dci = out_dci;
  memset((void *)device->input.address, 0, device->input.bytes);
  uint32_t *input = (uint32_t *)device->input.address;
  input[1] = 1u | (1u << bulk->in.dci) | (1u << bulk->out.dci);
  dma_read_barrier();
  memcpy(input_slot(device), (const void *)device->output.address, controller->context_bytes);
  unsigned entries = bulk->in.dci > bulk->out.dci ? bulk->in.dci : bulk->out.dci;
  unsigned previous = (input_slot(device)[0] & XHCI_SLOT_ENTRIES_MASK) >> XHCI_SLOT_ENTRIES_SHIFT;
  if (entries < previous) {
    entries = previous;
  }
  input_slot(device)[0] = (input_slot(device)[0] & ~XHCI_SLOT_ENTRIES_MASK) |
    (entries << XHCI_SLOT_ENTRIES_SHIFT);
  set_bulk_endpoint(device, &bulk->in, USB_BULK_BYTES);
  set_bulk_endpoint(device, &bulk->out, USB_BULK_BYTES);
  enum usb_result result = context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
  if (result == USB_OK) {
    bulk->configured = true;
  }
  return result;
}

static struct xhci_bulk_endpoint *bulk_endpoint(struct xhci_bulk *bulk, uint8_t address)
{
  if (bulk->in.descriptor.address == address) {
    return &bulk->in;
  }
  return bulk->out.descriptor.address == address ? &bulk->out : NULL;
}

enum usb_result usb_host_bulk_clear(struct usb_host_device *device, uint8_t address,
                                   void *destination, size_t capacity, size_t *actual, uint64_t deadline)
{
  assert_device_owner(device);
  if (device_identity_held(device)) {
    return USB_BUSY;
  }
  struct usb_host_controller *controller = device->controller;
  struct xhci_bulk *bulk = device->bulk;
  struct xhci_bulk_endpoint *endpoint = bulk ? bulk_endpoint(bulk, address) : NULL;
  if (!device_ready(device) || !endpoint) {
    return USB_IO;
  }
  if (bulk->state == CONTROL_ACTIVE || bulk->state == CONTROL_HELD) {
    return USB_BUSY;
  }
  bool stalled = bulk->active == endpoint && bulk->state == CONTROL_HALTED;
  if (actual) {
    *actual = 0;
  }
  if (destination && (!actual || endpoint != &bulk->in || !stalled || capacity < bulk->actual)) {
    return USB_INVALID;
  }
  if (!controller_healthy(controller) || !drain_events(controller)) {
    stop_controller(controller);
    return USB_IO;
  }
  if (endpoint->halted &&
      !endpoint_command(device, XHCI_TRB_RESET_ENDPOINT, endpoint->dci, 0, deadline)) {
    enum usb_result result = controller->command.timed_out ? USB_TIMEOUT : USB_IO;
    controller->failure = "bulk stall retirement failed";
    stop_controller(controller);
    return result;
  }
  if (endpoint->halted && !clear_tt(device, address, false, deadline)) {
    enum usb_result result = task_deadline_expired(deadline) ? USB_TIMEOUT : USB_IO;
    controller->failure = "bulk transaction translator retirement failed";
    stop_controller(controller);
    return result;
  }
  struct usb_setup setup = {
    .request_type = USB_REQUEST_ENDPOINT_OUT,
    .request = USB_REQUEST_CLEAR_FEATURE,
    .value = USB_FEATURE_ENDPOINT_HALT,
    .index = address,
  };
  enum usb_result result = host_control(device, &setup, deadline);
  if (result != USB_OK) {
    controller->failure = "bulk endpoint halt clear failed";
    stop_controller(controller);
    return result;
  }
  /* A completed single-TRB TD proves Running:Idle; a reset stalled endpoint is
   * Stopped. Drop+Add resets host toggle state for either case, including the
   * non-stalled pipe during BOT reset recovery. The retained producer frontier
   * skips the failed TD and contains an unpublished cycle. */
  memset((void *)device->input.address, 0, device->input.bytes);
  uint32_t *input = (uint32_t *)device->input.address;
  input[0] = 1u << endpoint->dci;
  input[1] = 1u | (1u << endpoint->dci);
  dma_read_barrier();
  memcpy(input_slot(device), (const void *)device->output.address, controller->context_bytes);
  set_bulk_endpoint(device, endpoint, USB_BULK_BYTES);
  result = context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
  if (result == USB_OK) {
    endpoint->halted = false;
    if (stalled) {
      if (destination) {
        dma_read_barrier();
        memcpy(destination, (const void *)bulk->data_address, bulk->actual);
      }
      if (actual) {
        *actual = bulk->actual;
      }
      bulk->state = CONTROL_IDLE;
    }
  }
  return result;
}

enum usb_result usb_host_bulk_transfer(struct usb_host_device *device, uint8_t address,
                                       const void *outbound, void *destination, size_t length,
                                       uint64_t deadline, size_t *actual, bool *submitted)
{
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  struct xhci_bulk *bulk = device->bulk;
  struct xhci_bulk_endpoint *endpoint = bulk ? bulk_endpoint(bulk, address) : NULL;
  if (device_identity_held(device)) {
    return USB_BUSY;
  }
  if (!device_ready(device) || !endpoint) {
    return USB_IO;
  }
  bool inbound = endpoint == &bulk->in;
  if (!actual || !length || length > USB_BULK_BYTES || (inbound ? !destination : !outbound)) {
    return USB_INVALID;
  }
  *actual = 0;
  if (bulk->state != CONTROL_IDLE || endpoint->halted) {
    return USB_BUSY;
  }
  if (task_deadline_expired(deadline)) {
    return USB_TIMEOUT;
  }
  if (!inbound) {
    memcpy((void *)bulk->data_address, outbound, length);
  }
  uint64_t flags = cpu_save_interrupts();
  unsigned index = endpoint->enqueue;
  bool cycle = endpoint->cycle;
  bulk->active = endpoint;
  bulk->trb = endpoint->ring.physical + index * XHCI_TRB_BYTES;
  bulk->requested = length;
  bulk->actual = 0;
  bulk->result = USB_BUSY;
  bulk->state = CONTROL_ACTIVE;
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)endpoint->ring.address;
  ring[index].parameter = bulk->data_physical;
  ring[index].status = length;
  unsigned control = (XHCI_TRB_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_COMPLETION_INTERRUPT |
    (inbound ? XHCI_TRB_SHORT_INTERRUPT : 0);
  if (++endpoint->enqueue == XHCI_RING_TRBS - 1) {
    ring[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
      XHCI_TRB_TOGGLE_CYCLE | (cycle ? XHCI_TRB_CYCLE : 0);
    endpoint->enqueue = 0;
    endpoint->cycle = !cycle;
  }
  dma_write_barrier();
  ring[index].control = control | (cycle ? XHCI_TRB_CYCLE : 0);
  if (submitted) {
    *submitted = true;
  }
  dma_write_barrier();
  write32(controller->registers.address, controller->doorbells + device->slot * XHCI_DOORBELL_BYTES,
          endpoint->dci);
  cpu_restore_interrupts(flags);
  bool timed_out = false;
  while (bulk->state == CONTROL_ACTIVE) {
    if (!controller_healthy(controller) || !drain_events(controller) || !control_deadlines(controller)) {
      stop_controller(controller);
      break;
    }
    if (bulk->state != CONTROL_ACTIVE) {
      break;
    }
    timed_out = task_deadline_expired(deadline);
    if (!device_present(device)) {
      mark_disconnected(device);
      break;
    }
    if (timed_out) {
      mark_subtree_retiring(device);
      break;
    }
    uint64_t poll = task_deadline_after_ms(USB_WORKER_POLL_MS);
    wait_activity(controller, poll < deadline ? poll : deadline);
  }
  if (device_identity_held(device) && !controller->ports[device->port].identity_hold) {
    usb_hub_resolve_identity(controller->discovery, device, deadline);
  }
  if (!device_ready(device) || device_identity_held(device)) {
    return USB_IO;
  }
  enum usb_result result = timed_out ? USB_TIMEOUT : bulk->result;
  if (bulk->state == CONTROL_DONE) {
    if (inbound) {
      dma_read_barrier();
      memcpy(destination, (const void *)bulk->data_address, bulk->actual);
    }
    *actual = bulk->actual;
    bulk->state = CONTROL_IDLE;
  }
  return result;
}

static void program_rings(struct usb_host_controller *controller)
{
  /* ERSTBA may cause the halted controller to fetch its table immediately.
   * BME must already be enabled, with all DMA storage retained from this point. */
  dma_write_barrier();
  uintptr_t op = operational(controller);
  uintptr_t intr = interrupter(controller);
  write32(op, XHCI_OP_NOTIFICATION, read32(op, XHCI_OP_NOTIFICATION) & ~XHCI_NOTIFICATION_MASK);
  write32(op, XHCI_OP_CONFIG,
          (read32(op, XHCI_OP_CONFIG) & ~XHCI_CONFIG_SLOTS_MASK) | controller->slot_count);
  write64(op, XHCI_OP_DCBAA, controller->dcbaa.physical);
  write64(op, XHCI_OP_COMMAND_RING, controller->command_ring.physical | XHCI_TRB_CYCLE |
          (read64(op, XHCI_OP_COMMAND_RING) & XHCI_COMMAND_RING_PRESERVE));
  set_interrupt_enable(controller, false);
  write32(intr, XHCI_INTR_MODERATION, 0);
  write32(intr, XHCI_INTR_ERST_SIZE, (read32(intr, XHCI_INTR_ERST_SIZE) & ~XHCI_ERST_SIZE_MASK) | 1);
  write64(intr, XHCI_INTR_DEQUEUE, controller->event_ring.physical);
  write64(intr, XHCI_INTR_ERST_BASE, controller->erst.physical |
          (read64(intr, XHCI_INTR_ERST_BASE) & XHCI_ERST_BASE_PRESERVE));
}

static void controller_worker(void *argument)
{
  struct usb_host_controller *controller = argument;
  uint64_t flags = cpu_save_interrupts();
  uint16_t command = pci_read16(controller->claim.device->address, PCI_COMMAND);
  bool enabled = pci_write16(&controller->claim, PCI_COMMAND, command | PCI_COMMAND_MASTER) &&
      (pci_read16(controller->claim.device->address, PCI_COMMAND) & PCI_COMMAND_MASTER) != 0;
  if (enabled) {
    program_rings(controller);
    uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
    enabled = !(status & (XHCI_STATUS_SYSTEM_ERROR | XHCI_STATUS_CONTROLLER_ERROR | XHCI_STATUS_NOT_READY));
  }
  controller->interrupt_ready = enabled;
  if (enabled) {
    set_interrupt_enable(controller, true);
    enabled = interrupt_delivery(controller, true);
  }
  if (enabled) {
    write32(operational(controller), XHCI_OP_COMMAND,
            read32(operational(controller), XHCI_OP_COMMAND) |
            XHCI_COMMAND_RUN | XHCI_COMMAND_INTERRUPT | XHCI_COMMAND_SYSTEM_ERROR);
  }
  cpu_restore_interrupts(flags);
  if (!enabled) {
    controller->failure = "cannot initialize rings or enable DMA/interrupts";
    stop_controller(controller);
    return;
  }
  uint64_t deadline = task_deadline_after_ms(USB_STATE_TIMEOUT_MS);
  while (read32(operational(controller), XHCI_OP_STATUS) & XHCI_STATUS_HALTED) {
    if (task_deadline_expired(deadline)) {
      controller->failure = "controller run deadline expired";
      stop_controller(controller);
      return;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  controller->running = true;
  uint64_t enumeration_deadline = task_deadline_after_ms(USB_ENUMERATION_TIMEOUT_MS);
  if (!controller_healthy(controller) || !prepare_ports(controller, enumeration_deadline)) {
    stop_controller(controller);
    return;
  }
  controller->enumerating = true;
  usb_enumerate(controller->discovery, enumeration_deadline);
  controller->enumerating = false;
  if (!controller->running || controller->failed) {
    return;
  }
  controller->hotplug_ready = true;
  kdebug("xHCI %x:%x.%u: boot USB enumeration finished\n",
         controller->address.bus, controller->address.device, controller->address.function);
  for (;;) {
    if (boot_options_get()->usb_xhci_diagnose) {
      controller->diagnostic_worker_ns = arch_monotonic_ns();
      controller->diagnostic_stage = "events";
    }
    if (!controller_healthy(controller) || !drain_events(controller) ||
        !control_deadlines(controller) || !update_ports(controller) || !retire_pending_devices(controller)) {
      stop_controller(controller);
      return;
    }
    if (boot_options_get()->usb_xhci_diagnose) {
      controller->diagnostic_stage = "bluetooth";
    }
    bluetooth_hci_progress(controller);
    if (!recover_boot_bulk(controller)) {
      stop_controller(controller);
      return;
    }
    if (boot_options_get()->usb_xhci_diagnose) {
      controller->diagnostic_stage = "storage";
    }
    usb_network_process(controller->discovery);
    usb_storage_process(controller->discovery);
    if (boot_options_get()->usb_xhci_diagnose) {
      controller->diagnostic_stage = "hid";
    }
    usb_topology_process(controller->discovery);
    if (!controller->running || controller->failed) {
      return;
    }
    if (!controller_healthy(controller)) {
      stop_controller(controller);
      return;
    }
    if (boot_options_get()->usb_xhci_diagnose) {
      controller->diagnostic_stage = "wait";
    }
    uint64_t wake = task_deadline_after_ms(USB_WORKER_POLL_MS), network_deadline;
    if (usb_network_next_deadline(controller->discovery, &network_deadline) && network_deadline < wake) {
      wake = network_deadline;
    }
    wait_activity(controller, wake);
  }
}

void xhci_start(void)
{
  for (struct usb_host_controller *controller = controllers; controller; controller = controller->next) {
    if (!controller->prepared) {
      continue;
    }
    enum mm_result result = kernel_task_create(controller_worker, controller);
    if (result != MM_OK) {
      controller->failure = "cannot create controller worker";
      controller->failed = true;
      usb_inventory_controller_failed(controller->inventory_index);
      kerror("xHCI %x:%x.%u: %s (error %u); resources retained until reboot\n",
             controller->address.bus, controller->address.device, controller->address.function,
             controller->failure, (unsigned)result);
    }
  }
}
