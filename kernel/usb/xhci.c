#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/dma.h>
#include <kernel/mm/heap.h>
#include <kernel/panic.h>
#include <kernel/pci/msix.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>
#include <kernel/usb/xhci.h>
#include "registers.h"

/* Initial scheduling/deadline choices, independent of image or machine size. */
#define XHCI_STATE_TIMEOUT_MS 1000
#define XHCI_COMMAND_TIMEOUT_MS 5000
#define XHCI_PORT_SETUP_TIMEOUT_MS 30000
#define XHCI_PORT_POWER_DELAY_MS 20
#define XHCI_WORKER_POLL_MS 10
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
struct xhci_port {
  enum port_state state;
  uint8_t major, minor, slot_type, speed, slot;
  bool protocol, dirty;
};

/* A single selected controller owns these records. Only its BSP worker parses
 * events and mutates ports/commands. IF=0 protects notification/wait publication
 * against the BSP-routed interrupt; other CPUs do not access this state. */
static struct {
  struct pci_claim claim;
  struct pci_msix msix;
  struct pci_mapping bootstrap, registers;
  struct dma_buffer dcbaa, command_ring, event_ring, erst, scratchpad_array, scratchpads;
  struct xhci_port *ports;
  uint32_t operational, runtime, doorbells;
  unsigned port_count, slot_count, scratchpad_count, context_bytes;
  unsigned command_enqueue, event_dequeue;
  bool command_cycle, event_cycle, port_power;
  bool prepared, running, interrupt_ready, notified, failed;
  struct task_wait *wait;
  struct {
    phys_addr_t physical;
    unsigned type, slot, completion;
    bool pending;
  } command;
  uint64_t interrupts, commands_completed, events_consumed;
  const char *failure;
} controller;

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

static uintptr_t operational(void)
{
  return controller.registers.address + controller.operational;
}

static uintptr_t interrupter(void)
{
  return controller.registers.address + controller.runtime + XHCI_INTERRUPTER_ZERO;
}

static void set_interrupt_enable(bool enabled)
{
  uintptr_t intr = interrupter();
  uint32_t management = read32(intr, XHCI_INTR_MANAGEMENT);
  write32(intr, XHCI_INTR_MANAGEMENT,
          (management & ~(XHCI_INTR_ENABLE | XHCI_INTR_PENDING)) |
          (enabled ? XHCI_INTR_ENABLE : 0) | (management & XHCI_INTR_PENDING));
}

static uintptr_t port_register(unsigned index)
{
  return operational() + XHCI_PORTS_OFFSET + index * XHCI_PORT_BYTES;
}

static bool bootstrap_fits(unsigned offset, size_t bytes)
{
  return offset <= PCI_BOOTSTRAP_BAR0_BYTES && bytes <= PCI_BOOTSTRAP_BAR0_BYTES - offset;
}

static bool wait_boot_bits(uintptr_t base, unsigned offset, uint32_t mask, uint32_t value)
{
  uint64_t deadline = task_deadline_after_ms(XHCI_STATE_TIMEOUT_MS);
  do {
    if ((read32(base, offset) & mask) == value) {
      return true;
    }
    __asm__ volatile("pause");
  } while (!task_deadline_expired(deadline));
  return false;
}

static bool inspect_capabilities(void)
{
  uintptr_t base = controller.bootstrap.address;
  uint32_t header = read32(base, XHCI_CAP_LENGTH);
  controller.operational = header & XHCI_EXT_ID_MASK;
  unsigned version = header >> XHCI_CAP_VERSION_SHIFT;
  uint32_t counts = read32(base, XHCI_CAP_SLOTS_PORTS);
  uint32_t scratchpads = read32(base, XHCI_CAP_SCRATCHPADS);
  uint32_t features = read32(base, XHCI_CAP_FEATURES);
  controller.slot_count = counts & XHCI_MAX_SLOTS_MASK;
  controller.port_count = counts >> XHCI_MAX_PORTS_SHIFT;
  controller.context_bytes = features & XHCI_FEATURE_CONTEXT_64 ? 64 : 32;
  controller.port_power = (features & XHCI_FEATURE_PORT_POWER) != 0;
  controller.scratchpad_count =
    (((scratchpads >> XHCI_SCRATCHPAD_HIGH_SHIFT) & XHCI_SCRATCHPAD_FIELD_MASK) <<
     XHCI_SCRATCHPAD_HIGH_WEIGHT) |
    ((scratchpads >> XHCI_SCRATCHPAD_LOW_SHIFT) & XHCI_SCRATCHPAD_FIELD_MASK);
  controller.runtime = read32(base, XHCI_CAP_RUNTIME) & XHCI_RUNTIME_OFFSET_MASK;
  controller.doorbells = read32(base, XHCI_CAP_DOORBELLS) & XHCI_DOORBELL_OFFSET_MASK;
  if ((version >> XHCI_VERSION_MAJOR_SHIFT) != 1 || !(features & XHCI_FEATURE_64_BIT) ||
      !controller.slot_count || !controller.port_count ||
      !((counts >> XHCI_MAX_INTERRUPTERS_SHIFT) & XHCI_MAX_INTERRUPTERS_MASK) ||
      controller.operational < XHCI_CAP_BYTES || (controller.operational & 7) ||
      !bootstrap_fits(controller.operational, XHCI_OP_BYTES)) {
    return false;
  }
  controller.ports = kmalloc(controller.port_count * sizeof(*controller.ports));
  if (!controller.ports) {
    return false;
  }
  for (unsigned i = 0; i < controller.port_count; ++i) {
    controller.ports[i] = (struct xhci_port){0};
  }
  klog("xHCI: version=%x slots=%u ports=%u context=%u scratchpads=%u\n",
       version, controller.slot_count, controller.port_count,
       controller.context_bytes, controller.scratchpad_count);
  return true;
}

static bool legacy_handoff(unsigned offset)
{
  uintptr_t base = controller.bootstrap.address + offset;
  volatile uint8_t *os_owned = (volatile uint8_t *)(base + XHCI_LEGACY_OS_BYTE);
  /* Byte access avoids writing the independently changing BIOS semaphore. */
  *os_owned |= XHCI_LEGACY_OWNED;
  uint64_t deadline = task_deadline_after_ms(XHCI_STATE_TIMEOUT_MS);
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

static bool extended_capabilities(void)
{
  uintptr_t base = controller.bootstrap.address;
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
          first > controller.port_count || count > controller.port_count - first + 1) {
        return false;
      }
      unsigned slot_type = read32(base, offset + 12) & XHCI_PROTOCOL_SLOT_TYPE_MASK;
      for (unsigned i = first - 1; i < first - 1 + count; ++i) {
        struct xhci_port *port = &controller.ports[i];
        if (port->protocol) {
          return false;
        }
        port->protocol = true;
        port->major = header >> XHCI_PROTOCOL_MAJOR_SHIFT;
        port->minor = header >> XHCI_PROTOCOL_MINOR_SHIFT;
        port->slot_type = slot_type;
      }
    }
    if (!bootstrap_fits(offset, bytes) || (next && next < bytes)) {
      return false;
    }
    uint64_t end = (uint64_t)offset + bytes;
    uint64_t ports = (uint64_t)controller.operational + XHCI_PORTS_OFFSET;
    uint64_t runtime = (uint64_t)controller.runtime + XHCI_INTERRUPTER_ZERO;
    if ((offset < controller.operational + XHCI_OP_BYTES && controller.operational < end) ||
        (offset < ports + controller.port_count * XHCI_PORT_BYTES && ports < end) ||
        (offset < runtime + XHCI_INTERRUPTER_BYTES && runtime < end) ||
        (offset < (uint64_t)controller.doorbells + (controller.slot_count + 1) * XHCI_DOORBELL_BYTES &&
         controller.doorbells < end)) {
      return false;
    }
    offset = next ? offset + next : 0;
  }
  return !legacy || legacy_handoff(legacy);
}

static bool halt_and_reset(void)
{
  uintptr_t op = controller.bootstrap.address + controller.operational;
  if (!wait_boot_bits(op, XHCI_OP_STATUS, XHCI_STATUS_NOT_READY, 0)) {
    return false;
  }
  uint32_t command = read32(op, XHCI_OP_COMMAND);
  write32(op, XHCI_OP_COMMAND, command & ~XHCI_COMMAND_RUN);
  if (!wait_boot_bits(op, XHCI_OP_STATUS, XHCI_STATUS_HALTED, XHCI_STATUS_HALTED) ||
      !pci_complete_claim(&controller.claim)) {
    return false;
  }
  command = read32(op, XHCI_OP_COMMAND);
  write32(op, XHCI_OP_COMMAND, command | XHCI_COMMAND_RESET);
  return wait_boot_bits(op, XHCI_OP_COMMAND, XHCI_COMMAND_RESET, 0) &&
    wait_boot_bits(op, XHCI_OP_STATUS, XHCI_STATUS_NOT_READY | XHCI_STATUS_HALTED,
                   XHCI_STATUS_HALTED);
}

static bool register_region_fits(uint64_t first, uint64_t bytes)
{
  return first <= controller.claim.bars[0].bytes && bytes <= controller.claim.bars[0].bytes - first;
}

static bool regions_overlap(uint64_t first, uint64_t bytes, const struct pci_region *region)
{
  return region->bar == 0 && first < (uint64_t)region->offset + region->length &&
    region->offset < first + bytes;
}

static bool map_registers(const struct boot_info *boot)
{
  struct pci_claim *claim = &controller.claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  pci_write16(claim, PCI_COMMAND, command & ~(PCI_COMMAND_MEMORY | PCI_COMMAND_IO));
  if (pci_read16(claim->device->address, PCI_COMMAND) &
      (PCI_COMMAND_MEMORY | PCI_COMMAND_IO | PCI_COMMAND_MASTER)) {
    return false;
  }
  uint32_t ports_end = controller.operational + XHCI_PORTS_OFFSET +
    controller.port_count * XHCI_PORT_BYTES;
  uint64_t runtime_end = (uint64_t)controller.runtime + XHCI_INTERRUPTER_ZERO + XHCI_INTERRUPTER_BYTES;
  uint64_t doorbells_end = (uint64_t)controller.doorbells +
    (controller.slot_count + 1) * XHCI_DOORBELL_BYTES;
  if (!pci_size_bars(claim) || claim->bars[0].bytes < PCI_BOOTSTRAP_BAR0_BYTES ||
      !register_region_fits(0, ports_end) ||
      !register_region_fits(controller.runtime, runtime_end - controller.runtime) ||
      !register_region_fits(controller.doorbells, doorbells_end - controller.doorbells) ||
      controller.runtime < ports_end || controller.doorbells < ports_end ||
      ((uint64_t)controller.runtime < doorbells_end && controller.doorbells < runtime_end) ||
      !pci_msix_discover(claim, &controller.msix)) {
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
  if (regions_overlap(0, end, &controller.msix.table) ||
      regions_overlap(0, end, &controller.msix.pba) ||
      pci_map_bar(claim, 0, 0, end, boot, &controller.registers) != MM_OK ||
      pci_msix_map(&controller.msix, boot) != MM_OK) {
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

static bool allocate_dma(void)
{
  if (dma_buffer_allocate(&controller.dcbaa, (controller.slot_count + 1) * sizeof(uint64_t)) != MM_OK ||
      dma_buffer_allocate(&controller.command_ring, PAGE_SIZE) != MM_OK ||
      dma_buffer_allocate(&controller.event_ring, PAGE_SIZE) != MM_OK ||
      dma_buffer_allocate(&controller.erst, XHCI_RING_ALIGNMENT) != MM_OK) {
    return false;
  }
  if (controller.scratchpad_count &&
      (dma_buffer_allocate(&controller.scratchpad_array,
                          controller.scratchpad_count * sizeof(uint64_t)) != MM_OK ||
       dma_buffer_allocate(&controller.scratchpads, controller.scratchpad_count * PAGE_SIZE) != MM_OK)) {
    return false;
  }
  if (!ring_layout(&controller.command_ring) || !ring_layout(&controller.event_ring)) {
    return false;
  }
  volatile uint64_t *dcbaa = (volatile uint64_t *)controller.dcbaa.address;
  if (controller.scratchpad_count) {
    volatile uint64_t *array = (volatile uint64_t *)controller.scratchpad_array.address;
    for (unsigned i = 0; i < controller.scratchpad_count; ++i) {
      array[i] = controller.scratchpads.physical + i * PAGE_SIZE;
    }
    dcbaa[0] = controller.scratchpad_array.physical;
  }
  volatile struct xhci_trb *commands = (volatile struct xhci_trb *)controller.command_ring.address;
  commands[XHCI_RING_TRBS - 1] = (struct xhci_trb){
    .parameter = controller.command_ring.physical,
    .control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_TOGGLE_CYCLE | XHCI_TRB_CYCLE,
  };
  volatile struct xhci_erst_entry *erst = (volatile struct xhci_erst_entry *)controller.erst.address;
  *erst = (struct xhci_erst_entry){.base = controller.event_ring.physical, .size = XHCI_RING_TRBS};
  controller.command_cycle = controller.event_cycle = true;
  return true;
}

static void release_boot_resources(void)
{
  dma_buffer_release(&controller.scratchpads);
  dma_buffer_release(&controller.scratchpad_array);
  dma_buffer_release(&controller.erst);
  dma_buffer_release(&controller.event_ring);
  dma_buffer_release(&controller.command_ring);
  dma_buffer_release(&controller.dcbaa);
  kfree(controller.ports);
  controller.ports = NULL;
  if (controller.claim.reserved) {
    pci_cancel_reservation(&controller.claim);
  } else {
    pci_release_device(&controller.claim);
  }
}

void xhci_prepare(const struct boot_info *boot)
{
  struct pci_device *device;
  enum pci_selection selection = pci_select_class(XHCI_PCI_CLASS, XHCI_PCI_SUBCLASS,
                                                  XHCI_PCI_INTERFACE, &device);
  if (selection != PCI_SELECTION_UNIQUE) {
    klog("xHCI: %s\n", selection == PCI_SELECTION_ABSENT ? "absent" :
         selection == PCI_SELECTION_AMBIGUOUS ? "ambiguous controller inventory" : "incomplete PCI inventory");
    return;
  }
  if (!pci_reserve_device(device, &controller.claim)) {
    klog("xHCI: controller busy or unsupported PCI state\n");
    return;
  }
  const char *failure = "unsupported assigned BAR0 bootstrap mapping";
  if (pci_map_bootstrap_bar0(&controller.claim, boot, &controller.bootstrap) != MM_OK) {
    goto fail;
  }
  failure = "unsupported capabilities or controller record allocation failed";
  if (!inspect_capabilities()) {
    goto fail;
  }
  failure = "unsupported extended capabilities or firmware ownership timeout";
  if (!extended_capabilities()) {
    goto fail;
  }
  failure = "cannot confirm controller halt/reset";
  if (!halt_and_reset()) {
    /* Completion/reset failure may leave hardware ownership unresolved. */
    if (!controller.claim.reserved) {
      controller.failure = failure;
      klog("xHCI: %s; claim retained until reboot\n", failure);
      return;
    }
    goto fail;
  }
  failure = "invalid register/MSI-X resources";
  if (!map_registers(boot)) {
    goto fail;
  }
  failure = "4 KiB controller pages required";
  if (!(read32(operational(), XHCI_OP_PAGE_SIZE) & XHCI_PAGE_4K)) {
    goto fail;
  }
  failure = "DMA allocation or ring placement failed";
  if (!allocate_dma()) {
    goto fail;
  }
  failure = "MSI-X setup failed";
  if (!pci_msix_prepare(&controller.msix, APIC_XHCI_VECTOR)) {
    if (!pci_msix_disable(&controller.msix)) {
      controller.failure = failure;
      klog("xHCI: %s; resources retained until reboot\n", failure);
      return;
    }
    goto fail;
  }
  controller.prepared = true;
  klog("xHCI: controller prepared, command/event rings=%u TRBs; DMA disabled\n", (unsigned)XHCI_RING_TRBS);
  return;

fail:
  controller.failure = failure;
  klog("xHCI: %s\n", failure);
  release_boot_resources();
}

void xhci_interrupt(void)
{
  if (!controller.interrupt_ready) {
    return;
  }
  ++controller.interrupts;
  uint32_t status = read32(operational(), XHCI_OP_STATUS);
  if (status & XHCI_STATUS_INTERRUPT) {
    write32(operational(), XHCI_OP_STATUS, XHCI_STATUS_INTERRUPT);
  }
  uint32_t management = read32(interrupter(), XHCI_INTR_MANAGEMENT);
  write32(interrupter(), XHCI_INTR_MANAGEMENT, management);
  /* MSI-X delivery can already have cleared IP. It is not a spurious test. */
  controller.notified = true;
  struct task_wait *wait = controller.wait;
  controller.wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void wait_activity(uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  if (!controller.notified) {
    struct task_wait *wait = task_wait_prepare();
    controller.wait = wait;
    task_wait_sleep_until(wait, deadline);
    if (controller.wait == wait) {
      controller.wait = NULL;
    }
  }
  controller.notified = false;
  cpu_restore_interrupts(flags);
}

static bool consume_command(const struct xhci_trb *event)
{
  unsigned completion = event->status >> XHCI_EVENT_COMPLETION_SHIFT;
  unsigned slot = event->control >> XHCI_TRB_SLOT_SHIFT;
  if (!controller.command.pending || event->parameter != controller.command.physical ||
      completion != XHCI_EVENT_SUCCESS || !slot || slot > controller.slot_count) {
    controller.failure = "invalid or failed command completion";
    return false;
  }
  if (controller.command.type == XHCI_TRB_ENABLE_SLOT) {
    for (unsigned i = 0; i < controller.port_count; ++i) {
      if (controller.ports[i].slot == slot) {
        controller.failure = "Enable Slot returned an owned slot";
        return false;
      }
    }
  } else if (slot != controller.command.slot) {
    controller.failure = "Disable Slot completion changed slot identity";
    return false;
  }
  controller.command.slot = slot;
  controller.command.completion = completion;
  controller.command.pending = false;
  ++controller.commands_completed;
  return true;
}

static bool drain_events(void)
{
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)controller.event_ring.address;
  unsigned consumed = 0;
  while (consumed < XHCI_RING_TRBS) {
    volatile struct xhci_trb *entry = &ring[controller.event_dequeue];
    uint32_t control = entry->control;
    if ((bool)(control & XHCI_TRB_CYCLE) != controller.event_cycle) {
      break;
    }
    dma_read_barrier();
    struct xhci_trb event = {.parameter = entry->parameter, .status = entry->status, .control = control};
    unsigned type = (control >> XHCI_TRB_TYPE_SHIFT) & XHCI_TRB_TYPE_MASK;
    if (type == XHCI_EVENT_COMMAND) {
      if (!consume_command(&event)) {
        return false;
      }
    } else if (type == XHCI_EVENT_PORT) {
      unsigned port = (event.parameter >> XHCI_EVENT_PORT_SHIFT) & XHCI_EXT_ID_MASK;
      if (!port || port > controller.port_count ||
          event.status >> XHCI_EVENT_COMPLETION_SHIFT != XHCI_EVENT_SUCCESS) {
        controller.failure = "invalid root-port event";
        return false;
      }
      controller.ports[port - 1].dirty = true;
    } else {
      controller.failure = "unexpected controller event";
      return false;
    }
    ++consumed;
    ++controller.events_consumed;
    if (++controller.event_dequeue == XHCI_RING_TRBS) {
      controller.event_dequeue = 0;
      controller.event_cycle = !controller.event_cycle;
    }
  }
  if (consumed) {
    dma_full_barrier();
    /* Never repeat an unchanged dequeue on empty polls. A whole-ring drain
     * may repeat the pointer: that is the specified full-to-empty exception. */
    write64(interrupter(), XHCI_INTR_DEQUEUE,
            (controller.event_ring.physical + controller.event_dequeue * XHCI_TRB_BYTES) | XHCI_DEQUEUE_BUSY);
  }
  return true;
}

static bool controller_healthy(void)
{
  uint32_t status = read32(operational(), XHCI_OP_STATUS);
  if (status & (XHCI_STATUS_HALTED | XHCI_STATUS_SYSTEM_ERROR | XHCI_STATUS_CONTROLLER_ERROR |
                XHCI_STATUS_NOT_READY)) {
    controller.failure = "controller stopped or reported an error";
    return false;
  }
  return true;
}

static bool run_command(unsigned type, unsigned argument)
{
  KASSERT(!controller.command.pending);
  volatile struct xhci_trb *ring = (volatile struct xhci_trb *)controller.command_ring.address;
  unsigned index = controller.command_enqueue;
  volatile struct xhci_trb *entry = &ring[index];
  unsigned control = type << XHCI_TRB_TYPE_SHIFT;
  control |= argument << (type == XHCI_TRB_ENABLE_SLOT ? XHCI_TRB_SLOT_TYPE_SHIFT : XHCI_TRB_SLOT_SHIFT);
  controller.command.physical = controller.command_ring.physical + index * XHCI_TRB_BYTES;
  controller.command.type = type;
  controller.command.slot = type == XHCI_TRB_DISABLE_SLOT ? argument : 0;
  controller.command.completion = 0;
  controller.command.pending = true;
  entry->parameter = 0;
  entry->status = 0;
  dma_write_barrier();
  entry->control = control | (controller.command_cycle ? XHCI_TRB_CYCLE : 0);
  if (++controller.command_enqueue == XHCI_RING_TRBS - 1) {
    ring[XHCI_RING_TRBS - 1].control = (XHCI_TRB_LINK << XHCI_TRB_TYPE_SHIFT) |
      XHCI_TRB_TOGGLE_CYCLE | (controller.command_cycle ? XHCI_TRB_CYCLE : 0);
    controller.command_enqueue = 0;
    controller.command_cycle = !controller.command_cycle;
  }
  dma_write_barrier();
  uint64_t deadline = task_deadline_after_ms(XHCI_COMMAND_TIMEOUT_MS);
  write32(controller.registers.address, controller.doorbells, 0);
  while (controller.command.pending) {
    if (!controller_healthy() || !drain_events()) {
      return false;
    }
    if (!controller.command.pending) {
      return true;
    }
    if (task_deadline_expired(deadline)) {
      controller.failure = "controller command deadline expired";
      return false;
    }
    uint64_t poll = task_deadline_after_ms(XHCI_WORKER_POLL_MS);
    wait_activity(poll < deadline ? poll : deadline);
  }
  return true;
}

static uint32_t observe_port(unsigned index)
{
  uint32_t status = read32(port_register(index), 0);
  /* Never echo PED, reset, link-write strobe or unobserved W1C changes. */
  write32(port_register(index), 0, (status & XHCI_PORT_NEUTRAL) | (status & XHCI_PORT_CHANGES));
  controller.ports[index].dirty = false;
  return status;
}

static bool prepare_ports(void)
{
  bool powered = false;
  if (controller.port_power) {
    for (unsigned i = 0; i < controller.port_count; ++i) {
      uint32_t status = read32(port_register(i), 0);
      if (!(status & XHCI_PORT_POWER)) {
        write32(port_register(i), 0, (status & XHCI_PORT_NEUTRAL) | XHCI_PORT_POWER);
        powered = true;
      }
    }
  }
  if (powered) {
    kernel_task_sleep_until(task_deadline_after_ms(XHCI_PORT_POWER_DELAY_MS));
  }
  /* One startup snapshot. Later insertion cannot acquire a device reservation. */
  for (unsigned i = 0; i < controller.port_count; ++i) {
    struct xhci_port *port = &controller.ports[i];
    uint32_t status = observe_port(i);
    if (!(status & XHCI_PORT_CONNECTED)) {
      continue;
    }
    port->state = port->protocol && (port->major == 2 || port->major == 3) ?
      PORT_CONNECTED : PORT_UNSUPPORTED;
  }
  uint64_t deadline = task_deadline_after_ms(XHCI_PORT_SETUP_TIMEOUT_MS);
  for (unsigned i = 0; i < controller.port_count; ++i) {
    struct xhci_port *port = &controller.ports[i];
    if (port->state != PORT_CONNECTED) {
      if (port->state == PORT_UNSUPPORTED) {
        klog("xHCI: port %u unsupported protocol %u.%u\n", i + 1, port->major, port->minor);
      }
      continue;
    }
    uint32_t status = observe_port(i);
    if (status & XHCI_PORT_CONNECT_CHANGE) {
      port->state = PORT_REMOVED;
      klog("xHCI: root port %u changed after startup snapshot; retired until reboot\n", i + 1);
      continue;
    }
    if (port->major == 2 && (status & XHCI_PORT_CONNECTED) && !(status & XHCI_PORT_ENABLED)) {
      write32(port_register(i), 0, (status & XHCI_PORT_NEUTRAL) | XHCI_PORT_RESET);
    }
    while ((status & XHCI_PORT_CONNECTED) && !(status & XHCI_PORT_CONNECT_CHANGE) &&
           (!(status & XHCI_PORT_ENABLED) || (status & XHCI_PORT_RESET))) {
      if (!controller_healthy() || !drain_events()) {
        return false;
      }
      if (task_deadline_expired(deadline)) {
        controller.failure = "root-port setup deadline expired";
        return false;
      }
      wait_activity(task_deadline_after_ms(XHCI_WORKER_POLL_MS));
      status = observe_port(i);
    }
    if (!(status & XHCI_PORT_CONNECTED) || (status & XHCI_PORT_CONNECT_CHANGE)) {
      port->state = PORT_REMOVED;
      continue;
    }
    if (status & XHCI_PORT_OVER_CURRENT) {
      controller.failure = "root-port over-current";
      return false;
    }
    port->speed = (status >> XHCI_PORT_SPEED_SHIFT) & XHCI_PORT_SPEED_MASK;
    if (!port->speed || !run_command(XHCI_TRB_ENABLE_SLOT, port->slot_type)) {
      if (!controller.failure) {
        controller.failure = "root port has no speed identity";
      }
      return false;
    }
    port->slot = controller.command.slot;
    port->state = PORT_RESERVED;
    klog("xHCI: root port %u USB %u.%u speed-id=%u slot=%u enabled; addressing pending\n",
         i + 1, port->major, port->minor, port->speed, port->slot);
  }
  return true;
}

static bool update_ports(void)
{
  for (unsigned i = 0; i < controller.port_count; ++i) {
    struct xhci_port *port = &controller.ports[i];
    if (!port->dirty) {
      continue;
    }
    uint32_t status = observe_port(i);
    if (port->state == PORT_RESERVED &&
        (!(status & XHCI_PORT_CONNECTED) || !(status & XHCI_PORT_ENABLED) ||
         (status & (XHCI_PORT_OVER_CURRENT | XHCI_PORT_CONNECT_CHANGE)))) {
      if (!run_command(XHCI_TRB_DISABLE_SLOT, port->slot)) {
        return false;
      }
      port->slot = 0;
      port->state = PORT_REMOVED;
      klog("xHCI: root port %u retired until reboot\n", i + 1);
    } else if (port->state == PORT_ABSENT && (status & XHCI_PORT_CONNECTED)) {
      port->state = PORT_UNSUPPORTED;
      klog("xHCI: root port %u insertion unsupported until reboot\n", i + 1);
    }
  }
  uint32_t status = read32(operational(), XHCI_OP_STATUS);
  if (status & XHCI_STATUS_PORT_CHANGE) {
    write32(operational(), XHCI_OP_STATUS, XHCI_STATUS_PORT_CHANGE);
  }
  return true;
}

static void stop_controller(void)
{
  uint64_t flags = cpu_save_interrupts();
  controller.interrupt_ready = false;
  set_interrupt_enable(false);
  bool interrupts_disabled = pci_msix_disable(&controller.msix);
  cpu_restore_interrupts(flags);
  uintptr_t op = operational();
  bool ready = !(read32(op, XHCI_OP_STATUS) & XHCI_STATUS_NOT_READY);
  if (ready) {
    uint32_t command = read32(op, XHCI_OP_COMMAND);
    write32(op, XHCI_OP_COMMAND, command & ~(XHCI_COMMAND_RUN | XHCI_COMMAND_INTERRUPT));
  }
  uint64_t deadline = task_deadline_after_ms(XHCI_STATE_TIMEOUT_MS);
  while (ready && !(read32(op, XHCI_OP_STATUS) & XHCI_STATUS_HALTED) &&
         !task_deadline_expired(deadline)) {
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  bool halted = ready && (read32(op, XHCI_OP_STATUS) & XHCI_STATUS_HALTED);
  flags = cpu_save_interrupts();
  uint16_t command = pci_read16(controller.claim.device->address, PCI_COMMAND);
  pci_write16(&controller.claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  cpu_restore_interrupts(flags);
  controller.running = false;
  controller.failed = true;
  klog("xHCI: %s; halt=%u interrupts-disabled=%u, all resources retained until reboot\n",
       controller.failure, halted, interrupts_disabled);
}

static void program_rings(void)
{
  /* ERSTBA may cause the halted controller to fetch its table immediately.
   * BME must already be enabled, with all DMA storage retained from this point. */
  dma_write_barrier();
  uintptr_t op = operational();
  uintptr_t intr = interrupter();
  write32(op, XHCI_OP_NOTIFICATION, read32(op, XHCI_OP_NOTIFICATION) & ~XHCI_NOTIFICATION_MASK);
  write32(op, XHCI_OP_CONFIG,
          (read32(op, XHCI_OP_CONFIG) & ~XHCI_CONFIG_SLOTS_MASK) | controller.slot_count);
  write64(op, XHCI_OP_DCBAA, controller.dcbaa.physical);
  write64(op, XHCI_OP_COMMAND_RING, controller.command_ring.physical | XHCI_TRB_CYCLE |
          (read64(op, XHCI_OP_COMMAND_RING) & XHCI_COMMAND_RING_PRESERVE));
  set_interrupt_enable(false);
  write32(intr, XHCI_INTR_MODERATION, 0);
  write32(intr, XHCI_INTR_ERST_SIZE, (read32(intr, XHCI_INTR_ERST_SIZE) & ~XHCI_ERST_SIZE_MASK) | 1);
  write64(intr, XHCI_INTR_DEQUEUE, controller.event_ring.physical);
  write64(intr, XHCI_INTR_ERST_BASE, controller.erst.physical |
          (read64(intr, XHCI_INTR_ERST_BASE) & XHCI_ERST_BASE_PRESERVE));
}

static void controller_worker(void *argument)
{
  (void)argument;
  uint64_t flags = cpu_save_interrupts();
  uint16_t command = pci_read16(controller.claim.device->address, PCI_COMMAND);
  pci_write16(&controller.claim, PCI_COMMAND, command | PCI_COMMAND_MASTER);
  bool enabled = (pci_read16(controller.claim.device->address, PCI_COMMAND) & PCI_COMMAND_MASTER) != 0;
  if (enabled) {
    program_rings();
    uint32_t status = read32(operational(), XHCI_OP_STATUS);
    enabled = !(status & (XHCI_STATUS_SYSTEM_ERROR | XHCI_STATUS_CONTROLLER_ERROR | XHCI_STATUS_NOT_READY));
  }
  controller.interrupt_ready = enabled;
  if (enabled) {
    set_interrupt_enable(true);
    enabled = pci_msix_enable(&controller.msix);
  }
  if (enabled) {
    write32(operational(), XHCI_OP_COMMAND,
            read32(operational(), XHCI_OP_COMMAND) |
            XHCI_COMMAND_RUN | XHCI_COMMAND_INTERRUPT | XHCI_COMMAND_SYSTEM_ERROR);
  }
  cpu_restore_interrupts(flags);
  if (!enabled) {
    controller.failure = "cannot initialize rings or enable DMA/MSI-X";
    stop_controller();
    return;
  }
  uint64_t deadline = task_deadline_after_ms(XHCI_STATE_TIMEOUT_MS);
  while (read32(operational(), XHCI_OP_STATUS) & XHCI_STATUS_HALTED) {
    if (task_deadline_expired(deadline)) {
      controller.failure = "controller run deadline expired";
      stop_controller();
      return;
    }
    kernel_task_sleep_until(task_deadline_after_ms(1));
  }
  controller.running = true;
  if (!controller_healthy() || !prepare_ports()) {
    stop_controller();
    return;
  }
  klog("xHCI: boot root-port scan complete; USB descriptors and classes pending\n");
  for (;;) {
    if (!controller_healthy() || !drain_events() || !update_ports()) {
      stop_controller();
      return;
    }
    wait_activity(task_deadline_after_ms(XHCI_WORKER_POLL_MS));
  }
}

void xhci_start(void)
{
  if (!controller.prepared) {
    return;
  }
  enum mm_result result = kernel_task_create(controller_worker, NULL);
  if (result != MM_OK) {
    controller.failure = "cannot create controller worker";
    controller.failed = true;
    klog("xHCI: %s (error %u); resources retained until reboot\n", controller.failure, (unsigned)result);
  }
}
