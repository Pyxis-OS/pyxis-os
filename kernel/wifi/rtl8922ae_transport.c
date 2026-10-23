/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Native subset of the rtw89 BSD reference; see NOTICE. */
#include <arch/apic.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/dma.h>
#include <arch/pci.h>
#include <kernel/memory.h>
#include <kernel/mm/dma.h>
#include <kernel/panic.h>
#include <kernel/pci/msi.h>
#include <kernel/pci/registers.h>
#include <kernel/task.h>
#include "rtl8922ae_hw.h"
#include "rtl8922ae_regs_hw.h"
#include "rtl8922ae_io.h"
#include "rtl8922ae_transport.h"

#define RTL_BAR 2u
#define RTL_REGISTER_BYTES 0x40000u
#define RTL_DMA_MAX ((UINT64_C(1) << 36) - 1)
#define RTL_TX_QUEUES 13u
#define RTL_RX_QUEUES 2u
#define RTL_RING_ENTRIES 256u
#define RTL_RING_SLICE 4096u
#define RTL_BD_BYTES 8u
#define RTL_RX_BYTES 11498u
#define RTL_RX_STRIDE 11520u
#define RTL_DATA_WD_BYTES (512u * 128u)
#define RTL_COMMAND_DESCRIPTOR 24u
#define RTL_COMMAND_STRIDE 2048u
#define RTL_IMAGE_HEADER_RESERVE 8192u
#define RTL_HEADER_RESERVE (2u * RTL_IMAGE_HEADER_RESERVE)
#define RTL_FW_CHANNEL 12u
#define RTL_SECOND UINT64_C(1000000000)
#define RTL_MS UINT64_C(1000000)
#define RTL_POLL_NS UINT64_C(50000)
#define RTL_RESET_MS 2000u
#define B_RING_DEVICE_INDEX 0x0fffu

#define R_DMA_CONFIG 0xb000u
#define B_TX_ENABLE (1u << 4)
#define B_RX_ENABLE (1u << 5)
#define B_STOP_MASTER (1u << 7)
#define B_BDRAM_RESET (1u << 16)
#define R_DMA_STOP 0xb010u
#define B_STOP_CHANNELS 0x7fffu
#define B_STOP_WPDMA (1u << 31)
#define R_TX_INDEX_CLEAR 0xb014u
#define R_RX_INDEX_CLEAR 0xb018u
#define R_DMA_BUSY 0xb01cu
#define B_DMA_BUSY (B_STOP_CHANNELS | (1u << 15) | (3u << 16) | (1u << 31))
#define R_DMA_WATCHDOG 0xb02cu
#define R_TX_NUMBER 0xb030u
#define R_RX_NUMBER 0xb050u
#define R_TX_INDEX 0xb100u
#define R_RX_INDEX 0xb160u
#define R_TX_ADDRESS 0xb200u
#define R_RX_ADDRESS 0xb300u
#define B_BD_LAST (1u << 14)
#define B_BD_ADDRESS_HIGH 0x3fc0u
#define B_RX_TAG 0x1fffu
#define B_RX_LAST (1u << 14)
#define B_RX_WRITTEN 0x3fffu

#define R_PCIE_MASK 0x30b0u
#define R_PCIE_STATUS 0x30b4u
#define R_PCIE_DMA_MASK 0x30b8u
#define R_PCIE_DMA_STATUS 0x30bcu
#define B_PCIE_HALT (1u << 0)
#define B_PCIE_AXI (1u << 5)
#define R_AXI_MASK 0xb0b0u
#define R_AXI_STATUS 0xb0b4u
#define B_AXI_RX_UNAVAILABLE (3u << 25)
#define R_HALT_MASK 0x01a0u
#define R_HALT_STATUS 0x01a4u
#define B_HALT (1u << 21)
#define B_WATCHDOG (1u << 22)
#define B_DMA_RX ((1u << 16) | (1u << 17))

#define R_PLATFORM 0x0088u
#define B_WCPU (1u << 1)
#define B_CPU_HOLD (1u << 11)
#define R_CLOCK 0x0008u
#define B_CPU_CLOCK (1u << 14)
#define R_DCPU_PLATFORM 0x0888u
#define B_DCPU_ENABLE (1u << 0)
#define R_FW_CONTROL 0x01e0u
#define B_FW_ENV (3u << 30)
#define B_FW_STATUS (15u << 26)
#define B_FW_DOWNLOAD (1u << 9)
#define B_BBMCU0_DOWNLOAD (1u << 11)
#define B_ROM_CUT_GET (1u << 8)
#define B_FW_WATCHDOG_RESET (1u << 17)
#define B_H2C_READY (1u << 1)
#define B_DOWNLOAD_READY (1u << 0)
#define R_BOOT_REASON 0x01e6u
#define R_UDM0 0x01f0u
#define R_UDM1 0x01f4u
#define R_UDM2 0x01f8u
#define R_HALT_H2C_CONTROL 0x0160u
#define R_HALT_C2H_CONTROL 0x0164u
#define R_HALT_H2C 0x0168u
#define R_HALT_C2H 0x016cu
#define R_SYSTEM_CONFIG5 0x0170u
#define B_WATCHDOG_WAKE ((1u << 10) | (1u << 9))
#define R_SECURE_MALLOC 0x0184u
#define RTL_SECURE_MALLOC 0x20248000u
#define RTL_PACKET_H2C 13u
#define RTL_PACKET_C2H 10u
#define RTL_PACKET_DOWNLOAD 14u
#define RTL_H2C_HEADER_BYTES 8u
#define RTL_H2C_CATEGORY_MAC 1u
#define RTL_H2C_CLASS_DOWNLOAD 3u
#define RTL_H2C_CATEGORY_MASK 0x3u
#define RTL_H2C_CLASS_MASK 0x3fu
#define RTL_H2C_LENGTH_MASK 0x3fffu
#define RTL_H2C_RECEIVE_ACK (UINT32_C(1) << 14)
#define RTL_H2C_DONE_ACK (UINT32_C(1) << 15)
#define RTL_RX_PACKET_TYPE_SHIFT 24u
#define RTL_RX_PACKET_TYPE_MASK 0x3fu

static struct {
  struct pci_claim claim;
  struct pci_function_reset reset;
  struct pci_mapping registers;
  struct pci_msi msi;
  struct dma_buffer rings, data_wd, receive, firmware;
  size_t firmware_slots, next_slot;
  uint64_t interrupts, received, dropped;
  unsigned tx_write, rx_read[RTL_RX_QUEUES], rx_write[RTL_RX_QUEUES];
  unsigned rx_tag[RTL_RX_QUEUES];
  uint8_t h2c_sequence;
  bool prepared, active, interrupt_live, fault, stopped, firmware_ready;
  bool download_attempted, runtime_busy;
  bool stopping, pre_deinit, mac_off;
  uint16_t initial_power_control;
} transport;

static bool valid_register(uint32_t value)
{
  return value != UINT32_MAX && value != UINT32_C(0xeaeaeaea) &&
    value != UINT32_C(0xdeadbeef);
}

static uintptr_t mmio(unsigned offset, size_t bytes)
{
  KASSERT(transport.registers.address && offset <= RTL_REGISTER_BYTES - bytes);
  return transport.registers.address + offset;
}

uint8_t rtl8922ae_read8(unsigned offset)
{
  return *(volatile uint8_t *)mmio(offset, 1);
}

uint16_t rtl8922ae_read16(unsigned offset)
{
  KASSERT(!(offset & 1));
  return *(volatile uint16_t *)mmio(offset, 2);
}

uint32_t rtl8922ae_read32(unsigned offset)
{
  KASSERT(!(offset & 3));
  return *(volatile uint32_t *)mmio(offset, 4);
}

void rtl8922ae_write8(unsigned offset, uint8_t value)
{
  *(volatile uint8_t *)mmio(offset, 1) = value;
}

void rtl8922ae_write16(unsigned offset, uint16_t value)
{
  KASSERT(!(offset & 1));
  *(volatile uint16_t *)mmio(offset, 2) = value;
}

void rtl8922ae_write32(unsigned offset, uint32_t value)
{
  KASSERT(!(offset & 3));
  *(volatile uint32_t *)mmio(offset, 4) = value;
}

uint64_t rtl8922ae_deadline(uint64_t outer, uint64_t interval)
{
  uint64_t now = arch_monotonic_ns();
  uint64_t local = UINT64_MAX - now < interval ? UINT64_MAX : now + interval;
  return local < outer ? local : outer;
}

static uint32_t get32(const void *data)
{
  const uint8_t *p = data;
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put16(void *data, uint16_t value)
{
  uint8_t *p = data;
  p[0] = value;
  p[1] = value >> 8;
}

static void put32(void *data, uint32_t value)
{
  uint8_t *p = data;
  for (unsigned i = 0; i < 4; ++i) {
    p[i] = value >> (8 * i);
  }
}

static void mask_interrupts(void)
{
  rtl8922ae_write32(R_PCIE_MASK, 0);
  rtl8922ae_write32(R_PCIE_DMA_MASK, 0);
  rtl8922ae_write32(R_AXI_MASK, 0);
  rtl8922ae_write32(R_HALT_MASK, 0);
}

static void enable_interrupts(void)
{
  rtl8922ae_write32(R_HALT_MASK, B_HALT | B_WATCHDOG);
  rtl8922ae_write32(R_AXI_MASK, B_AXI_RX_UNAVAILABLE);
  rtl8922ae_write32(R_PCIE_DMA_MASK, B_DMA_RX);
  rtl8922ae_write32(R_PCIE_MASK, B_PCIE_HALT | B_PCIE_AXI);
}

void rtl8922ae_transport_interrupt(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  if (!transport.interrupt_live) {
    return;
  }
  uint32_t cause = rtl8922ae_read32(R_PCIE_STATUS);
  uint32_t dma = rtl8922ae_read32(R_PCIE_DMA_STATUS);
  if (!valid_register(cause) || !valid_register(dma)) {
    transport.fault = true;
    transport.interrupt_live = false;
    pci_msi_disable(&transport.msi);
    return;
  }
  if (!(cause & (B_PCIE_HALT | B_PCIE_AXI)) && !(dma & B_DMA_RX)) {
    return;
  }
  ++transport.interrupts;
  /* Mask until the bounded worker drain. No DMA pointer escapes interrupt entry. */
  mask_interrupts();
  uint32_t halt = rtl8922ae_read32(R_HALT_STATUS);
  uint32_t axi = rtl8922ae_read32(R_AXI_STATUS);
  if (!valid_register(halt) || !valid_register(axi) || !valid_register(dma) ||
      (halt & (B_HALT | B_WATCHDOG))) {
    transport.fault = true;
  }
  rtl8922ae_write32(R_HALT_STATUS, halt & (B_HALT | B_WATCHDOG));
  rtl8922ae_write32(R_AXI_STATUS, axi);
  rtl8922ae_write32(R_PCIE_DMA_STATUS, dma);
  rtl8922ae_write32(R_PCIE_STATUS, cause);
}

/* Firmware startup can produce unsolicited C2H/diagnostic packets. Validate
 * their bounded envelopes and return every buffer; this slice consumes no RF
 * results and exposes no borrowed DMA storage. */
static void drain_receive(void)
{
  if (!transport.active || transport.fault) {
    return;
  }
  for (unsigned queue = 0; queue < RTL_RX_QUEUES; ++queue) {
    uint32_t indices = rtl8922ae_read32(R_RX_INDEX + queue * 4);
    if (!valid_register(indices) || ((indices >> 16) & B_RING_DEVICE_INDEX) >= RTL_RING_ENTRIES) {
      transport.fault = true;
      return;
    }
    unsigned pending = (((indices >> 16) & B_RING_DEVICE_INDEX) + RTL_RING_ENTRIES - transport.rx_read[queue]) %
      RTL_RING_ENTRIES;
    for (unsigned i = 0; i < pending && i < 32; ++i) {
      unsigned slot = transport.rx_read[queue];
      uint8_t *buffer = (void *)(transport.receive.address +
          (queue * RTL_RING_ENTRIES + slot) * RTL_RX_STRIDE);
      dma_read_barrier();
      uint32_t completed = get32(buffer);
      unsigned tag = completed >> 16 & B_RX_TAG;
      size_t bytes = completed & B_RX_WRITTEN;
      /* Device pointer publication can precede the final tag store. Leave the
       * slot owned by hardware and observe it on the next bounded worker step. */
      if (tag != transport.rx_tag[queue]) {
        break;
      }
      if (!(completed & B_RX_LAST) || bytes < 4 + 24 || bytes > RTL_RX_BYTES) {
        transport.fault = true;
        return;
      }
      uint32_t descriptor = get32(buffer + 4);
      size_t header = descriptor & (1u << 31) ? 40 : 24;
      size_t offset = 4 + header + ((descriptor >> 14) & 3) * 2 +
        ((descriptor >> 18) & 3) * 8 + ((descriptor >> 20) & 3) * 16 +
        ((descriptor >> 22) & 3) * 8;
      size_t payload = descriptor & 0x3fff;
      if (offset > bytes || payload > bytes - offset) {
        transport.fault = true;
        return;
      }
      ++transport.received;
      if (((descriptor >> RTL_RX_PACKET_TYPE_SHIFT) & RTL_RX_PACKET_TYPE_MASK) == RTL_PACKET_C2H) {
        const uint8_t *message = buffer + offset;
        if (payload < RTL_H2C_HEADER_BYTES ||
            (get32(message + 4) & RTL_H2C_LENGTH_MASK) != payload ||
            !rtl8922ae_runtime_receive(message, payload)) {
          transport.fault = true;
          return;
        }
      } else {
        ++transport.dropped;
      }
      /* Clear completion before restoring the host producer index. The BD
       * still names this same retained buffer; no allocation occurs here. */
      put32(buffer, 0);
      dma_write_barrier();
      transport.rx_read[queue] = (slot + 1) % RTL_RING_ENTRIES;
      transport.rx_write[queue] = (transport.rx_write[queue] + 1) % RTL_RING_ENTRIES;
      transport.rx_tag[queue] = tag == B_RX_TAG ? 1 : tag + 1;
      rtl8922ae_write16(R_RX_INDEX + queue * 4, transport.rx_write[queue]);
    }
  }
  uint64_t flags = cpu_save_interrupts();
  if (transport.interrupt_live && !transport.fault) {
    enable_interrupts();
  }
  cpu_restore_interrupts(flags);
}

bool rtl8922ae_sleep(uint64_t interval, uint64_t deadline)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  if (task_deadline_expired(deadline) || (transport.fault && !transport.stopping) ||
      (!transport.stopping && !rtl8922ae_operation_continue())) {
    return false;
  }
  uint64_t target = rtl8922ae_deadline(deadline, interval);
  do {
    drain_receive();
    /* AFE delays may be long. Bound cancellation/current-rfkill observation
     * without changing the reference's required total delay. */
    kernel_task_sleep_until(rtl8922ae_deadline(target, 10 * RTL_MS));
    if ((!transport.stopping && (transport.fault || !rtl8922ae_operation_continue())) ||
        task_deadline_expired(deadline)) return false;
  } while (!task_deadline_expired(target));
  return true;
}

bool rtl8922ae_transport_poll(void)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  if (!transport.active || transport.fault || transport.stopping) {
    return false;
  }
  drain_receive();
  return !transport.fault;
}

bool rtl8922ae_poll32_observe(unsigned offset, uint32_t mask, uint32_t expected,
    uint64_t deadline, uint32_t *observed, bool *sampled)
{
  if (observed) *observed = 0;
  if (sampled) *sampled = false;
  do {
    if (task_deadline_expired(deadline)) {
      return false;
    }
    uint32_t value = rtl8922ae_read32(offset);
    if (observed) *observed = value;
    if (sampled) *sampled = true;
    if (!valid_register(value) || (transport.fault && !transport.stopping)) {
      return false;
    }
    if ((value & mask) == expected) {
      return true;
    }
  } while (rtl8922ae_sleep(RTL_POLL_NS, deadline));
  return false;
}

bool rtl8922ae_poll32(unsigned offset, uint32_t mask, uint32_t expected,
    uint64_t deadline)
{
  return rtl8922ae_poll32_observe(offset, mask, expected, deadline, NULL, NULL);
}

static bool change32(unsigned offset, uint32_t clear, uint32_t set)
{
  uint32_t value = rtl8922ae_read32(offset);
  if (!valid_register(value)) {
    return false;
  }
  rtl8922ae_write32(offset, (value & ~clear) | set);
  return true;
}

static bool bus_master(bool enabled)
{
  uint64_t flags = cpu_save_interrupts();
  struct pci_address address = transport.claim.device->address;
  uint16_t command = pci_read16(address, PCI_COMMAND);
  uint16_t expected = enabled ? command | PCI_COMMAND_MASTER : command & ~PCI_COMMAND_MASTER;
  bool valid = command != UINT16_MAX && (command & PCI_COMMAND_MEMORY) &&
    (command & PCI_COMMAND_INTX_DISABLE) && pci_write16(&transport.claim, PCI_COMMAND, expected) &&
    pci_read16(address, PCI_COMMAND) == expected;
  cpu_restore_interrupts(flags);
  return valid;
}

static bool allocate_dma(struct dma_buffer *buffer, size_t bytes)
{
  return dma_buffer_allocate_below(buffer, bytes, RTL_DMA_MAX) == MM_OK &&
    buffer->bytes && buffer->physical <= RTL_DMA_MAX &&
    buffer->bytes - 1 <= RTL_DMA_MAX - buffer->physical;
}

static bool span(const struct dma_buffer *buffer, size_t offset, size_t bytes,
    phys_addr_t *physical, void **address)
{
  if (!bytes || offset > buffer->bytes || bytes > buffer->bytes - offset ||
      buffer->physical > RTL_DMA_MAX || offset > RTL_DMA_MAX - buffer->physical ||
      bytes - 1 > RTL_DMA_MAX - (buffer->physical + offset)) {
    return false;
  }
  *physical = buffer->physical + offset;
  *address = (void *)(buffer->address + offset);
  return true;
}

static bool initialize_receive(void)
{
  for (unsigned queue = 0; queue < RTL_RX_QUEUES; ++queue) {
    uint8_t *ring = (void *)(transport.rings.address + (RTL_TX_QUEUES + queue) * RTL_RING_SLICE);
    for (unsigned slot = 0; slot < RTL_RING_ENTRIES; ++slot) {
      phys_addr_t physical;
      void *buffer;
      if (!span(&transport.receive, (queue * RTL_RING_ENTRIES + slot) * RTL_RX_STRIDE,
          RTL_RX_BYTES, &physical, &buffer)) {
        return false;
      }
      put16(ring + slot * RTL_BD_BYTES, RTL_RX_BYTES);
      put16(ring + slot * RTL_BD_BYTES + 2, (physical >> 32) << 6);
      put32(ring + slot * RTL_BD_BYTES + 4, physical);
    }
    transport.rx_write[queue] = RTL_RING_ENTRIES - 1;
    transport.rx_tag[queue] = 1;
  }
  return true;
}

bool rtl8922ae_transport_prepare(const struct boot_info *boot, struct pci_device *device,
    size_t firmware_bytes, const char **detail)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  *detail = "RTL8922AE PCI reset admission refused";
  if (!pci_reserve_device(device, &transport.claim)) {
    return false;
  }
  if (!pci_function_reset_prepare(&transport.claim, &transport.reset)) {
    *detail = transport.reset.detail;
    pci_cancel_reservation(&transport.claim);
    return false;
  }
  transport.initial_power_control = transport.reset.power_control;
  if (!pci_function_reset_boot(&transport.reset, task_deadline_after_ms(RTL_RESET_MS))) {
    *detail = transport.reset.detail;
    return false;
  }
  if (!pci_complete_claim(&transport.claim)) {
    *detail = "RTL8922AE PCI claim completion failed after reset; ownership retained";
    return false;
  }
  if (!pci_size_bars(&transport.claim)) {
    *detail = "RTL8922AE BAR sizing refused; PCI ownership retained";
    return false;
  }
  if (transport.claim.bars[RTL_BAR].bytes < RTL_REGISTER_BYTES) {
    *detail = "RTL8922AE BAR2 too small for register window; PCI ownership retained";
    return false;
  }
  if (pci_map_bar(&transport.claim, RTL_BAR, 0, RTL_REGISTER_BYTES, boot,
          &transport.registers) != MM_OK) {
    *detail = "RTL8922AE BAR2 mapping refused; PCI ownership retained";
    return false;
  }
  if (!pci_msi_prepare(&transport.claim, &transport.msi, APIC_WIFI_VECTOR)) {
    *detail = "RTL8922AE MSI layout/programming refused; PCI ownership retained";
    return false;
  }
  uint16_t command = PCI_COMMAND_MEMORY | PCI_COMMAND_INTX_DISABLE;
  if (!pci_write16(&transport.claim, PCI_COMMAND, command) ||
      pci_read16(device->address, PCI_COMMAND) != command) {
    *detail = "RTL8922AE PCI command readback failed; PCI ownership retained";
    return false;
  }
  *detail = "RTL8922AE 36-bit DMA allocation refused; no driver DMA started";
  /* All section chunks stay immutable until terminal reset. The reference
   * retains trailing command buffers even after the TXBD consumer advances. */
  if (firmware_bytes > SIZE_MAX - RTL8922AE_FIRMWARE_CHUNK_BYTES + 1) {
    return false;
  }
  transport.firmware_slots = (firmware_bytes + RTL8922AE_FIRMWARE_CHUNK_BYTES - 1) /
    RTL8922AE_FIRMWARE_CHUNK_BYTES + 2u * RTL8922AE_FIRMWARE_MAX_SECTIONS;
  if (transport.firmware_slots >
      (SIZE_MAX - RTL_HEADER_RESERVE - RTL_COMMAND_STRIDE) / RTL_COMMAND_STRIDE) {
    return false;
  }
  /* One separately retained runtime packet is reused only after TX consumption. */
  size_t firmware_backing = RTL_HEADER_RESERVE +
      (transport.firmware_slots + 1) * RTL_COMMAND_STRIDE;
  if (!allocate_dma(&transport.rings, (RTL_TX_QUEUES + RTL_RX_QUEUES) * RTL_RING_SLICE) ||
      !allocate_dma(&transport.data_wd, (RTL_TX_QUEUES - 1) * RTL_DATA_WD_BYTES) ||
      !allocate_dma(&transport.receive, RTL_RX_QUEUES * RTL_RING_ENTRIES * RTL_RX_STRIDE) ||
      !allocate_dma(&transport.firmware, firmware_backing) || !initialize_receive()) {
    dma_buffer_release(&transport.firmware);
    dma_buffer_release(&transport.receive);
    dma_buffer_release(&transport.data_wd);
    dma_buffer_release(&transport.rings);
    return false;
  }
  transport.prepared = true;
  return true;
}

static bool publish_rings(void)
{
  dma_write_barrier();
  for (unsigned queue = 0; queue < RTL_TX_QUEUES + RTL_RX_QUEUES; ++queue) {
    phys_addr_t physical;
    void *ring;
    if (!span(&transport.rings, queue * RTL_RING_SLICE, RTL_RING_ENTRIES * RTL_BD_BYTES,
        &physical, &ring)) {
      return false;
    }
    bool tx = queue < RTL_TX_QUEUES;
    unsigned channel = tx ? queue : queue - RTL_TX_QUEUES;
    unsigned number = (tx ? R_TX_NUMBER : R_RX_NUMBER) + channel * 2;
    unsigned base = (tx ? R_TX_ADDRESS : R_RX_ADDRESS) + channel * 8;
    unsigned index = (tx ? R_TX_INDEX : R_RX_INDEX) + channel * 4;
    rtl8922ae_write16(number, RTL_RING_ENTRIES);
    rtl8922ae_write32(base, physical);
    rtl8922ae_write32(base + 4, physical >> 32);
    rtl8922ae_write16(index, tx ? 0 : RTL_RING_ENTRIES - 1);
    if (rtl8922ae_read16(number) != RTL_RING_ENTRIES ||
        rtl8922ae_read32(base) != (uint32_t)physical ||
        rtl8922ae_read32(base + 4) != physical >> 32) {
      return false;
    }
  }
  return true;
}

bool rtl8922ae_transport_begin(uint8_t cut, uint64_t deadline, const char **detail)
{
  KASSERT(transport.prepared && cpu_interrupts_enabled());
  *detail = "RTL8922AE PCI DMA bootstrap failed";
  mask_interrupts();
  if (!rtl8922ae_hw_pci_prepare(deadline, detail) ||
      !change32(R_DMA_STOP, 0, B_STOP_WPDMA | B_STOP_CHANNELS) ||
      !change32(R_DMA_CONFIG, B_TX_ENABLE | B_RX_ENABLE, B_STOP_MASTER)) {
    return false;
  }
  rtl8922ae_write32(R_TX_INDEX_CLEAR, B_STOP_CHANNELS);
  rtl8922ae_write32(R_RX_INDEX_CLEAR, 3);
  if (!rtl8922ae_poll32(R_DMA_BUSY, B_DMA_BUSY, 0,
          rtl8922ae_deadline(deadline, RTL_MS)) ||
      !rtl8922ae_hw_pci_mode(deadline, detail) || !publish_rings() ||
      !change32(R_DMA_CONFIG, 0, B_BDRAM_RESET) ||
      !rtl8922ae_poll32(R_DMA_CONFIG, B_BDRAM_RESET, 0,
          rtl8922ae_deadline(deadline, RTL_SECOND / 2)) ||
      !rtl8922ae_hw_pci_finish(cut, deadline, detail)) {
    return false;
  }
  if (!change32(R_DMA_STOP, 1u << RTL_FW_CHANNEL, B_STOP_CHANNELS & ~(1u << RTL_FW_CHANNEL)) ||
      !change32(R_DMA_CONFIG, B_STOP_MASTER, B_TX_ENABLE | B_RX_ENABLE)) {
    return false;
  }
  uint32_t watchdog = rtl8922ae_read32(R_DMA_WATCHDOG);
  if (!valid_register(watchdog)) {
    return false;
  }
  rtl8922ae_write32(R_DMA_WATCHDOG, (watchdog & ~0x1fu) | 4u);
  /* MMIO addresses are complete before PCI permits the first DMA request. */
  dma_full_barrier();
  if (!bus_master(true)) {
    return false;
  }
  transport.active = true;
  uint64_t flags = cpu_save_interrupts();
  enable_interrupts();
  transport.interrupt_live = pci_msi_enable(&transport.msi);
  cpu_restore_interrupts(flags);
  return transport.interrupt_live;
}

static bool send_command(const uint8_t *packet, phys_addr_t physical, size_t bytes,
    uint64_t deadline)
{
  if (!bytes || bytes > UINT16_MAX || physical > RTL_DMA_MAX ||
      bytes - 1 > RTL_DMA_MAX - physical || !packet) {
    return false;
  }
  uint64_t local = rtl8922ae_deadline(deadline, RTL_SECOND);
  unsigned next = (transport.tx_write + 1) % RTL_RING_ENTRIES;
  do {
    uint32_t index = rtl8922ae_read32(R_TX_INDEX + RTL_FW_CHANNEL * 4);
    if (!valid_register(index) || ((index >> 16) & B_RING_DEVICE_INDEX) >= RTL_RING_ENTRIES || transport.fault) {
      return false;
    }
    if (next != ((index >> 16) & B_RING_DEVICE_INDEX)) {
      break;
    }
    if (!rtl8922ae_sleep(RTL_POLL_NS, local)) {
      return false;
    }
  } while (true);
  uint8_t *bd = (void *)(transport.rings.address + RTL_FW_CHANNEL * RTL_RING_SLICE +
      transport.tx_write * RTL_BD_BYTES);
  put16(bd, bytes);
  put16(bd + 2, B_BD_LAST | ((physical >> 32) << 6 & B_BD_ADDRESS_HIGH));
  put32(bd + 4, physical);
  dma_write_barrier();
  transport.tx_write = next;
  rtl8922ae_write16(R_TX_INDEX + RTL_FW_CHANNEL * 4, next);
  return !transport.fault;
}

static bool start_wcpu(bool include_bb)
{
  if (!change32(R_PLATFORM, B_WCPU, 0) ||
      !change32(R_PLATFORM, 0, B_CPU_HOLD) || !change32(R_PLATFORM, 0, B_WCPU)) {
    return false;
  }
  uint32_t value = rtl8922ae_read32(R_FW_CONTROL);
  if (!valid_register(value)) {
    return false;
  }
  rtl8922ae_write32(R_FW_CONTROL, value & B_FW_ENV);
  value = rtl8922ae_read32(R_DCPU_PLATFORM);
  if (!valid_register(value) && value != UINT32_C(0xdeadbeef)) {
    return false;
  }
  /* Successful Linux startup reads this value here and performs the same RMW.
   * WCPU progress is checked by the bounded H2C-ready wait below. */
  rtl8922ae_write32(R_DCPU_PLATFORM, value | B_DCPU_ENABLE);
  rtl8922ae_write32(R_UDM0, 0);
  rtl8922ae_write32(R_HALT_C2H, 0);
  rtl8922ae_write32(R_UDM2, 0);
  if (!change32(R_FW_CONTROL, 0, B_FW_DOWNLOAD |
      (include_bb ? B_BBMCU0_DOWNLOAD : 0))) {
    return false;
  }
  rtl8922ae_write32(R_UDM1, 0);
  rtl8922ae_write32(R_UDM2, 0);
  rtl8922ae_write32(R_HALT_H2C, 0);
  rtl8922ae_write32(R_HALT_C2H, 0);
  rtl8922ae_write32(R_HALT_H2C_CONTROL, 0);
  rtl8922ae_write32(R_HALT_C2H_CONTROL, 0);
  rtl8922ae_write32(R_HALT_STATUS, B_HALT);
  if (!change32(R_CLOCK, 0, B_CPU_CLOCK) ||
      !change32(R_SYSTEM_CONFIG5, B_WATCHDOG_WAKE, 0) ||
      !change32(R_FW_CONTROL, B_FW_WATCHDOG_RESET | B_ROM_CUT_GET, 0)) {
    return false;
  }
  uint16_t reason = rtl8922ae_read16(R_BOOT_REASON);
  if (reason == UINT16_MAX) {
    return false;
  }
  rtl8922ae_write16(R_BOOT_REASON, reason & ~7u);
  return change32(R_PLATFORM, B_WCPU, 0) && change32(R_PLATFORM, B_CPU_HOLD, 0) &&
    change32(R_PLATFORM, 0, B_WCPU);
}

static bool firmware_ready(uint64_t deadline)
{
  do {
    uint32_t control = rtl8922ae_read32(R_FW_CONTROL);
    if (!valid_register(control) || transport.fault) {
      return false;
    }
    unsigned status = (control & B_FW_STATUS) >> 26;
    if (status >= 4 && status <= 7) {
      return false;
    }
    if (!(control & B_FW_DOWNLOAD) && status == 3) {
      return true;
    }
  } while (rtl8922ae_sleep(RTL_POLL_NS, deadline));
  return false;
}

struct staged_image {
  phys_addr_t header_physical;
  const uint8_t *header;
  size_t header_bytes, first_slot, slots;
};

static bool stage_image(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_plan *plan, size_t image_index,
    struct staged_image *staged)
{
  size_t header_payload = RTL_H2C_HEADER_BYTES + plan->header_bytes;
  size_t header_bytes = RTL_COMMAND_DESCRIPTOR + header_payload;
  void *storage;
  if (!plan->section_count || plan->section_count > RTL8922AE_FIRMWARE_MAX_SECTIONS ||
      !plan->header_bytes || plan->header_bytes > sizeof(plan->header) ||
      header_bytes > RTL_IMAGE_HEADER_RESERVE || header_payload > RTL_H2C_LENGTH_MASK ||
      !span(&transport.firmware, image_index * RTL_IMAGE_HEADER_RESERVE,
          header_bytes, &staged->header_physical, &storage)) {
    return false;
  }
  uint8_t *packet = storage;
  put32(packet, header_payload | RTL_PACKET_H2C << 24);
  put32(packet + RTL_COMMAND_DESCRIPTOR,
      RTL_H2C_CATEGORY_MAC | RTL_H2C_CLASS_DOWNLOAD << 2);
  put32(packet + RTL_COMMAND_DESCRIPTOR + 4, header_payload);
  memcpy(packet + RTL_COMMAND_DESCRIPTOR + RTL_H2C_HEADER_BYTES, plan->header, plan->header_bytes);
  staged->header = packet;
  staged->header_bytes = header_bytes;
  staged->first_slot = transport.next_slot;
  for (size_t section = 0; section < plan->section_count; ++section) {
    if (!plan->sections[section].length) {
      return false;
    }
    for (size_t offset = 0; offset < plan->sections[section].length;) {
      size_t length = plan->sections[section].length - offset;
      if (length > RTL8922AE_FIRMWARE_CHUNK_BYTES) {
        length = RTL8922AE_FIRMWARE_CHUNK_BYTES;
      }
      phys_addr_t physical;
      if (transport.next_slot >= transport.firmware_slots ||
          !span(&transport.firmware, RTL_HEADER_RESERVE +
              transport.next_slot * RTL_COMMAND_STRIDE, RTL_COMMAND_DESCRIPTOR + length,
              &physical, &storage)) {
        return false;
      }
      packet = storage;
      if (!rtl8922ae_firmware_copy_section(asset, bytes, plan, section, offset,
          packet + RTL_COMMAND_DESCRIPTOR, length)) {
        return false;
      }
      put32(packet, length | RTL_PACKET_DOWNLOAD << 24);
      ++transport.next_slot;
      offset += length;
    }
  }
  staged->slots = transport.next_slot - staged->first_slot;
  return true;
}

static bool download_complete(uint32_t enable_mask, uint64_t deadline)
{
  do {
    uint32_t control = rtl8922ae_read32(R_FW_CONTROL);
    if (!valid_register(control) || transport.fault) {
      return false;
    }
    unsigned status = (control & B_FW_STATUS) >> 26;
    if (status >= 4 && status <= 7) {
      return false;
    }
    if (!(control & enable_mask)) {
      return true;
    }
  } while (rtl8922ae_sleep(RTL_POLL_NS, deadline));
  return false;
}

static bool download_image(const struct staged_image *image,
    uint32_t enable_mask, uint64_t deadline, const char **detail)
{
  *detail = "RTL8922AE firmware H2C download path unavailable";
  if (!rtl8922ae_poll32(R_FW_CONTROL, B_H2C_READY, B_H2C_READY,
      rtl8922ae_deadline(deadline, RTL_SECOND))) {
    return false;
  }
  *detail = "RTL8922AE firmware header download failed";
  if (!send_command(image->header, image->header_physical, image->header_bytes, deadline) ||
      !rtl8922ae_poll32(R_FW_CONTROL, B_DOWNLOAD_READY, B_DOWNLOAD_READY,
          rtl8922ae_deadline(deadline, RTL_SECOND))) {
    return false;
  }
  rtl8922ae_write32(R_HALT_H2C_CONTROL, 0);
  rtl8922ae_write32(R_HALT_C2H_CONTROL, 0);
  *detail = "RTL8922AE firmware section download failed";
  for (size_t i = 0; i < image->slots; ++i) {
    phys_addr_t physical;
    void *storage;
    if (!span(&transport.firmware, RTL_HEADER_RESERVE +
        (image->first_slot + i) * RTL_COMMAND_STRIDE, RTL_COMMAND_STRIDE,
        &physical, &storage)) {
      return false;
    }
    const uint8_t *packet = storage;
    size_t length = get32(packet) & RTL_H2C_LENGTH_MASK;
    if (!length || length > RTL8922AE_FIRMWARE_CHUNK_BYTES ||
        !send_command(packet, physical, RTL_COMMAND_DESCRIPTOR + length, deadline) ||
        !rtl8922ae_sleep(RTL_POLL_NS, deadline)) {
      return false;
    }
  }
  *detail = enable_mask == B_FW_DOWNLOAD ?
      "RTL8922AE WCPU download/security completion unavailable" :
      "RTL8922AE BBMCU0 download/security completion unavailable";
  return download_complete(enable_mask, rtl8922ae_deadline(deadline, RTL_SECOND));
}

static bool download_firmware(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_plan *wcpu,
    const struct rtl8922ae_firmware_plan *bbmcu0, uint64_t deadline, const char **detail)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  *detail = "RTL8922AE firmware startup unavailable or already attempted";
  if (!transport.active || transport.fault || transport.stopping ||
      transport.download_attempted || !wcpu) {
    return false;
  }
  struct staged_image images[2] = {0};
  transport.next_slot = 0;
  *detail = "RTL8922AE combined firmware staging refused before CPU startup";
  if (!stage_image(asset, bytes, wcpu, 0, &images[0]) ||
      (bbmcu0 && !stage_image(asset, bytes, bbmcu0, 1, &images[1]))) {
    return false;
  }
  transport.download_attempted = true;
  *detail = "RTL8922AE WCPU startup unavailable";
  if (!rtl8922ae_operation_continue() || task_deadline_expired(deadline) ||
      !start_wcpu(bbmcu0 != NULL)) {
    return false;
  }
  rtl8922ae_write32(R_SECURE_MALLOC, RTL_SECURE_MALLOC);
  if (!download_image(&images[0], B_FW_DOWNLOAD, deadline, detail) ||
      (bbmcu0 && !download_image(&images[1], B_BBMCU0_DOWNLOAD, deadline, detail))) {
    return false;
  }
  *detail = "RTL8922AE FreeRTOS readiness/security acknowledgement unavailable";
  if (!rtl8922ae_sleep(5 * RTL_MS, deadline) ||
      !firmware_ready(rtl8922ae_deadline(deadline, RTL_SECOND))) {
    return false;
  }
  transport.h2c_sequence = 0;
  transport.firmware_ready = true;
  drain_receive();
  return !transport.fault;
}

bool rtl8922ae_transport_download_onephy(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_plan *wcpu,
    const struct rtl8922ae_firmware_plan *bbmcu0, uint64_t deadline, const char **detail)
{
  if (!bbmcu0) {
    *detail = "RTL8922AE BBMCU0 firmware plan unavailable";
    return false;
  }
  return download_firmware(asset, bytes, wcpu, bbmcu0, deadline, detail);
}

bool rtl8922ae_transport_h2c(uint8_t category, uint8_t class_id, uint8_t function,
    bool receive_ack, bool done_ack, const void *payload, size_t bytes,
    uint8_t *sequence, uint64_t deadline, const char **detail)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  *detail = "RTL8922AE runtime H2C admission refused";
  if (!transport.firmware_ready || !transport.active || transport.fault ||
      transport.stopping || transport.runtime_busy || category > RTL_H2C_CATEGORY_MASK ||
      class_id > RTL_H2C_CLASS_MASK ||
      bytes > RTL8922AE_H2C_PAYLOAD_BYTES || (bytes && !payload) ||
      task_deadline_expired(deadline) || !rtl8922ae_operation_continue()) {
    return false;
  }
  transport.runtime_busy = true;
  phys_addr_t physical;
  void *storage;
  size_t offset = RTL_HEADER_RESERVE + transport.firmware_slots * RTL_COMMAND_STRIDE;
  size_t total = RTL_H2C_HEADER_BYTES + bytes;
  if (!span(&transport.firmware, offset, RTL_COMMAND_DESCRIPTOR + total,
      &physical, &storage)) {
    transport.runtime_busy = false;
    return false;
  }
  uint8_t *packet = storage;
  uint8_t serial = transport.h2c_sequence++;
  memset(packet, 0, RTL_COMMAND_DESCRIPTOR + RTL_H2C_HEADER_BYTES);
  put32(packet, total | RTL_PACKET_H2C << 24);
  put32(packet + RTL_COMMAND_DESCRIPTOR, category | (uint32_t)class_id << 2 |
      (uint32_t)function << 8 | (uint32_t)serial << 24);
  put32(packet + RTL_COMMAND_DESCRIPTOR + 4, total |
      ((receive_ack || !(serial % 4)) ? RTL_H2C_RECEIVE_ACK : 0) |
      (done_ack ? RTL_H2C_DONE_ACK : 0));
  if (bytes) {
    memcpy(packet + RTL_COMMAND_DESCRIPTOR + RTL_H2C_HEADER_BYTES, payload, bytes);
  }
  if (sequence) {
    *sequence = serial;
  }
  *detail = "RTL8922AE runtime H2C submission/consumption uncertain";
  bool consumed = false;
  if (send_command(packet, physical, RTL_COMMAND_DESCRIPTOR + total, deadline)) {
    uint64_t local = rtl8922ae_deadline(deadline, RTL_SECOND);
    do {
      uint32_t index = rtl8922ae_read32(R_TX_INDEX + RTL_FW_CHANNEL * 4);
      if (!valid_register(index) || ((index >> 16) & B_RING_DEVICE_INDEX) >= RTL_RING_ENTRIES ||
          transport.fault) {
        break;
      }
      if (((index >> 16) & B_RING_DEVICE_INDEX) == transport.tx_write) {
        dma_read_barrier();
        consumed = true;
        break;
      }
    } while (rtl8922ae_sleep(RTL_POLL_NS, local));
  }
  /* An uncertain packet keeps its bytes immutable until terminal reset. */
  if (!consumed) {
    transport.fault = true;
    return false;
  }
  transport.runtime_busy = false;
  return true;
}

static bool pre_deinit(uint64_t deadline)
{
  if (!change32(R_BE_HCI_OPT_CTRL, BIT_WAKE_CTRL_V1, 0)) {
    return false;
  }
  uint32_t state = rtl8922ae_read32(R_BE_IC_PWR_STATE);
  if (!valid_register(state)) {
    return false;
  }
  if (!(state & B_BE_WLMAC_PWR_STE_MASK)) {
    return true;
  }
  if (!change32(R_DMA_CONFIG, B_TX_ENABLE | B_RX_ENABLE, B_STOP_MASTER)) {
    return false;
  }
  rtl8922ae_write32(R_TX_INDEX_CLEAR, B_STOP_CHANNELS);
  rtl8922ae_write32(R_RX_INDEX_CLEAR, 3);
  for (unsigned queue = 0; queue < RTL_RX_QUEUES; ++queue) {
    rtl8922ae_write16(R_RX_INDEX + queue * 4, RTL_RING_ENTRIES - 1);
  }
  return rtl8922ae_poll32(R_DMA_CONFIG, B_TX_ENABLE | B_RX_ENABLE | B_STOP_MASTER,
      B_STOP_MASTER, rtl8922ae_deadline(deadline, RTL_MS));
}

bool rtl8922ae_transport_stop(uint64_t deadline, const char **detail)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  if (transport.stopping) {
    return transport.stopped;
  }
  /* Withdraw IRQ/RX users first; shutdown polls must still run after a latched
   * startup fault. No submission or receive rearm occurs in this phase. */
  uint64_t flags = cpu_save_interrupts();
  transport.stopping = true;
  transport.interrupt_live = false;
  bool masked = pci_msi_disable(&transport.msi);
  mask_interrupts();
  transport.active = false;
  cpu_restore_interrupts(flags);
  transport.pre_deinit = pre_deinit(deadline);
  const char *power_detail = "RTL8922AE MAC power-off unconfirmed";
  transport.mac_off = rtl8922ae_hw_power_off(deadline, &power_detail);
  /* A failed or expired MAC shutdown never consumes the PCI-reset budget.
   * Keep every mapping/buffer/context even when either stop proof is missing. */
  bool reset = pci_function_reset_stop(&transport.reset, task_deadline_after_ms(RTL_RESET_MS));
  transport.stopped = masked && transport.pre_deinit && transport.mac_off && reset;
  if (!masked) {
    *detail = "RTL8922AE MSI mask unconfirmed; ownership retained";
  } else if (!transport.pre_deinit) {
    *detail = "RTL8922AE DMA pre-deinit unconfirmed; ownership retained";
  } else if (!transport.mac_off) {
    *detail = power_detail;
  } else if (!reset) {
    *detail = transport.reset.detail;
  }
  return transport.stopped;
}

void rtl8922ae_transport_status(struct wifi_status_reply *status)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
  if (transport.reset.prepared) {
    status->boot.valid |= WIFI_BOOT_RESET_VALID;
    status->boot.reset_method = transport.reset.method == PCI_RESET_FLR ?
      WIFI_RESET_FLR : WIFI_RESET_POWER;
    status->boot.pci_device_capabilities = transport.reset.device_capabilities;
    status->boot.pci_power_control = transport.initial_power_control;
  }
  if (transport.firmware_ready) status->boot.state |= WIFI_BOOT_FIRMWARE_READY;
  if (transport.pre_deinit) status->boot.state |= WIFI_BOOT_PRE_DEINIT;
  if (transport.mac_off) status->boot.state |= WIFI_BOOT_MAC_OFF;
  if (transport.reset.stop_completed) status->boot.state |= WIFI_BOOT_PCI_STOP;
  status->dma_bytes = transport.rings.bytes + transport.data_wd.bytes +
    transport.receive.bytes + transport.firmware.bytes;
  status->firmware_dma_bytes = transport.firmware.bytes;
  status->interrupts = transport.interrupts;
  status->received_packets = transport.received;
  status->rx_dropped = transport.dropped;
  if (transport.prepared) {
    status->rx_entries = RTL_RING_ENTRIES;
    status->rx_buffers = RTL_RX_QUEUES * RTL_RING_ENTRIES;
    status->command_entries = RTL_RING_ENTRIES;
    status->command_queues = 1;
  }
}
