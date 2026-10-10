#include <arch/cpu.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/rtl8111.h>
#include <kernel/net/log_udp.h>
#include <kernel/task.h>
#include "internal.h"
#include "io_registers.h"

#define RTL_TX_TIMEOUT_MS 5000
#define RTL_RESET_RECHECK_MS 1
#define RTL_FCS_BYTES 4u
#define RTL_MIN_FRAME_BYTES 60u
#define RTL_PANIC_TX_RESERVED 2u
#define RTL_PANIC_KICK_POLLS 64u
#define RTL_MSIX_IO_ENTRY 0u
#define RTL_COMMAND_RUNNING (RTL_COMMAND_RX | RTL_COMMAND_TX)

static uint64_t panic_section_enter_interrupts(void)
{
  return net_panic_context_enabled() ? cpu_save_interrupts() : 0;
}

static void panic_section_leave_interrupts(uint64_t flags)
{
  if (net_panic_context_enabled()) {
    cpu_restore_interrupts(flags);
  }
}

static void stop_controller(struct rtl8111_controller *controller, const char *reason)
{
  uint64_t flags = cpu_save_interrupts();
  if (!net_panic_gate_enter(&controller->panic_gate, RTL_PANIC_STOP)) {
    cpu_restore_interrupts(flags);
    return;
  }
  controller->active = false;
  controller->prepared = false;
  controller->link_up = false;
  controller->stop_reason = reason;
  rtl_write16(controller, RTL_INTERRUPT_MASK, 0);
  controller->interrupts_disabled = pci_msix_disable(&controller->msix) &&
    !rtl_read16(controller, RTL_INTERRUPT_MASK);
  struct pci_claim *claim = &controller->claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  pci_write16(claim, PCI_COMMAND, command & ~PCI_COMMAND_MASTER);
  controller->dma_disabled = !(pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER);
  rtl_write8(controller, RTL_CHIP_COMMAND, RTL_COMMAND_RESET);
  controller->stopping = true;
  net_panic_gate_leave(&controller->panic_gate);
  cpu_restore_interrupts(flags);
  controller->reset_deadline = task_deadline_after_ms(RTL_RESET_TIMEOUT_NS / UINT64_C(1000000));
  controller->reset_recheck = 0;
}

static void finish_stop(struct rtl8111_controller *controller)
{
  if (!task_deadline_expired(controller->reset_recheck) &&
      !task_deadline_expired(controller->reset_deadline)) {
    return;
  }
  uint8_t command = rtl_read8(controller, RTL_CHIP_COMMAND);
  bool reset = command != UINT8_MAX && !(command & (RTL_COMMAND_RESET | RTL_COMMAND_RUNNING));
  if (!reset && !task_deadline_expired(controller->reset_deadline)) {
    controller->reset_recheck = task_deadline_after_ms(RTL_RESET_RECHECK_MS);
    return;
  }
  controller->stopping = false;
  klog("rtl8111: %s; stopped (reset=%u MSI-X/mask disabled=%u DMA disabled=%u), "
       "resources retained until reboot\n", controller->stop_reason, (unsigned)reset,
       (unsigned)controller->interrupts_disabled, (unsigned)controller->dma_disabled);
}

void rtl8111_start(struct rtl8111_controller *controller)
{
  net_worker_assert_context();
  if (!controller || controller->started || net_panic_gate_closed(&controller->panic_gate)) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  if (!net_panic_gate_enter(&controller->panic_gate, RTL_PANIC_START)) {
    cpu_restore_interrupts(flags);
    return;
  }
  controller->started = true;
  if (!controller->prepared) {
    net_panic_gate_leave(&controller->panic_gate);
    cpu_restore_interrupts(flags);
    return;
  }
  struct pci_claim *claim = &controller->claim;
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  dma_full_barrier();
  pci_write16(claim, PCI_COMMAND, command | PCI_COMMAND_MASTER);
  bool ready = (pci_read16(claim->device->address, PCI_COMMAND) & PCI_COMMAND_MASTER) != 0;
  if (ready) {
    rtl_write8(controller, RTL_CHIP_COMMAND, RTL_COMMAND_RUNNING);
    rtl_write32(controller, RTL_TX_CONFIG, RTL_TX_CONFIG_8168H);
    rtl_write32(controller, RTL_RX_CONFIG, RTL_RX_CONFIG_8168H);
    uint8_t chip = rtl_read8(controller, RTL_CHIP_COMMAND);
    ready = chip != UINT8_MAX &&
      (chip & (RTL_COMMAND_RUNNING | RTL_COMMAND_RESET)) == RTL_COMMAND_RUNNING;
  }
  uint8_t phy = rtl_read8(controller, RTL_PHY_STATUS);
  ready = ready && phy != UINT8_MAX;
  for (unsigned i = 0; i < sizeof(controller->mac); ++i) {
    ready = ready && rtl_read8(controller, i) == controller->mac[i];
  }
  controller->link_up = phy & RTL_PHY_LINK;
  controller->active = ready;
  if (ready) {
    rtl_write16(controller, RTL_INTERRUPT_STATUS, UINT16_MAX);
    ready = pci_msix_enable(&controller->msix);
    if (ready) {
      rtl_write16(controller, RTL_INTERRUPT_MASK, RTL_INTERRUPT_IO);
      ready = rtl_read16(controller, RTL_INTERRUPT_MASK) == RTL_INTERRUPT_IO;
    }
  }
  net_panic_gate_leave(&controller->panic_gate);
  cpu_restore_interrupts(flags);
  if (!ready) {
    stop_controller(controller, "activation rejected");
    return;
  }
  ktrace("rtl8111: RX/TX active, %u buffers per ring, link %s; BSP worker owns completions\n",
         RTL_RING_COUNT, controller->link_up ? "up" : "down");
}

void rtl8111_interrupt(void)
{
  for (struct rtl8111_controller *controller = rtl8111_first(); controller;
       controller = rtl8111_next(controller)) {
    if (!controller->active) {
      continue;
    }
    if (!net_panic_gate_enter(&controller->panic_gate, RTL_PANIC_IRQ)) {
      continue;
    }
    uint16_t status = rtl_read16(controller, RTL_INTERRUPT_STATUS);
    if (status) {
      rtl_write16(controller, RTL_INTERRUPT_MASK, 0);
      rtl_write16(controller, RTL_INTERRUPT_STATUS, status);
      controller->pending_interrupts |= status;
      ++controller->interrupts;
    }
    net_panic_gate_leave(&controller->panic_gate);
    if (status) {
      net_worker_notify();
    }
  }
}

static bool descriptor_address_valid(const struct rtl_ring *ring, unsigned id)
{
  return ring->descriptors[id].address ==
    ring->storage.physical + PAGE_SIZE + id * RTL_BUFFER_BYTES;
}

bool rtl8111_service(struct rtl8111_controller *controller)
{
  net_worker_assert_context();
  if (!controller || net_panic_gate_closed(&controller->panic_gate)) {
    return false;
  }
  if (controller->stopping) {
    finish_stop(controller);
    return false;
  }
  if (!controller->active) {
    return false;
  }
  uint64_t flags = cpu_save_interrupts();
  uint16_t status = controller->pending_interrupts;
  controller->pending_interrupts = 0;
  cpu_restore_interrupts(flags);
  uint8_t phy = rtl_read8(controller, RTL_PHY_STATUS);
  uint8_t chip = rtl_read8(controller, RTL_CHIP_COMMAND);
  if (status == UINT16_MAX || phy == UINT8_MAX ||
      chip == UINT8_MAX ||
      (chip & (RTL_COMMAND_RUNNING | RTL_COMMAND_RESET)) != RTL_COMMAND_RUNNING) {
    stop_controller(controller, "register state unavailable or RX/TX stopped");
    return false;
  }
  bool link = phy & RTL_PHY_LINK;
  bool link_returned = link && !controller->link_up;
  if (link != controller->link_up) {
    controller->link_up = link;
    if (link) {
      uint64_t deadline = task_deadline_after_ms(RTL_TX_TIMEOUT_MS);
      for (unsigned i = 0; i < controller->tx.outstanding; ++i) {
        unsigned id = (controller->tx.consumer + i) % RTL_RING_COUNT;
        controller->tx_deadlines[id] = deadline;
      }
    }
    klog("rtl8111: link %s\n", link ? "up" : "down");
  }

  unsigned tx_count = 0;
  struct rtl_ring *tx = &controller->tx;
  while (tx->outstanding && tx_count < RTL_RING_COUNT) {
    unsigned id = tx->consumer;
    uint32_t opts = tx->descriptors[id].opts1;
    if (opts & RTL_DESCRIPTOR_OWN) {
      break;
    }
    dma_read_barrier();
    if (!descriptor_address_valid(tx, id) ||
        (opts & RTL_DESCRIPTOR_EOR) != (id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0)) {
      stop_controller(controller, "invalid TX descriptor completion");
      return false;
    }
    tx->consumer = (id + 1) % RTL_RING_COUNT;
    --tx->outstanding;
    ++tx_count;
    ++controller->completed;
  }
  /* Carrier loss does not revoke device ownership or consume the TX timeout. */
  if (link && tx->outstanding && task_deadline_expired(controller->tx_deadlines[tx->consumer])) {
    stop_controller(controller, "TX completion timed out");
    return false;
  }
  if (link && (tx_count || link_returned) && tx->outstanding) {
    /* Closely spaced kicks can be lost on this family. */
    flags = panic_section_enter_interrupts();
    if (!net_panic_gate_enter(&controller->panic_gate, RTL_PANIC_SERVICE)) {
      panic_section_leave_interrupts(flags);
      return false;
    }
    dma_full_barrier();
    rtl_write8(controller, RTL_TX_POLL, RTL_TX_POLL_NORMAL);
    net_panic_gate_leave(&controller->panic_gate);
    panic_section_leave_interrupts(flags);
  }

  unsigned rx_count = 0;
  struct rtl_ring *rx = &controller->rx;
  while (rx_count < RTL_RING_COUNT) {
    unsigned id = rx->consumer;
    uint32_t opts = rx->descriptors[id].opts1;
    if (opts & RTL_DESCRIPTOR_OWN) {
      break;
    }
    dma_read_barrier();
    size_t bytes = opts & RTL_DESCRIPTOR_LENGTH_MASK;
    if (!descriptor_address_valid(rx, id) ||
        (opts & RTL_DESCRIPTOR_EOR) != (id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0)) {
      stop_controller(controller, "invalid RX descriptor completion");
      return false;
    }
    if ((opts & RTL_RX_STATUS_ERRORS) || bytes > RTL_BUFFER_BYTES ||
        (opts & (RTL_DESCRIPTOR_FS | RTL_DESCRIPTOR_LS)) !=
          (RTL_DESCRIPTOR_FS | RTL_DESCRIPTOR_LS) ||
        bytes < ETHERNET_HEADER_BYTES + RTL_FCS_BYTES ||
        bytes > ETHERNET_FRAME_MAX + RTL_FCS_BYTES) {
      ++controller->malformed;
    } else {
      ++controller->received;
      /* Protocols borrow bytes only until this descriptor is returned below. */
      net_ethernet_receive(rtl_ring_buffer(rx, id), bytes - RTL_FCS_BYTES);
      if (!controller->active) {
        return false;
      }
    }
    flags = panic_section_enter_interrupts();
    if (!net_panic_gate_enter(&controller->panic_gate, RTL_PANIC_RX_REPOST)) {
      panic_section_leave_interrupts(flags);
      return false;
    }
    rtl_ring_repost_receive(rx, id);
    rx->consumer = (id + 1) % RTL_RING_COUNT;
    net_panic_gate_leave(&controller->panic_gate);
    panic_section_leave_interrupts(flags);
    ++rx_count;
  }
  bool busy = tx_count == RTL_RING_COUNT || rx_count == RTL_RING_COUNT;
  if (!busy) {
    /* Pending status stays latched while masked; enabling delivery closes the
     * completion/sleep race without a timer poll. */
    flags = cpu_save_interrupts();
    if (!net_panic_gate_enter(&controller->panic_gate, RTL_PANIC_SERVICE)) {
      cpu_restore_interrupts(flags);
      return false;
    }
    rtl_write16(controller, RTL_INTERRUPT_MASK, RTL_INTERRUPT_IO);
    net_panic_gate_leave(&controller->panic_gate);
    cpu_restore_interrupts(flags);
  }
  return busy;
}

bool rtl8111_next_deadline(struct rtl8111_controller *controller, uint64_t *deadline)
{
  if (!controller || net_panic_gate_closed(&controller->panic_gate)) {
    return false;
  }
  if (controller->stopping) {
    *deadline = controller->reset_recheck < controller->reset_deadline ?
      controller->reset_recheck : controller->reset_deadline;
    return true;
  }
  if (controller->active && controller->link_up && controller->tx.outstanding) {
    *deadline = controller->tx_deadlines[controller->tx.consumer];
    return true;
  }
  return false;
}

enum net_result rtl8111_transmit(struct rtl8111_controller *controller,
    const void *frame, size_t length)
{
  net_worker_assert_context();
  if (!frame || length < ETHERNET_HEADER_BYTES || length > ETHERNET_FRAME_MAX) {
    return NET_INVALID;
  }
  if (!rtl8111_available(controller)) {
    return NET_UNAVAILABLE;
  }
  struct rtl_ring *tx = &controller->tx;
  unsigned capacity = net_log_udp_enabled() ? RTL_RING_COUNT - RTL_PANIC_TX_RESERVED : RTL_RING_COUNT;
  if (tx->outstanding >= capacity) {
    ++controller->queue_full;
    return NET_QUEUE_FULL;
  }
  unsigned id = tx->producer;
  uint64_t deadline = task_deadline_after_ms(RTL_TX_TIMEOUT_MS);
  uint64_t flags = panic_section_enter_interrupts();
  if (!net_panic_gate_enter(&controller->panic_gate, RTL_PANIC_TX_BASE + id)) {
    panic_section_leave_interrupts(flags);
    return NET_UNAVAILABLE;
  }
  if (tx->descriptors[id].opts1 & RTL_DESCRIPTOR_OWN) {
    net_panic_gate_leave(&controller->panic_gate);
    panic_section_leave_interrupts(flags);
    stop_controller(controller, "TX ownership disagrees with ring state");
    return NET_UNAVAILABLE;
  }
  uint8_t *buffer = rtl_ring_buffer(tx, id);
  memcpy(buffer, frame, length);
  size_t bytes = length < RTL_MIN_FRAME_BYTES ? RTL_MIN_FRAME_BYTES : length;
  memset(buffer + length, 0, bytes - length);
  tx->descriptors[id].opts2 = 0;
  controller->tx_deadlines[id] = deadline;
  ++tx->outstanding;
  tx->producer = (id + 1) % RTL_RING_COUNT;
  uint32_t opts = RTL_DESCRIPTOR_OWN | RTL_DESCRIPTOR_FS | RTL_DESCRIPTOR_LS | bytes;
  if (id == RTL_RING_COUNT - 1) {
    opts |= RTL_DESCRIPTOR_EOR;
  }
  dma_write_barrier();
  tx->descriptors[id].opts1 = opts;
  dma_full_barrier();
  rtl_write8(controller, RTL_TX_POLL, RTL_TX_POLL_NORMAL);
  ++controller->transmitted;
  net_panic_gate_leave(&controller->panic_gate);
  panic_section_leave_interrupts(flags);
  return NET_OK;
}

bool rtl8111_panic_begin(struct rtl8111_controller *controller)
{
  if (!controller || !net_log_udp_enabled() || !controller->registers.address ||
      !controller->msix.table.mapping.address) {
    return false;
  }
  unsigned interrupted;
  if (!net_panic_gate_take(&controller->panic_gate, &interrupted)) {
    return false;
  }
  /* The gate remains fatal even if activation, reset or ownership is uncertain. */
  rtl_write16(controller, RTL_INTERRUPT_MASK, 0);
  /* Fatal ownership delegates only this prepared table entry, not the BSP's
   * PCI configuration mutators. The AP may mask delivery without resetting DMA. */
  volatile struct pci_msix_entry *table =
    (volatile struct pci_msix_entry *)controller->msix.table.mapping.address;
  table[RTL_MSIX_IO_ENTRY].control |= PCI_MSIX_VECTOR_MASK;
  bool masked = (table[RTL_MSIX_IO_ENTRY].control & PCI_MSIX_VECTOR_MASK) &&
    !rtl_read16(controller, RTL_INTERRUPT_MASK);
  if (!masked || interrupted == RTL_PANIC_START || interrupted == RTL_PANIC_STOP ||
      !controller->active || !controller->prepared || controller->stopping ||
      !controller->tx.storage.address) {
    return false;
  }
  uint8_t chip = rtl_read8(controller, RTL_CHIP_COMMAND);
  uint8_t phy = rtl_read8(controller, RTL_PHY_STATUS);
  uint16_t command = pci_read16(controller->claim.device->address, PCI_COMMAND);
  if (chip == UINT8_MAX || phy == UINT8_MAX || command == UINT16_MAX ||
      (chip & (RTL_COMMAND_RUNNING | RTL_COMMAND_RESET)) != RTL_COMMAND_RUNNING ||
      !(phy & RTL_PHY_LINK) || !(command & PCI_COMMAND_MASTER)) {
    return false;
  }
  controller->panic_slot = controller->tx.producer;
  if (interrupted >= RTL_PANIC_TX_BASE && interrupted < RTL_PANIC_TX_BASE + RTL_RING_COUNT) {
    unsigned id = interrupted - RTL_PANIC_TX_BASE;
    controller->panic_interrupted_slot = id;
    controller->panic_duplicate = !(controller->tx.descriptors[id].opts1 & RTL_DESCRIPTOR_OWN);
    controller->panic_slot = (id + 1) % RTL_RING_COUNT;
  }
  if (controller->panic_slot >= RTL_RING_COUNT) {
    return false;
  }
  controller->panic_ready = true;
  return true;
}

static bool panic_wait_slot(struct rtl8111_controller *controller, unsigned id)
{
  struct rtl_ring *tx = &controller->tx;
  for (unsigned poll = 0; poll < NET_PANIC_COMPLETION_POLLS; ++poll) {
    if (!(tx->descriptors[id].opts1 & RTL_DESCRIPTOR_OWN)) {
      dma_read_barrier();
      uint32_t opts = tx->descriptors[id].opts1;
      return descriptor_address_valid(tx, id) &&
          (opts & RTL_DESCRIPTOR_EOR) ==
          (id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0);
    }
    if (poll % RTL_PANIC_KICK_POLLS == 0) {
      /* Closely spaced doorbells can be lost; keep retrying within the budget. */
      dma_full_barrier();
      rtl_write8(controller, RTL_TX_POLL, RTL_TX_POLL_NORMAL);
    }
    __asm__ volatile("pause");
  }
  return false;
}

static bool panic_publish(struct rtl_ring *tx, unsigned id, const void *frame, size_t length)
{
  uint32_t previous = tx->descriptors[id].opts1;
  if ((previous & RTL_DESCRIPTOR_OWN) || !descriptor_address_valid(tx, id) ||
      (previous & RTL_DESCRIPTOR_EOR) !=
      (id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0)) {
    return false;
  }
  uint8_t *buffer = (uint8_t *)(tx->storage.address + PAGE_SIZE + id * RTL_BUFFER_BYTES);
  memcpy(buffer, frame, length);
  size_t bytes = length < RTL_MIN_FRAME_BYTES ? RTL_MIN_FRAME_BYTES : length;
  memset(buffer + length, 0, bytes - length);
  tx->descriptors[id].opts2 = 0;
  uint32_t opts = RTL_DESCRIPTOR_OWN | RTL_DESCRIPTOR_FS | RTL_DESCRIPTOR_LS | bytes;
  if (id == RTL_RING_COUNT - 1) {
    opts |= RTL_DESCRIPTOR_EOR;
  }
  dma_write_barrier();
  tx->descriptors[id].opts1 = opts;
  return true;
}

bool rtl8111_panic_transmit(struct rtl8111_controller *controller,
    const void *frame, size_t length)
{
  if (!controller || !controller->panic_ready || !frame ||
      length < ETHERNET_HEADER_BYTES || length > ETHERNET_FRAME_MAX) {
    return false;
  }
  struct rtl_ring *tx = &controller->tx;
  unsigned id = controller->panic_slot;
  if (!panic_wait_slot(controller, id)) {
    controller->panic_ready = false;
    return false;
  }
  if (controller->panic_duplicate) {
    /* OWN clear cannot distinguish unpublished from already completed TX. The
     * hardware cursor may still be here or may be at the following free slot.
     * Publish both before waiting; the host deduplicates this first datagram. */
    unsigned interrupted = controller->panic_interrupted_slot;
    if (!panic_publish(tx, interrupted, frame, length)) {
      controller->panic_ready = false;
      return false;
    }
    controller->panic_duplicate = false;
  }
  if (!panic_publish(tx, id, frame, length)) {
    controller->panic_ready = false;
    return false;
  }
  dma_full_barrier();
  rtl_write8(controller, RTL_TX_POLL, RTL_TX_POLL_NORMAL);
  /* Do not wait on the ambiguous duplicate: it may stay owned until wrap. */
  if (!panic_wait_slot(controller, id)) {
    controller->panic_ready = false;
    return false;
  }
  controller->panic_slot = (id + 1) % RTL_RING_COUNT;
  return true;
}

const uint8_t *rtl8111_mac(const struct rtl8111_controller *controller)
{
  return controller && controller->prepared ? controller->mac : NULL;
}

bool rtl8111_available(const struct rtl8111_controller *controller)
{
  return controller && controller->active && controller->link_up &&
    !net_panic_gate_closed(&controller->panic_gate);
}

bool rtl8111_ready(const struct rtl8111_controller *controller)
{
  return controller && controller->active && !net_panic_gate_closed(&controller->panic_gate);
}

bool rtl8111_prepared(const struct rtl8111_controller *controller)
{
  return controller && controller->prepared && !controller->stopping &&
    !net_panic_gate_closed(&controller->panic_gate);
}

bool rtl8111_carrier(const struct rtl8111_controller *controller, bool *up)
{
  net_worker_assert_context();
  *up = false;
  if (!rtl8111_prepared(controller)) {
    return false;
  }
  uint8_t status = rtl_read8(controller, RTL_PHY_STATUS);
  if (status == UINT8_MAX) {
    return false;
  }
  *up = (status & RTL_PHY_LINK) != 0;
  return true;
}

bool rtl8111_debug_ready(struct rtl8111_controller *controller,
    struct net_debug_device *device)
{
  if (!rtl8111_available(controller) ||
      atomic_load_explicit(&controller->panic_gate.state, memory_order_acquire)) {
    return false;
  }
  device->controller_id = controller->controller_id;
  memcpy(device->mac, controller->mac, sizeof(device->mac));
  device->pci = controller->claim.device->address;
  for (unsigned i = 0; i < PCI_BAR_COUNT; ++i) {
    device->bars[i] = (struct net_debug_range){controller->claim.bars[i].physical,
      controller->claim.bars[i].bytes};
  }
  device->dma[0] = (struct net_debug_range){controller->rx.storage.physical,
    controller->rx.storage.bytes};
  device->dma[1] = (struct net_debug_range){controller->tx.storage.physical,
    controller->tx.storage.bytes};
  device->dma[2] = (struct net_debug_range){controller->counters.physical,
    controller->counters.bytes};
  device->dma_count = 3;
  return true;
}

bool rtl8111_debug_service(struct rtl8111_controller *controller)
{
  if (!controller) {
    return false;
  }
  uint64_t flags = cpu_save_interrupts();
  bool entered = net_panic_gate_pipeline_begin(&controller->panic_gate);
  cpu_restore_interrupts(flags);
  if (!entered) {
    return false;
  }
  bool busy = rtl8111_service(controller);
  flags = cpu_save_interrupts();
  net_panic_gate_pipeline_end(&controller->panic_gate);
  cpu_restore_interrupts(flags);
  return busy;
}

static enum net_debug_status debug_failed(struct rtl8111_controller *controller)
{
  controller->debug.failed = true;
  return NET_DEBUG_FAILED;
}

static bool debug_device_valid(const struct rtl8111_controller *controller)
{
  if (!controller->active || !controller->prepared || controller->stopping) {
    return false;
  }
  uint8_t chip = rtl_read8(controller, RTL_CHIP_COMMAND);
  return chip != UINT8_MAX &&
    (chip & (RTL_COMMAND_RUNNING | RTL_COMMAND_RESET)) == RTL_COMMAND_RUNNING;
}

static bool debug_ring_valid(const struct rtl_ring *ring)
{
  if (!ring->storage.address || !ring->descriptors || ring->producer >= RTL_RING_COUNT ||
      ring->consumer >= RTL_RING_COUNT || ring->outstanding > RTL_RING_COUNT) {
    return false;
  }
  for (unsigned id = 0; id < RTL_RING_COUNT; ++id) {
    uint32_t opts = ring->descriptors[id].opts1;
    if (!descriptor_address_valid(ring, id) ||
        (opts & RTL_DESCRIPTOR_EOR) != (id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0)) {
      return false;
    }
  }
  return ring->receive || ring->producer ==
    (ring->consumer + ring->outstanding) % RTL_RING_COUNT;
}

enum net_debug_status rtl8111_debug_begin(struct rtl8111_controller *controller,
    uint64_t generation)
{
  if (!controller || !generation) {
    return NET_DEBUG_UNAVAILABLE;
  }
  if (!net_panic_gate_debug_begin(&controller->panic_gate, generation)) {
    return NET_DEBUG_UNSAFE;
  }
  memset(&controller->debug, 0, sizeof(controller->debug));
  if (!debug_device_valid(controller) || !controller->link_up) {
    return debug_failed(controller);
  }
  uint16_t mask = rtl_read16(controller, RTL_INTERRUPT_MASK);
  if (mask == UINT16_MAX) {
    return debug_failed(controller);
  }
  controller->debug.interrupt_mask = mask;
  rtl_write16(controller, RTL_INTERRUPT_MASK, 0);
  if (rtl_read16(controller, RTL_INTERRUPT_MASK) ||
      !pci_msix_mask_prepared_entry(&controller->msix, RTL_MSIX_IO_ENTRY,
        &controller->debug.msix_control) ||
      !debug_ring_valid(&controller->rx) || !debug_ring_valid(&controller->tx)) {
    return debug_failed(controller);
  }
  uint8_t phy = rtl_read8(controller, RTL_PHY_STATUS);
  if (phy == UINT8_MAX || !(phy & RTL_PHY_LINK)) {
    return debug_failed(controller);
  }
  return NET_DEBUG_OK;
}

static enum net_debug_status debug_tx_complete(struct rtl8111_controller *controller)
{
  if (controller->debug.failed || !debug_device_valid(controller)) {
    return debug_failed(controller);
  }
  struct rtl_ring *tx = &controller->tx;
  unsigned completed = 0;
  while (tx->outstanding && completed < RTL_RING_COUNT) {
    unsigned id = tx->consumer;
    uint32_t opts = tx->descriptors[id].opts1;
    if (opts & RTL_DESCRIPTOR_OWN) {
      break;
    }
    dma_read_barrier();
    if (!descriptor_address_valid(tx, id) ||
        (opts & RTL_DESCRIPTOR_EOR) != (id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0)) {
      return debug_failed(controller);
    }
    controller->debug.tx_owned &= ~(1u << id);
    tx->consumer = (id + 1) % RTL_RING_COUNT;
    --tx->outstanding;
    ++completed;
    ++controller->completed;
  }
  if (tx->outstanding) {
    dma_full_barrier();
    rtl_write8(controller, RTL_TX_POLL, RTL_TX_POLL_NORMAL);
  }
  return NET_DEBUG_OK;
}

enum net_debug_status rtl8111_debug_poll(struct rtl8111_controller *controller,
    uint64_t generation, void *frame, size_t capacity, size_t *length)
{
  *length = 0;
  if (!controller || !net_panic_gate_debug_owned(&controller->panic_gate, generation)) {
    return NET_DEBUG_UNSAFE;
  }
  if (debug_tx_complete(controller) != NET_DEBUG_OK) {
    return NET_DEBUG_FAILED;
  }
  if (!frame || capacity < ETHERNET_FRAME_MAX) {
    return NET_DEBUG_UNAVAILABLE;
  }
  struct rtl_ring *rx = &controller->rx;
  unsigned id = rx->consumer;
  uint32_t opts = rx->descriptors[id].opts1;
  if (opts & RTL_DESCRIPTOR_OWN) {
    return NET_DEBUG_IDLE;
  }
  dma_read_barrier();
  if (!descriptor_address_valid(rx, id) ||
      (opts & RTL_DESCRIPTOR_EOR) != (id == RTL_RING_COUNT - 1 ? RTL_DESCRIPTOR_EOR : 0)) {
    return debug_failed(controller);
  }
  size_t bytes = opts & RTL_DESCRIPTOR_LENGTH_MASK;
  bool valid = !(opts & RTL_RX_STATUS_ERRORS) &&
    (opts & (RTL_DESCRIPTOR_FS | RTL_DESCRIPTOR_LS)) == (RTL_DESCRIPTOR_FS | RTL_DESCRIPTOR_LS) &&
    bytes >= ETHERNET_HEADER_BYTES + RTL_FCS_BYTES &&
    bytes <= ETHERNET_FRAME_MAX + RTL_FCS_BYTES;
  if (valid) {
    *length = bytes - RTL_FCS_BYTES;
    memcpy(frame, (const void *)(rx->storage.address + PAGE_SIZE + id * RTL_BUFFER_BYTES), *length);
    ++controller->received;
  } else {
    ++controller->malformed;
  }
  rtl_ring_repost_receive(rx, id);
  rx->consumer = (id + 1) % RTL_RING_COUNT;
  return valid ? NET_DEBUG_OK : NET_DEBUG_IDLE;
}

enum net_debug_status rtl8111_debug_transmit(struct rtl8111_controller *controller,
    uint64_t generation, const void *frame, size_t length)
{
  if (!controller || !net_panic_gate_debug_owned(&controller->panic_gate, generation)) {
    return NET_DEBUG_UNSAFE;
  }
  if (debug_tx_complete(controller) != NET_DEBUG_OK) {
    return NET_DEBUG_FAILED;
  }
  if (!frame || length < ETHERNET_HEADER_BYTES || length > ETHERNET_FRAME_MAX) {
    return NET_DEBUG_UNAVAILABLE;
  }
  uint8_t phy = rtl_read8(controller, RTL_PHY_STATUS);
  if (phy == UINT8_MAX) {
    return debug_failed(controller);
  }
  if (!(phy & RTL_PHY_LINK)) {
    return NET_DEBUG_PENDING;
  }
  struct rtl_ring *tx = &controller->tx;
  unsigned capacity = net_log_udp_enabled() ? RTL_RING_COUNT - RTL_PANIC_TX_RESERVED : RTL_RING_COUNT;
  if (tx->outstanding >= capacity) {
    return NET_DEBUG_PENDING;
  }
  unsigned id = tx->producer;
  if (!panic_publish(tx, id, frame, length)) {
    return debug_failed(controller);
  }
  ++tx->outstanding;
  tx->producer = (id + 1) % RTL_RING_COUNT;
  controller->debug.tx_owned |= 1u << id;
  dma_full_barrier();
  rtl_write8(controller, RTL_TX_POLL, RTL_TX_POLL_NORMAL);
  ++controller->transmitted;
  return NET_DEBUG_OK;
}

enum net_debug_status rtl8111_debug_restore(struct rtl8111_controller *controller,
    uint64_t generation)
{
  if (!controller || !net_panic_gate_debug_owned(&controller->panic_gate, generation)) {
    return NET_DEBUG_UNSAFE;
  }
  if (debug_tx_complete(controller) != NET_DEBUG_OK) {
    return NET_DEBUG_FAILED;
  }
  if (controller->debug.tx_owned) {
    return NET_DEBUG_PENDING;
  }
  if (!debug_ring_valid(&controller->rx) || !debug_ring_valid(&controller->tx)) {
    return debug_failed(controller);
  }
  uint16_t status = rtl_read16(controller, RTL_INTERRUPT_STATUS);
  if (status == UINT16_MAX) {
    return debug_failed(controller);
  }
  controller->pending_interrupts |= status;
  rtl_write16(controller, RTL_INTERRUPT_STATUS, status);
  rtl_write16(controller, RTL_INTERRUPT_MASK, controller->debug.interrupt_mask);
  if (rtl_read16(controller, RTL_INTERRUPT_MASK) != controller->debug.interrupt_mask ||
      !pci_msix_restore_prepared_entry(&controller->msix, RTL_MSIX_IO_ENTRY,
        controller->debug.msix_control)) {
    return debug_failed(controller);
  }
  net_panic_gate_debug_release(&controller->panic_gate);
  return NET_DEBUG_OK;
}

bool rtl8111_debug_retained(const struct rtl8111_controller *controller)
{
  return controller && net_panic_gate_debug_retained(&controller->panic_gate);
}
