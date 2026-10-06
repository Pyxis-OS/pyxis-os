#include <arch/cpu.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/net/ethernet.h>
#include <kernel/net/rtl8111.h>
#include <kernel/task.h>
#include "internal.h"
#include "io_registers.h"

#define RTL_TX_TIMEOUT_MS 5000
#define RTL_RESET_RECHECK_MS 1
#define RTL_FCS_BYTES 4u
#define RTL_MIN_FRAME_BYTES 60u
#define RTL_COMMAND_RUNNING (RTL_COMMAND_RX | RTL_COMMAND_TX)

static void stop_controller(struct rtl8111_controller *controller, const char *reason)
{
  uint64_t flags = cpu_save_interrupts();
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
  cpu_restore_interrupts(flags);
  controller->stopping = true;
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
  if (!controller || controller->started) {
    return;
  }
  controller->started = true;
  if (!controller->prepared) {
    return;
  }
  uint64_t flags = cpu_save_interrupts();
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
    uint16_t status = rtl_read16(controller, RTL_INTERRUPT_STATUS);
    if (!status) {
      continue;
    }
    rtl_write16(controller, RTL_INTERRUPT_MASK, 0);
    rtl_write16(controller, RTL_INTERRUPT_STATUS, status);
    controller->pending_interrupts |= status;
    ++controller->interrupts;
    net_worker_notify();
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
  if (!controller) {
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
    dma_full_barrier();
    rtl_write8(controller, RTL_TX_POLL, RTL_TX_POLL_NORMAL);
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
    rtl_ring_repost_receive(rx, id);
    rx->consumer = (id + 1) % RTL_RING_COUNT;
    ++rx_count;
  }
  bool busy = tx_count == RTL_RING_COUNT || rx_count == RTL_RING_COUNT;
  if (!busy) {
    /* Pending status stays latched while masked; enabling delivery closes the
     * completion/sleep race without a timer poll. */
    flags = cpu_save_interrupts();
    rtl_write16(controller, RTL_INTERRUPT_MASK, RTL_INTERRUPT_IO);
    cpu_restore_interrupts(flags);
  }
  return busy;
}

bool rtl8111_next_deadline(struct rtl8111_controller *controller, uint64_t *deadline)
{
  if (!controller) {
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
  if (tx->outstanding == RTL_RING_COUNT) {
    ++controller->queue_full;
    return NET_QUEUE_FULL;
  }
  unsigned id = tx->producer;
  if (tx->descriptors[id].opts1 & RTL_DESCRIPTOR_OWN) {
    stop_controller(controller, "TX ownership disagrees with ring state");
    return NET_UNAVAILABLE;
  }
  uint8_t *buffer = rtl_ring_buffer(tx, id);
  memcpy(buffer, frame, length);
  size_t bytes = length < RTL_MIN_FRAME_BYTES ? RTL_MIN_FRAME_BYTES : length;
  memset(buffer + length, 0, bytes - length);
  tx->descriptors[id].opts2 = 0;
  controller->tx_deadlines[id] = task_deadline_after_ms(RTL_TX_TIMEOUT_MS);
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
  return NET_OK;
}

const uint8_t *rtl8111_mac(const struct rtl8111_controller *controller)
{
  return controller && controller->prepared ? controller->mac : NULL;
}

bool rtl8111_available(const struct rtl8111_controller *controller)
{
  return controller && controller->active && controller->link_up;
}

bool rtl8111_ready(const struct rtl8111_controller *controller)
{
  return controller && controller->active;
}

bool rtl8111_prepared(const struct rtl8111_controller *controller)
{
  return controller && controller->prepared && !controller->stopping;
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
