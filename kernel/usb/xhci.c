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
#include <kernel/memory.h>
#include <kernel/mm/dma.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/pci/msix.h>
#include <kernel/pci/registers.h>
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
#define USB_TT_ADDRESS_SHIFT 4
#define USB_TT_TYPE_SHIFT 11
#define USB_TT_DIRECTION_IN 0x8000
#define USB_TT_BULK_TYPE 2
#define USB_ADDRESS_MASK 0xff
#define USB_ADDRESS_MAX 127

#define XHCI_RING_TRBS (PAGE_SIZE / XHCI_TRB_BYTES)

struct xhci_trb {
  uint64_t parameter;
  uint32_t status, control;
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
};
enum interrupt_receive_state { INTERRUPT_FREE, INTERRUPT_POSTED, INTERRUPT_HELD };
struct xhci_interrupt_receive {
  uintptr_t data_address;
  phys_addr_t data_physical, trb;
  enum interrupt_receive_state state;
};
struct xhci_interrupt_record {
  struct usb_interrupt_completion completion;
  uint8_t bytes[USB_INTERRUPT_BYTES];
};
struct xhci_interrupt {
  struct usb_host_device *device;
  struct dma_buffer ring;
  struct xhci_interrupt_receive receive[USB_INTERRUPT_RECEIVES];
  struct xhci_interrupt_record queue[USB_INTERRUPT_COMPLETIONS];
  size_t receive_bytes;
  uint64_t sequence;
  unsigned enqueue, dci, queue_head, queue_count;
  enum usb_result result;
  bool cycle, configured, started, successful_short_traced;
};
enum async_bulk_receive_state { ASYNC_BULK_FREE, ASYNC_BULK_POSTED, ASYNC_BULK_HELD };
struct xhci_async_bulk_receive {
  uintptr_t data_address;
  phys_addr_t data_physical, trb;
  enum async_bulk_receive_state state;
};
struct xhci_async_bulk_record {
  struct usb_interrupt_completion completion;
  uint8_t bytes[USB_ASYNC_BULK_BYTES];
};
enum async_bulk_boot_state { ASYNC_BULK_BOOT_NONE, ASYNC_BULK_BOOT_HALTED, ASYNC_BULK_BOOT_RETIRED };
struct xhci_async_bulk {
  struct usb_host_device *device;
  struct xhci_bulk_endpoint in, out;
  struct xhci_async_bulk_receive receive[USB_ASYNC_BULK_RECEIVES];
  struct xhci_async_bulk_record queue[USB_ASYNC_BULK_COMPLETIONS];
  size_t receive_bytes;
  uint64_t sequence;
  unsigned queue_head, queue_count;
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
  struct xhci_interrupt *interrupt;
  struct xhci_async_bulk *async_bulk;
  struct dma_buffer input, output, control_ring, data;
  uintptr_t data_address;
  phys_addr_t data_physical;
  unsigned port, slot, enqueue, downstream_port, depth, hub_ports;
  uint32_t route;
  uint8_t raw_speed, parent_slot, parent_port;
  uint16_t packet;
  struct usb_link link;
  bool cycle, addressed, multi_tt, removed;
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
  bool protocol, dirty, boot_present;
  struct usb_host_device device;
};

static void controller_worker(void *argument);
static void stop_controller(struct usb_host_controller *controller);
static enum usb_result recover_control_stall(struct usb_host_device *device, uint64_t deadline);
static bool device_present(const struct usb_host_device *device);

/* Each controller owns its records and BSP worker. IF=0 protects notification
 * and wait publication against the shared BSP-routed interrupt. The retained
 * list is complete before AP startup and never changes at runtime. */
struct usb_host_controller {
  struct usb_host_controller *next;
  struct usb_discovery *discovery;
  struct pci_address address;
  size_t inventory_index;
  struct pci_claim claim;
  struct pci_msix msix;
  struct pci_mapping bootstrap, registers;
  struct dma_buffer dcbaa, command_ring, event_ring, erst, scratchpad_array, scratchpads;
  struct dma_buffer descendant_dma, bulk_dma, interrupt_dma, async_bulk_dma;
  struct xhci_bulk bulk[USB_STORAGE_DEVICE_BUDGET];
  struct xhci_interrupt *interrupt_streams;
  struct xhci_async_bulk *async_bulk_streams;
  struct xhci_port *ports;
  struct usb_host_device *descendants;
  uint32_t operational, runtime, doorbells;
  unsigned port_count, slot_count, scratchpad_count, context_bytes;
  unsigned descendant_capacity, descendants_used;
  unsigned command_enqueue, event_dequeue;
  bool command_cycle, event_cycle, port_power;
  bool prepared, running, interrupt_ready, notified, failed, enumerating;
  struct task_wait *wait;
  struct {
    phys_addr_t physical;
    unsigned type, slot, completion;
    bool pending, timed_out;
  } command;
  uint64_t interrupts, commands_completed, events_consumed;
  const char *failure;
};

static struct usb_host_controller *controllers;

static unsigned device_capacity(const struct usb_host_controller *controller)
{
  return controller->port_count + controller->descendant_capacity;
}

static struct usb_host_device *device_at(struct usb_host_controller *controller, unsigned index)
{
  return index < controller->port_count ? &controller->ports[index].device :
    &controller->descendants[index - controller->port_count];
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
  if (controller->descendant_capacity) {
    controller->descendants = kmalloc(controller->descendant_capacity * sizeof(*controller->descendants));
    if (!controller->descendants) {
      controller->descendant_capacity = 0;
      return false;
    }
    memset(controller->descendants, 0, controller->descendant_capacity * sizeof(*controller->descendants));
  }
  ktrace("xHCI %x:%x.%u: version=%x slots=%u ports=%u context=%u scratchpads=%u\n",
         controller->address.bus, controller->address.device, controller->address.function,
         version, controller->slot_count, controller->port_count,
         controller->context_bytes, controller->scratchpad_count);
  return true;
}

static bool legacy_handoff(struct usb_host_controller *controller, unsigned offset)
{
  uintptr_t base = controller->bootstrap.address + offset;
  volatile uint8_t *os_owned = (volatile uint8_t *)(base + XHCI_LEGACY_OS_BYTE);
  /* Byte access avoids writing the independently changing BIOS semaphore. */
  *os_owned |= XHCI_LEGACY_OWNED;
  uint64_t deadline = task_deadline_after_ms(USB_STATE_TIMEOUT_MS);
  while (*(volatile uint8_t *)(base + XHCI_LEGACY_BIOS_BYTE) & XHCI_LEGACY_OWNED) {
    if (task_deadline_expired(deadline)) {
      return false;
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

static bool extended_capabilities(struct usb_host_controller *controller)
{
  uintptr_t base = controller->bootstrap.address;
  unsigned offset = (read32(base, XHCI_CAP_FEATURES) >> XHCI_EXT_CAP_SHIFT) *
    XHCI_EXT_CAP_DWORD_BYTES;
  unsigned legacy = 0;
  /* Next pointers only move forward; a body must fit before the next header. */
  while (offset) {
    if (offset < XHCI_CAP_BYTES || !bootstrap_fits(offset, sizeof(uint32_t))) {
      return false;
    }
    uint32_t header = read32(base, offset);
    unsigned id = header & XHCI_EXT_ID_MASK;
    unsigned next = ((header >> XHCI_EXT_NEXT_SHIFT) & XHCI_EXT_NEXT_MASK) *
      XHCI_EXT_CAP_DWORD_BYTES;
    unsigned bytes = sizeof(uint32_t);
    if (id == XHCI_EXT_LEGACY) {
      bytes = XHCI_LEGACY_BYTES;
      if (legacy) {
        return false;
      }
      legacy = offset;
    } else if (id == XHCI_EXT_PROTOCOL) {
      if (!bootstrap_fits(offset, XHCI_PROTOCOL_BYTES)) {
        return false;
      }
      uint32_t ports = read32(base, offset + 8);
      unsigned speeds = ports >> XHCI_PROTOCOL_SPEED_COUNT_SHIFT;
      bytes = XHCI_PROTOCOL_BYTES + speeds * sizeof(uint32_t);
      unsigned first = ports & XHCI_EXT_ID_MASK;
      unsigned count = (ports >> XHCI_PROTOCOL_PORT_COUNT_SHIFT) & XHCI_EXT_ID_MASK;
      if (read32(base, offset + 4) != XHCI_PROTOCOL_NAME || !first || !count ||
          first > controller->port_count || count > controller->port_count - first + 1) {
        return false;
      }
      unsigned slot_type = read32(base, offset + 12) & XHCI_PROTOCOL_SLOT_TYPE_MASK;
      struct xhci_speed speed_map[XHCI_PORT_SPEED_MASK + 1] = {0};
      if (!bootstrap_fits(offset, bytes) ||
          !protocol_speeds(base, offset, speeds, header >> XHCI_PROTOCOL_MAJOR_SHIFT,
                           (header >> XHCI_PROTOCOL_MINOR_SHIFT) & XHCI_EXT_ID_MASK, speed_map)) {
        return false;
      }
      for (unsigned i = first - 1; i < first - 1 + count; ++i) {
        struct xhci_port *port = &controller->ports[i];
        if (port->protocol) {
          return false;
        }
        port->protocol = true;
        port->major = header >> XHCI_PROTOCOL_MAJOR_SHIFT;
        port->minor = header >> XHCI_PROTOCOL_MINOR_SHIFT;
        port->slot_type = slot_type;
        memcpy(port->speeds, speed_map, sizeof(speed_map));
      }
    }
    if (!bootstrap_fits(offset, bytes) || (next && next < bytes)) {
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
  return !legacy || legacy_handoff(controller, legacy);
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
  if (!pci_size_bars(claim) || claim->bars[0].bytes < PCI_BOOTSTRAP_BAR_BYTES ||
      !register_region_fits(controller, 0, ports_end) ||
      !register_region_fits(controller, controller->runtime, runtime_end - controller->runtime) ||
      !register_region_fits(controller, controller->doorbells, doorbells_end - controller->doorbells) ||
      controller->runtime < ports_end || controller->doorbells < ports_end ||
      ((uint64_t)controller->runtime < doorbells_end && controller->doorbells < runtime_end) ||
      !pci_msix_discover(claim, &controller->msix)) {
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
  if (regions_overlap(0, end, &controller->msix.table) ||
      regions_overlap(0, end, &controller->msix.pba) ||
      pci_map_bar(claim, 0, 0, end, boot, &controller->registers) != MM_OK ||
      pci_msix_map(&controller->msix, boot) != MM_OK) {
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
  if (!controller->descendant_capacity) {
    return true;
  }
  size_t input_bytes =
    (XHCI_INPUT_CONTEXT_COUNT * controller->context_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t output_bytes =
    (XHCI_CONTEXT_COUNT * controller->context_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t data_bytes = (capacity + padding + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
  size_t stride = input_bytes + output_bytes + PAGE_SIZE + data_bytes;
  if (controller->descendant_capacity > SIZE_MAX / stride ||
      dma_buffer_allocate(&controller->descendant_dma, controller->descendant_capacity * stride) != MM_OK) {
    return false;
  }
  /* One owned arena avoids spending four VM range records per descendant.
   * Page-aligned slices are borrowed; only the controller releases the arena. */
  for (unsigned i = 0; i < controller->descendant_capacity; ++i) {
    struct usb_host_device *device = &controller->descendants[i];
    size_t offset = i * stride;
    device->controller = controller;
    device->input = descendant_dma_slice(&controller->descendant_dma, offset, input_bytes);
    offset += input_bytes;
    device->output = descendant_dma_slice(&controller->descendant_dma, offset, output_bytes);
    offset += output_bytes;
    device->control_ring = descendant_dma_slice(&controller->descendant_dma, offset, PAGE_SIZE);
    offset += PAGE_SIZE;
    device->data = descendant_dma_slice(&controller->descendant_dma, offset, data_bytes);
    if (!prepare_device_buffers(device, capacity, padding)) {
      return false;
    }
  }
  return true;
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
                 "a prepared async bulk buffer fits one page and one Normal TRB boundary");
  _Static_assert(USB_ASYNC_BULK_RECEIVES && USB_ASYNC_BULK_RECEIVES < XHCI_RING_TRBS,
                 "posted async bulk receives fit the usable transfer ring");
  _Static_assert(USB_ASYNC_BULK_COMPLETIONS, "async bulk completion queue is nonempty");
  size_t stride = (3 + USB_ASYNC_BULK_RECEIVES) * PAGE_SIZE;
  size_t count = USB_ASYNC_BULK_DEVICE_BUDGET;
  if (!count || count > SIZE_MAX / stride || count > SIZE_MAX / sizeof(*controller->async_bulk_streams)) {
    return false;
  }
  controller->async_bulk_streams = kmalloc(count * sizeof(*controller->async_bulk_streams));
  if (!controller->async_bulk_streams) {
    return false;
  }
  memset(controller->async_bulk_streams, 0, count * sizeof(*controller->async_bulk_streams));
  if (dma_buffer_allocate(&controller->async_bulk_dma, count * stride) != MM_OK) {
    return false;
  }
  for (unsigned i = 0; i < count; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    size_t offset = i * stride;
    bulk->in.ring = descendant_dma_slice(&controller->async_bulk_dma, offset, PAGE_SIZE);
    bulk->out.ring = descendant_dma_slice(&controller->async_bulk_dma, offset + PAGE_SIZE, PAGE_SIZE);
    if (!ring_layout(&bulk->in.ring) || !ring_layout(&bulk->out.ring)) {
      return false;
    }
    initialize_transfer_ring(&bulk->in.ring);
    initialize_transfer_ring(&bulk->out.ring);
    bulk->in.cycle = bulk->out.cycle = true;
    bulk->tx.data_address = controller->async_bulk_dma.address + offset + 2 * PAGE_SIZE;
    bulk->tx.data_physical = controller->async_bulk_dma.physical + offset + 2 * PAGE_SIZE;
    for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
      size_t data_offset = offset + (j + 3) * PAGE_SIZE;
      bulk->receive[j].data_address = controller->async_bulk_dma.address + data_offset;
      bulk->receive[j].data_physical = controller->async_bulk_dma.physical + data_offset;
    }
  }
  return true;
}

static bool allocate_interrupts(struct usb_host_controller *controller)
{
  _Static_assert(USB_INTERRUPT_BYTES && USB_INTERRUPT_BYTES <= PAGE_SIZE,
                 "a prepared interrupt buffer fits one page and one Normal TRB boundary");
  _Static_assert(USB_INTERRUPT_RECEIVES && USB_INTERRUPT_RECEIVES < XHCI_RING_TRBS,
                 "posted interrupt receives fit the usable transfer ring");
  _Static_assert(USB_INTERRUPT_COMPLETIONS, "interrupt completion queue is nonempty");
  size_t stride = (1 + USB_INTERRUPT_RECEIVES) * PAGE_SIZE;
  size_t ports = controller->port_count;
  if (ports > SIZE_MAX / stride || ports > SIZE_MAX / sizeof(*controller->interrupt_streams)) {
    return false;
  }
  controller->interrupt_streams = kmalloc(controller->port_count * sizeof(*controller->interrupt_streams));
  if (!controller->interrupt_streams) {
    return false;
  }
  memset(controller->interrupt_streams, 0, controller->port_count * sizeof(*controller->interrupt_streams));
  if (dma_buffer_allocate(&controller->interrupt_dma, controller->port_count * stride) != MM_OK) {
    return false;
  }
  for (unsigned i = 0; i < controller->port_count; ++i) {
    struct xhci_interrupt *interrupt = &controller->interrupt_streams[i];
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
  dma_buffer_release(&controller->async_bulk_dma);
  kfree(controller->async_bulk_streams);
  controller->async_bulk_streams = NULL;
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
    klog("xHCI %x:%x.%u: controller busy or unsupported PCI state\n",
         controller->address.bus, controller->address.device, controller->address.function);
    controller->failed = true;
    usb_inventory_controller_failed(controller->inventory_index);
    return;
  }
  const char *failure = "unsupported assigned BAR0 bootstrap mapping";
  if (pci_map_bootstrap_bar(&controller->claim, 0, boot, &controller->bootstrap) != MM_OK) {
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
  failure = "unsupported extended capabilities or firmware ownership timeout";
  if (!extended_capabilities(controller)) {
    goto fail;
  }
  failure = "cannot confirm controller halt/reset";
  if (!halt_and_reset(controller)) {
    /* Completion/reset failure may leave hardware ownership unresolved. */
    if (!controller->claim.reserved) {
      controller->failure = failure;
      klog("xHCI %x:%x.%u: %s; claim retained until reboot\n",
           controller->address.bus, controller->address.device, controller->address.function,
           failure);
      controller->failed = true;
      usb_inventory_controller_failed(controller->inventory_index);
      return;
    }
    goto fail;
  }
  failure = "invalid register/MSI-X resources";
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
  failure = "MSI-X setup failed";
  if (!pci_msix_prepare(&controller->msix, APIC_XHCI_VECTOR)) {
    if (!pci_msix_disable(&controller->msix)) {
      controller->failure = failure;
      klog("xHCI %x:%x.%u: %s; resources retained until reboot\n",
           controller->address.bus, controller->address.device, controller->address.function,
           failure);
      controller->failed = true;
      usb_inventory_controller_failed(controller->inventory_index);
      return;
    }
    goto fail;
  }
  controller->prepared = true;
  ktrace("xHCI %x:%x.%u: controller prepared, command/event rings=%u TRBs; DMA disabled\n",
         controller->address.bus, controller->address.device, controller->address.function,
         (unsigned)XHCI_RING_TRBS);
  return;

fail:
  controller->failure = failure;
  klog("xHCI %x:%x.%u: %s\n",
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
      klog("xHCI %x:%x.%u: cannot allocate controller record\n",
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
    /* MSI-X delivery can already have cleared IP. The shared vector notifies
     * every active controller, whose worker checks its own cycle-owned events. */
    controller->notified = true;
    struct task_wait *wait = controller->wait;
    controller->wait = NULL;
    if (wait) {
      task_wait_wake(wait);
    }
  }
}

void usb_host_notify(struct usb_host_controller *controller)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  controller->notified = true;
  struct task_wait *wait = controller->wait;
  controller->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void wait_activity(struct usb_host_controller *controller, uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  if (!controller->notified) {
    struct task_wait *wait = task_wait_prepare();
    controller->wait = wait;
    task_wait_sleep_until(wait, deadline);
    if (controller->wait == wait) {
      controller->wait = NULL;
    }
  }
  controller->notified = false;
  cpu_restore_interrupts(flags);
}

static bool consume_command(struct usb_host_controller *controller, const struct xhci_trb *event)
{
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  unsigned slot = event->control >> XHCI_TRB_SLOT_SHIFT;
  if (!controller->command.pending || event->parameter != controller->command.physical ||
      completion != XHCI_EVENT_SUCCESS || !slot || slot > controller->slot_count) {
    controller->failure = "invalid or failed command completion";
    return false;
  }
  if (controller->command.type == XHCI_TRB_ENABLE_SLOT) {
    for (unsigned i = 0; i < device_capacity(controller); ++i) {
      if (device_at(controller, i)->slot == slot) {
        controller->failure = "Enable Slot returned an owned slot";
        return false;
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

static bool publish_interrupt_receive(struct xhci_interrupt *interrupt,
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

static bool interrupt_pending(const struct xhci_interrupt *interrupt)
{
  for (unsigned i = 0; i < USB_INTERRUPT_RECEIVES; ++i) {
    if (interrupt->receive[i].state != INTERRUPT_FREE) {
      return true;
    }
  }
  return false;
}

static bool rearm_interrupts(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < controller->port_count; ++i) {
    struct xhci_interrupt *interrupt = &controller->interrupt_streams[i];
    if (!interrupt->started) {
      continue;
    }
    if (!device_present(interrupt->device)) {
      if (interrupt_pending(interrupt)) {
        controller->failure = "device disappeared during interrupt transfer";
        return false;
      }
      if (interrupt->result == USB_OK) {
        interrupt->result = USB_IO;
      }
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

static bool consume_interrupt(struct usb_host_controller *controller, struct xhci_interrupt *interrupt,
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
    if (KLOG_TRACE_ENABLED && successful_short) {
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
  if (completion == XHCI_EVENT_STALL && residue <= interrupt->receive_bytes) {
    if (interrupt->result == USB_OK) {
      interrupt->result = USB_STALL;
    }
    return true;
  }
  interrupt->result = USB_IO;
  controller->failure = "failed or invalid interrupt transfer completion";
  return false;
}

static bool async_bulk_pending(const struct xhci_async_bulk *bulk)
{
  if (bulk->tx.state == CONTROL_ACTIVE || bulk->tx.state == CONTROL_HELD) {
    return true;
  }
  for (unsigned i = 0; i < USB_ASYNC_BULK_RECEIVES; ++i) {
    if (bulk->receive[i].state != ASYNC_BULK_FREE) {
      return true;
    }
  }
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
  for (unsigned i = 0; i < USB_ASYNC_BULK_DEVICE_BUDGET; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    if (!bulk->started) {
      continue;
    }
    if (!device_present(bulk->device)) {
      if (async_bulk_pending(bulk)) {
        controller->failure = "device disappeared during async bulk transfer";
        return false;
      }
      if (bulk->result == USB_OK) {
        bulk->result = USB_IO;
      }
      continue;
    }
    if (bulk->result != USB_OK || bulk->boot_state != ASYNC_BULK_BOOT_NONE) {
      continue;
    }
    for (unsigned j = 0; j < USB_ASYNC_BULK_RECEIVES; ++j) {
      if (bulk->receive[j].state == ASYNC_BULK_FREE &&
          !publish_async_bulk_receive(bulk, &bulk->receive[j])) {
        return false;
      }
    }
  }
  return true;
}

static bool async_bulk_deadlines(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < USB_ASYNC_BULK_DEVICE_BUDGET; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    if (bulk->tx.state == CONTROL_ACTIVE &&
        (task_deadline_expired(bulk->tx.deadline) || !device_present(bulk->device))) {
      bool expired = task_deadline_expired(bulk->tx.deadline);
      bulk->tx.state = CONTROL_HELD;
      bulk->tx.result = expired ? USB_TIMEOUT : USB_IO;
      controller->failure = expired ? "async bulk OUT deadline expired" :
        "device disappeared during async bulk OUT transfer";
      return false;
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
    if (bulk->tx.state != CONTROL_ACTIVE || event->parameter != bulk->tx.trb) {
      controller->failure = "unowned async bulk OUT transfer event";
      return false;
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
        bulk->device->interrupt ? bulk->device->interrupt->queue_count : 0);
    if (KLOG_TRACE_ENABLED && successful_short) {
      bulk->successful_short_traced = true;
    }
  }
  if (completion == XHCI_EVENT_TRANSACTION_ERROR && residue == bulk->receive_bytes &&
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
  controller->failure = "failed or invalid async bulk IN transfer completion";
  return false;
}

static bool consume_bulk(struct usb_host_controller *controller, struct usb_host_device *device,
                         unsigned endpoint, const struct xhci_trb *event)
{
  struct xhci_bulk *bulk = device->bulk;
  if (!bulk || bulk->state != CONTROL_ACTIVE || !bulk->active ||
      endpoint != bulk->active->dci || event->parameter != bulk->trb) {
    controller->failure = "unowned bulk transfer event";
    return false;
  }
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  size_t residue = event->status & XHCI_EVENT_RESIDUE_MASK;
  bool inbound = bulk->active == &bulk->in;
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
  controller->failure = "failed or invalid bulk transfer completion";
  return false;
}

static bool consume_transfer(struct usb_host_controller *controller, const struct xhci_trb *event)
{
  unsigned slot = event->control >> XHCI_TRB_SLOT_SHIFT;
  unsigned endpoint = (event->control >> XHCI_EVENT_ENDPOINT_SHIFT) & XHCI_EVENT_ENDPOINT_MASK;
  struct usb_host_device *device = NULL;
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    if (device_at(controller, i)->slot == slot && slot) {
      device = device_at(controller, i);
      break;
    }
  }
  if (!device || (event->control & XHCI_EVENT_DATA)) {
    controller->failure = "unowned transfer event";
    return false;
  }
  if (endpoint != XHCI_ENDPOINT_ZERO) {
    if (device->interrupt && endpoint == device->interrupt->dci) {
      return consume_interrupt(controller, device->interrupt, event);
    }
    if (device->async_bulk &&
        (endpoint == device->async_bulk->in.dci || endpoint == device->async_bulk->out.dci)) {
      return consume_async_bulk(controller, device->async_bulk, endpoint, event);
    }
    return consume_bulk(controller, device, endpoint, event);
  }
  if (device->request.state != CONTROL_ACTIVE ||
      (event->parameter != device->request.setup && event->parameter != device->request.status &&
       (!device->request.requested || event->parameter != device->request.data))) {
    controller->failure = "unowned control transfer event";
    return false;
  }
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  size_t residue = event->status & XHCI_EVENT_RESIDUE_MASK;
  if (completion == XHCI_EVENT_SHORT_PACKET && event->parameter == device->request.data &&
      device->request.inbound && !device->request.short_packet && residue <= device->request.requested) {
    device->request.actual = device->request.requested - residue;
    device->request.short_packet = true;
    return true;
  }
  if (completion == XHCI_EVENT_SUCCESS && event->parameter == device->request.status && !residue) {
    dma_read_barrier();
    device->request.result = USB_OK;
    device->request.state = device->request.client ? CONTROL_DONE : CONTROL_IDLE;
    return true;
  }
  if (completion == XHCI_EVENT_STALL && residue <= device->request.requested) {
    device->request.result = USB_STALL;
    device->request.state = CONTROL_HALTED;
    return true;
  }
  /* An early error does not retire the rest of the control TD sequence. */
  device->request.result = USB_IO;
  device->request.state = CONTROL_HELD;
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
  if (!async_bulk_deadlines(controller) || !rearm_interrupts(controller) ||
      !rearm_async_bulk(controller)) {
    return false;
  }
  /* The class tick is bounded: it copies/collects and can publish transfers,
   * but cannot wait or recursively drain the event ring. */
  bluetooth_hci_drain_progress(controller);
  return controller->running && !controller->failed;
}

static bool controller_healthy(struct usb_host_controller *controller)
{
  uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
  if (status & (XHCI_STATUS_HALTED | XHCI_STATUS_SYSTEM_ERROR | XHCI_STATUS_CONTROLLER_ERROR |
                XHCI_STATUS_NOT_READY)) {
    controller->failure = "controller stopped or reported an error";
    return false;
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
    return false;
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
      return false;
    }
    if (!controller->command.pending) {
      return true;
    }
    if (task_deadline_expired(deadline)) {
      controller->command.timed_out = true;
      controller->failure = "controller command deadline expired";
      return false;
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

static uint32_t observe_port(struct usb_host_controller *controller, unsigned index)
{
  uint32_t status = read32(port_register(controller, index), 0);
  /* Never echo PED, reset, link-write strobe or unobserved W1C changes. */
  write32(port_register(controller, index), 0, (status & XHCI_PORT_NEUTRAL) | (status & XHCI_PORT_CHANGES));
  controller->ports[index].dirty = false;
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

static bool prepare_ports(struct usb_host_controller *controller, uint64_t deadline)
{
  bool powered = false;
  if (controller->port_power) {
    for (unsigned i = 0; i < controller->port_count; ++i) {
      uint32_t status = read32(port_register(controller, i), 0);
      if (!(status & XHCI_PORT_POWER)) {
        write32(port_register(controller, i), 0, (status & XHCI_PORT_NEUTRAL) | XHCI_PORT_POWER);
        powered = true;
      }
    }
  }
  if (powered) {
    kernel_task_sleep_until(task_deadline_after_ms(USB_PORT_POWER_DELAY_MS));
  }
  /* One startup snapshot. Later insertion cannot acquire a device reservation. */
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
        ktrace("xHCI %x:%x.%u: port %u unsupported protocol %s\n",
               controller->address.bus, controller->address.device, controller->address.function,
               i + 1, revision);
      }
      continue;
    }
    uint32_t status = observe_port(controller, i);
    if (status & XHCI_PORT_CONNECT_CHANGE) {
      port->state = PORT_REMOVED;
      klog("xHCI %x:%x.%u: root port %u changed after startup snapshot; retired until reboot\n",
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
    ktrace("xHCI %x:%x.%u: root port %u USB %s speed-id=%u slot=%u enabled; addressing pending\n",
           controller->address.bus, controller->address.device, controller->address.function,
           i + 1, revision, port->speed, port->slot);
  }
  return true;
}

static bool retire_root_devices(struct usb_host_controller *controller, unsigned port)
{
  for (unsigned i = 0; i < device_capacity(controller); ++i) {
    struct usb_host_device *device = device_at(controller, i);
    if (device->slot && !device->removed && device->port == port &&
        (device->request.state == CONTROL_ACTIVE ||
         (device->bulk && device->bulk->state == CONTROL_ACTIVE) ||
         (device->async_bulk && async_bulk_pending(device->async_bulk)) ||
         (device->interrupt && interrupt_pending(device->interrupt)))) {
      controller->failure = "root subtree disappeared during transfer";
      return false;
    }
  }
  /* Children are reserved after their parents. Disable them first, retaining
   * every slot identity and DMA object rather than allowing reuse this boot. */
  for (unsigned i = device_capacity(controller); i; --i) {
    struct usb_host_device *device = device_at(controller, i - 1);
    if (device->slot && !device->removed && device->port == port) {
      device->removed = true;
      if (!run_command(controller, XHCI_TRB_DISABLE_SLOT, device->slot, 0,
                       task_deadline_after_ms(USB_COMMAND_TIMEOUT_MS))) {
        return false;
      }
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
    uint32_t status = observe_port(controller, i);
    if (port->state == PORT_RESERVED &&
        (!(status & XHCI_PORT_CONNECTED) || !(status & XHCI_PORT_ENABLED) ||
         (status & (XHCI_PORT_OVER_CURRENT | XHCI_PORT_CONNECT_CHANGE)))) {
      if (!retire_root_devices(controller, i)) {
        return false;
      }
      port->slot = 0;
      port->state = PORT_REMOVED;
      klog("xHCI %x:%x.%u: root port %u retired until reboot\n",
           controller->address.bus, controller->address.device, controller->address.function,
           i + 1);
    } else if (port->state == PORT_ABSENT && (status & XHCI_PORT_CONNECTED)) {
      port->state = PORT_UNSUPPORTED;
      klog("xHCI %x:%x.%u: root port %u insertion unsupported until reboot\n",
           controller->address.bus, controller->address.device, controller->address.function,
           i + 1);
    }
  }
  uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
  if (status & XHCI_STATUS_PORT_CHANGE) {
    write32(operational(controller), XHCI_OP_STATUS, XHCI_STATUS_PORT_CHANGE);
  }
  return true;
}

static void stop_controller(struct usb_host_controller *controller)
{
  uint64_t flags = cpu_save_interrupts();
  controller->interrupt_ready = false;
  set_interrupt_enable(controller, false);
  bool interrupts_disabled = pci_msix_disable(&controller->msix);
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
    if (device->interrupt) {
      if (device->interrupt->result == USB_OK) {
        device->interrupt->result = USB_IO;
      }
      for (unsigned j = 0; j < USB_INTERRUPT_RECEIVES; ++j) {
        if (device->interrupt->receive[j].state == INTERRUPT_POSTED) {
          device->interrupt->receive[j].state = INTERRUPT_HELD;
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
  klog("xHCI %x:%x.%u: %s; halt=%u interrupts-disabled=%u, all resources retained until reboot\n",
       controller->address.bus, controller->address.device, controller->address.function,
       controller->failure, halted, interrupts_disabled);
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
      if (!(status & XHCI_PORT_CONNECTED) || !(status & XHCI_PORT_ENABLED) ||
          (status & (XHCI_PORT_OVER_CURRENT | XHCI_PORT_CONNECT_CHANGE))) {
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
  return &controller->ports[index].device;
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
    (status & (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED)) == (XHCI_PORT_CONNECTED | XHCI_PORT_ENABLED) &&
    !(status & (XHCI_PORT_OVER_CURRENT | XHCI_PORT_CONNECT_CHANGE));
}

static bool device_ready(const struct usb_host_device *device)
{
  struct usb_host_controller *controller = device->controller;
  return controller->running && !controller->failed && device_present(device);
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
  dma_write_barrier();
  if (!run_command(controller, type, device->slot, device->input.physical, deadline)) {
    enum usb_result result = controller->command.timed_out ? USB_TIMEOUT : USB_IO;
    stop_controller(controller);
    return result;
  }
  return USB_OK;
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
  enum usb_result result = context_command(device, XHCI_TRB_ADDRESS_DEVICE, deadline);
  if (result == USB_OK) {
    device->addressed = true;
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
  if (!device->controller->enumerating || device->hub_ports || device->async_bulk ||
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

enum usb_result usb_host_attach_child(struct usb_host_device *parent, unsigned port,
                                      const struct usb_link *link, uint64_t deadline,
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
  if (!controller->enumerating || parent->request.state != CONTROL_IDLE) {
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
      controller->descendants_used == controller->descendant_capacity) {
    return USB_UNSUPPORTED;
  }
  for (unsigned i = 0; i < controller->descendants_used; ++i) {
    if (controller->descendants[i].parent == parent && controller->descendants[i].downstream_port == port) {
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
  struct usb_host_device *device = &controller->descendants[controller->descendants_used++];
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
  if (!run_command(controller, XHCI_TRB_ENABLE_SLOT, root->slot_type, 0, deadline)) {
    stop_controller(controller);
    return USB_IO;
  }
  device->slot = controller->command.slot;
  *child = device;
  return USB_OK;
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
    if (device->request.state == CONTROL_ACTIVE &&
        (task_deadline_expired(device->request.deadline) || !device_present(device))) {
      bool expired = task_deadline_expired(device->request.deadline);
      device->request.state = CONTROL_HELD;
      device->request.result = expired ? USB_TIMEOUT : USB_IO;
      controller->failure = expired ? "control transfer deadline expired" : "device disappeared during control transfer";
      return false;
    }
  }
  return async_bulk_deadlines(controller);
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
    if (candidate->slot == device->parent_slot) {
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

enum usb_result usb_host_configure_interrupt_in(struct usb_host_device *device,
                                                const struct usb_interrupt_endpoint *endpoint,
                                                size_t receive_bytes, uint64_t deadline)
{
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  if (device->parent || device->link.speed != USB_SPEED_FULL) {
    return USB_UNSUPPORTED;
  }
  if (!endpoint || !(endpoint->address & USB_ENDPOINT_DIRECTION_IN) ||
      !(endpoint->address & USB_ENDPOINT_NUMBER) || (endpoint->address & USB_ENDPOINT_RESERVED) ||
      !endpoint->interval || !endpoint->packet || endpoint->packet > USB_FULL_SPEED_INTERRUPT_PACKET_MAX ||
      !receive_bytes || receive_bytes > USB_INTERRUPT_BYTES) {
    return USB_INVALID;
  }
  if (!controller->enumerating || device->interrupt ||
      device->request.state != CONTROL_IDLE || device->request.client) {
    return USB_BUSY;
  }
  unsigned dci = ((endpoint->address & USB_ENDPOINT_NUMBER) << 1) | 1;
  if (device->bulk && (device->bulk->in.dci == dci || device->bulk->out.dci == dci)) {
    return USB_BUSY;
  }
  if (device->async_bulk && (device->async_bulk->in.dci == dci || device->async_bulk->out.dci == dci)) {
    return USB_BUSY;
  }
  struct xhci_interrupt *interrupt = &controller->interrupt_streams[device->port];
  if (interrupt->device) {
    return USB_BUSY;
  }
  device->interrupt = interrupt;
  interrupt->device = device;
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
  /* Full-speed bInterval is frames; xHCI uses a base-two microframe exponent. */
  unsigned microframes = endpoint->interval * USB_MICROFRAMES_PER_FRAME, interval = 0;
  while (microframes > 1) {
    microframes >>= 1;
    ++interval;
  }
  uint32_t *context = input_endpoint(device, dci);
  context[0] = interval << XHCI_ENDPOINT_INTERVAL_SHIFT;
  context[1] = ((unsigned)endpoint->packet << XHCI_ENDPOINT_PACKET_SHIFT) |
    (XHCI_ENDPOINT_INTERRUPT_IN << XHCI_ENDPOINT_TYPE_SHIFT) | XHCI_ENDPOINT_ERRORS;
  context[2] = (uint32_t)interrupt->ring.physical | XHCI_TRB_CYCLE;
  context[3] = interrupt->ring.physical >> 32;
  context[4] = receive_bytes | ((unsigned)endpoint->packet << XHCI_ENDPOINT_ESIT_SHIFT);
  enum usb_result result = context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
  if (result == USB_OK) {
    interrupt->configured = true;
  }
  return result;
}

static enum usb_result interrupt_result(struct usb_host_device *device)
{
  struct xhci_interrupt *interrupt = device->interrupt;
  struct usb_host_controller *controller = device->controller;
  bool ready = device_ready(device);
  if (!ready && controller->running && !controller->failed && interrupt_pending(interrupt)) {
    controller->failure = "device disappeared during interrupt transfer";
    stop_controller(controller);
  }
  return interrupt->result != USB_OK ? interrupt->result : ready ? USB_OK : USB_IO;
}

enum usb_result usb_host_interrupt_start(struct usb_host_device *device)
{
  assert_device_owner(device);
  struct xhci_interrupt *interrupt = device->interrupt;
  if (!interrupt || !interrupt->configured) {
    return USB_INVALID;
  }
  enum usb_result result = interrupt_result(device);
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

enum usb_result usb_host_interrupt_wait(struct usb_host_device *device, uint64_t deadline)
{
  assert_device_owner(device);
  struct xhci_interrupt *interrupt = device->interrupt;
  struct usb_host_controller *controller = device->controller;
  if (!interrupt || !interrupt->started) {
    return USB_INVALID;
  }
  for (;;) {
    enum usb_result result = interrupt_result(device);
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

enum usb_result usb_host_interrupt_take(struct usb_host_device *device, void *destination,
                                        size_t capacity, struct usb_interrupt_completion *completion)
{
  assert_device_owner(device);
  struct xhci_interrupt *interrupt = device->interrupt;
  if (!interrupt || !interrupt->started) {
    return USB_INVALID;
  }
  enum usb_result result = interrupt_result(device);
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

enum usb_result usb_host_configure_async_bulk(struct usb_host_device *device,
                                             const struct usb_bulk_endpoint *in,
                                             const struct usb_bulk_endpoint *out,
                                             size_t receive_bytes, uint64_t deadline)
{
  assert_device_owner(device);
  struct usb_host_controller *controller = device->controller;
  if (!device_ready(device) || !device->addressed) {
    return USB_IO;
  }
  if (device->parent || device->link.speed != USB_SPEED_FULL) {
    return USB_UNSUPPORTED;
  }
  if (!controller->enumerating || device->async_bulk || device->bulk || device->hub_ports ||
      device->request.state != CONTROL_IDLE || device->request.client) {
    return USB_BUSY;
  }
  if (!valid_bulk_endpoint(device, in, true) || !valid_bulk_endpoint(device, out, false)) {
    return USB_UNSUPPORTED;
  }
  if (!receive_bytes || receive_bytes > USB_ASYNC_BULK_BYTES) {
    return USB_INVALID;
  }
  unsigned in_dci = ((in->address & USB_ENDPOINT_NUMBER) << 1) | 1;
  unsigned out_dci = (out->address & USB_ENDPOINT_NUMBER) << 1;
  if (device->interrupt && (device->interrupt->dci == in_dci || device->interrupt->dci == out_dci)) {
    return USB_BUSY;
  }
  struct xhci_async_bulk *bulk = NULL;
  for (unsigned i = 0; i < USB_ASYNC_BULK_DEVICE_BUDGET; ++i) {
    if (!controller->async_bulk_streams[i].device) {
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
  set_bulk_endpoint(device, &bulk->out, USB_ASYNC_BULK_BYTES);
  enum usb_result result = context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
  if (result == USB_OK) {
    bulk->configured = true;
  }
  return result;
}

static enum usb_result async_bulk_result(struct usb_host_device *device)
{
  struct xhci_async_bulk *bulk = device->async_bulk;
  struct usb_host_controller *controller = device->controller;
  bool ready = device_ready(device);
  if (controller->running && !controller->failed) {
    if (!async_bulk_deadlines(controller)) {
      stop_controller(controller);
    } else if (!ready && async_bulk_pending(bulk)) {
      controller->failure = "device disappeared during async bulk transfer";
      stop_controller(controller);
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
  return USB_OK;
}

bool usb_host_async_bulk_in_ready(const struct usb_host_device *device)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  const struct xhci_async_bulk *bulk = device->async_bulk;
  return bulk && bulk->started && bulk->result == USB_OK &&
      bulk->boot_state == ASYNC_BULK_BOOT_NONE && !bulk->in.halted &&
      device->controller->running && !device->controller->failed;
}

static bool recover_boot_bulk(struct usb_host_controller *controller)
{
  for (unsigned i = 0; i < USB_ASYNC_BULK_DEVICE_BUDGET; ++i) {
    struct xhci_async_bulk *bulk = &controller->async_bulk_streams[i];
    if (bulk->boot_state == ASYNC_BULK_BOOT_NONE) {
      continue;
    }
    struct usb_host_device *device = bulk->device;
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
      ktrace("xHCI %x:%x.%u: firmware bulk IN reset/dequeue fence complete, slot %u dci %u\n",
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
      ktrace("xHCI %x:%x.%u: firmware bulk IN operational receives posted\n",
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
  if (!bytes || !length || length > USB_ASYNC_BULK_BYTES || !ticket || bulk->tx.generation == UINT64_MAX) {
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
  if (device->interrupt && (device->interrupt->dci == in_dci || device->interrupt->dci == out_dci)) {
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
  return context_command(device, XHCI_TRB_CONFIGURE_ENDPOINT, deadline);
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
    if (timed_out || !device_present(device)) {
      controller->failure = "bulk transfer timeout or device removal";
      stop_controller(controller);
      break;
    }
    uint64_t poll = task_deadline_after_ms(USB_WORKER_POLL_MS);
    wait_activity(controller, poll < deadline ? poll : deadline);
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
  pci_write16(&controller->claim, PCI_COMMAND, command | PCI_COMMAND_MASTER);
  bool enabled = (pci_read16(controller->claim.device->address, PCI_COMMAND) & PCI_COMMAND_MASTER) != 0;
  if (enabled) {
    program_rings(controller);
    uint32_t status = read32(operational(controller), XHCI_OP_STATUS);
    enabled = !(status & (XHCI_STATUS_SYSTEM_ERROR | XHCI_STATUS_CONTROLLER_ERROR | XHCI_STATUS_NOT_READY));
  }
  controller->interrupt_ready = enabled;
  if (enabled) {
    set_interrupt_enable(controller, true);
    enabled = pci_msix_enable(&controller->msix);
  }
  if (enabled) {
    write32(operational(controller), XHCI_OP_COMMAND,
            read32(operational(controller), XHCI_OP_COMMAND) |
            XHCI_COMMAND_RUN | XHCI_COMMAND_INTERRUPT | XHCI_COMMAND_SYSTEM_ERROR);
  }
  cpu_restore_interrupts(flags);
  if (!enabled) {
    controller->failure = "cannot initialize rings or enable DMA/MSI-X";
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
  ktrace("xHCI %x:%x.%u: boot USB enumeration finished\n",
         controller->address.bus, controller->address.device, controller->address.function);
  for (;;) {
    if (!controller_healthy(controller) || !drain_events(controller) ||
        !control_deadlines(controller) || !update_ports(controller)) {
      stop_controller(controller);
      return;
    }
    bluetooth_hci_progress(controller);
    if (!recover_boot_bulk(controller)) {
      stop_controller(controller);
      return;
    }
    usb_storage_process(controller->discovery);
    if (!controller->running || controller->failed) {
      return;
    }
    if (!controller_healthy(controller)) {
      stop_controller(controller);
      return;
    }
    wait_activity(controller, task_deadline_after_ms(USB_WORKER_POLL_MS));
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
      klog("xHCI %x:%x.%u: %s (error %u); resources retained until reboot\n",
           controller->address.bus, controller->address.device, controller->address.function,
           controller->failure, (unsigned)result);
    }
  }
}
