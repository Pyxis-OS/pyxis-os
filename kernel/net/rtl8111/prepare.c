#include <arch/clock.h>
#include <arch/apic.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/pci.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/net/rtl8111.h>
#include <kernel/net/ethernet.h>
#include <kernel/panic.h>
#include "internal.h"
#include "io_setup.h"

static struct rtl8111_controller *controllers;
static bool inventory_complete;

void rtl_delay(uint64_t ns)
{
  uint64_t start = arch_monotonic_ns();
  while (arch_monotonic_ns() - start < ns) {
    __asm__ volatile("pause");
  }
}

static bool wait8(struct rtl8111_controller *controller, unsigned reg,
                  uint8_t mask, uint8_t expected, uint64_t timeout)
{
  uint64_t start = arch_monotonic_ns();
  do {
    uint8_t value = rtl_read8(controller, reg);
    if (value == UINT8_MAX) {
      return false;
    }
    if ((value & mask) == expected) {
      return true;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < timeout);
  return false;
}

static bool wait32(struct rtl8111_controller *controller, unsigned reg,
                   uint32_t mask, uint32_t expected, uint64_t timeout)
{
  uint64_t start = arch_monotonic_ns();
  do {
    uint32_t value = rtl_read32(controller, reg);
    if (value == UINT32_MAX) {
      return false;
    }
    if ((value & mask) == expected) {
      return true;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < timeout);
  return false;
}

uint16_t rtl_mac_read(struct rtl8111_controller *controller, unsigned address)
{
  rtl_write32(controller, RTL_MAC_OCP, address << RTL_OCP_ADDRESS_SHIFT);
  return rtl_read32(controller, RTL_MAC_OCP);
}

void rtl_mac_write(struct rtl8111_controller *controller, unsigned address, uint16_t value)
{
  rtl_write32(controller, RTL_MAC_OCP,
              RTL_INDIRECT_FLAG | (address << RTL_OCP_ADDRESS_SHIFT) | value);
}

void rtl_mac_modify(struct rtl8111_controller *controller, unsigned address,
                    uint16_t clear, uint16_t set)
{
  uint16_t value = rtl_mac_read(controller, address);
  rtl_mac_write(controller, address, (value & ~clear) | set);
}

static bool eri_clear(struct rtl8111_controller *controller, unsigned address, uint32_t mask)
{
  uint32_t command = RTL_ERI_ALL_BYTES | address;
  rtl_write32(controller, RTL_ERI_COMMAND, command);
  if (!wait32(controller, RTL_ERI_COMMAND, RTL_INDIRECT_FLAG,
               RTL_INDIRECT_FLAG, RTL_ERI_TIMEOUT_NS)) {
    return false;
  }
  uint32_t value = rtl_read32(controller, RTL_ERI_DATA);
  rtl_write32(controller, RTL_ERI_DATA, value & ~mask);
  rtl_write32(controller, RTL_ERI_COMMAND, command | RTL_INDIRECT_FLAG);
  if (!wait32(controller, RTL_ERI_COMMAND, RTL_INDIRECT_FLAG, 0, RTL_ERI_TIMEOUT_NS)) {
    return false;
  }
  rtl_write32(controller, RTL_ERI_COMMAND, command);
  return wait32(controller, RTL_ERI_COMMAND, RTL_INDIRECT_FLAG,
                 RTL_INDIRECT_FLAG, RTL_ERI_TIMEOUT_NS) &&
    !(rtl_read32(controller, RTL_ERI_DATA) & mask);
}

static const char *quiesce(struct rtl8111_controller *controller)
{
  rtl_write16(controller, RTL_INTERRUPT_MASK, 0);
  if (rtl_read16(controller, RTL_INTERRUPT_MASK)) {
    return "interrupt mask did not clear";
  }
  rtl_write16(controller, RTL_INTERRUPT_STATUS, UINT16_MAX);
  rtl_write32(controller, RTL_MISC, rtl_read32(controller, RTL_MISC) | RTL_MISC_RX_GATE);
  rtl_delay(RTL_RX_GATE_SETTLE_NS);
  if (!wait32(controller, RTL_TX_CONFIG, RTL_TX_FIFO_EMPTY,
                RTL_TX_FIFO_EMPTY, RTL_FIFO_TIMEOUT_NS)) {
    return "TX FIFO did not drain";
  }
  if (!wait8(controller, RTL_MCU, RTL_MCU_FIFO_EMPTY,
               RTL_MCU_FIFO_EMPTY, RTL_FIFO_TIMEOUT_NS)) {
    return "RX/TX FIFOs did not drain";
  }
  rtl_write8(controller, RTL_CHIP_COMMAND,
             rtl_read8(controller, RTL_CHIP_COMMAND) & ~(RTL_COMMAND_TX | RTL_COMMAND_RX));
  rtl_delay(RTL_STOP_SETTLE_NS);
  rtl_write8(controller, RTL_MCU, rtl_read8(controller, RTL_MCU) & ~RTL_MCU_OOB);
  rtl_mac_modify(controller, RTL_MAC_SHARED_FIFO, RTL_MAC_FIFO_CLEAR, 0);
  if (!wait8(controller, RTL_MCU, RTL_MCU_LINK_LIST_READY,
               RTL_MCU_LINK_LIST_READY, RTL_FIFO_TIMEOUT_NS)) {
    return "shared FIFO clear did not complete";
  }
  rtl_mac_modify(controller, RTL_MAC_SHARED_FIFO, 0, RTL_MAC_FIFO_SET);
  if (!wait8(controller, RTL_MCU, RTL_MCU_LINK_LIST_READY,
               RTL_MCU_LINK_LIST_READY, RTL_FIFO_TIMEOUT_NS)) {
    return "shared FIFO setup did not complete";
  }
  rtl_write8(controller, RTL_CHIP_COMMAND, RTL_COMMAND_RESET);
  if (!wait8(controller, RTL_CHIP_COMMAND,
               RTL_COMMAND_RESET | RTL_COMMAND_RX | RTL_COMMAND_TX, 0, RTL_RESET_TIMEOUT_NS)) {
    return "reset did not confirm a stopped controller";
  }
  controller->quiesced = true;
  return NULL;
}

static bool find_power_capabilities(struct rtl8111_controller *controller)
{
  struct pci_claim *claim = &controller->claim;
  controller->power_capability = controller->probe.power_capability;
  for (unsigned i = 0; i < claim->capability_count; ++i) {
    unsigned offset = claim->capabilities[i];
    if (pci_read8(claim->device->address, offset) == PCI_CAP_EXPRESS) {
      if (controller->express_capability ||
          !pci_capability_fits(claim, offset, PCI_EXPRESS_LINK_BYTES)) {
        return false;
      }
      controller->express_capability = offset;
    }
  }
  return controller->power_capability && controller->express_capability;
}

static bool pci_power_policy(struct rtl8111_controller *controller)
{
  struct pci_claim *claim = &controller->claim;
  struct pci_address address = claim->device->address;
  unsigned offset = controller->power_capability + PCI_POWER_CONTROL;
  uint16_t power = pci_read16(address, offset);
  pci_write16(claim, offset, power & ~(PCI_POWER_PME_ENABLE | PCI_POWER_PME_STATUS));
  power = pci_read16(address, offset);
  if (power & (PCI_POWER_STATE_MASK | PCI_POWER_PME_ENABLE)) {
    return false;
  }
  offset = controller->express_capability + PCI_EXPRESS_LINK_CONTROL;
  uint16_t link = pci_read16(address, offset);
  pci_write16(claim, offset, link & ~(PCI_EXPRESS_LINK_ASPM | PCI_EXPRESS_LINK_CLKREQ));
  link = pci_read16(address, offset);
  return !(link & (PCI_EXPRESS_LINK_ASPM | PCI_EXPRESS_LINK_CLKREQ));
}

static bool mac_power_policy(struct rtl8111_controller *controller)
{
  rtl_write8(controller, RTL_CONFIG_LOCK, RTL_CONFIG_UNLOCK);
  rtl_write8(controller, RTL_CONFIG2,
             rtl_read8(controller, RTL_CONFIG2) & ~(RTL_CONFIG2_CLKREQ | RTL_CONFIG2_PME));
  rtl_write8(controller, RTL_CONFIG3,
             rtl_read8(controller, RTL_CONFIG3) & ~(RTL_CONFIG3_LINK_WAKE | RTL_CONFIG3_L23_READY));
  rtl_write8(controller, RTL_CONFIG5, rtl_read8(controller, RTL_CONFIG5) &
             ~(RTL_CONFIG5_ASPM | RTL_CONFIG5_LAN_WAKE | RTL_CONFIG5_FRAME_WAKE));
  rtl_mac_modify(controller, RTL_MAC_L1_POWER, RTL_MAC_L1_POWER_MASK, 0);
  bool configured = !(rtl_read8(controller, RTL_CONFIG2) & (RTL_CONFIG2_CLKREQ | RTL_CONFIG2_PME)) &&
    !(rtl_read8(controller, RTL_CONFIG3) & (RTL_CONFIG3_LINK_WAKE | RTL_CONFIG3_L23_READY)) &&
    !(rtl_read8(controller, RTL_CONFIG5) &
       (RTL_CONFIG5_ASPM | RTL_CONFIG5_LAN_WAKE | RTL_CONFIG5_FRAME_WAKE));
  bool eri = eri_clear(controller, RTL_ERI_WAKE, RTL_ERI_MAGIC_WAKE) &&
    eri_clear(controller, RTL_ERI_EEE, RTL_ERI_EEE_ENABLE);
  rtl_write8(controller, RTL_DLL_POWER,
             rtl_read8(controller, RTL_DLL_POWER) & ~(RTL_DLL_PFM | RTL_DLL_10M_POWER_SAVE));
  rtl_write8(controller, RTL_MISC1, rtl_read8(controller, RTL_MISC1) & ~RTL_MISC1_D3_PFM);
  rtl_write8(controller, RTL_CONFIG_LOCK, 0);
  return configured && eri &&
    !(rtl_read8(controller, RTL_DLL_POWER) & (RTL_DLL_PFM | RTL_DLL_10M_POWER_SAVE)) &&
    !(rtl_read8(controller, RTL_MISC1) & RTL_MISC1_D3_PFM) &&
    !(rtl_mac_read(controller, RTL_MAC_L1_POWER) & RTL_MAC_L1_POWER_MASK);
}

static const char *prepare_controller(struct rtl8111_controller *controller,
                                      const struct boot_info *boot)
{
  struct pci_claim *claim = &controller->claim;
  if (!pci_begin_mmio_probe(claim, &controller->probe)) {
    return "initial power/decode state unavailable or inconsistent";
  }
  if (pci_map_bootstrap_bar(claim, RTL_REGISTER_BAR, boot, &controller->registers) != MM_OK) {
    return "cannot map provisional BAR2 registers";
  }
  uint32_t tx = rtl_read32(controller, RTL_TX_CONFIG);
  if (tx == UINT32_MAX) {
    return "register read failed";
  }
  controller->xid = (tx >> RTL_XID_SHIFT) & RTL_XID_MASK;
  controller->identified = true;
  if (controller->xid != RTL_XID_8168H) {
    return "unsupported XID; no variant-specific writes";
  }
  for (unsigned i = 0; i < sizeof(controller->mac); ++i) {
    controller->mac[i] = rtl_read8(controller, i);
  }
  controller->identity_known = net_ethernet_is_unicast(controller->mac);
  if (!controller->identity_known) {
    return "invalid boot-loaded MAC identity";
  }
  if (!find_power_capabilities(controller)) {
    return "missing or invalid power/PCIe capability";
  }

  controller->mutated = true;
  if (!pci_power_policy(controller)) {
    return "PCI power policy readback failed";
  }
  const char *failure = quiesce(controller);
  if (failure) {
    return failure;
  }
  if (!pci_complete_claim(claim)) {
    return "cannot confirm bus-master/INTx disable";
  }
  uint16_t command = pci_read16(claim->device->address, PCI_COMMAND);
  command &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
  command |= PCI_COMMAND_INTX_DISABLE;
  pci_write16(claim, PCI_COMMAND, command);
  if (pci_read16(claim->device->address, PCI_COMMAND) != command || !pci_size_bars(claim) ||
      claim->bars[RTL_REGISTER_BAR].bytes < PCI_BOOTSTRAP_BAR_BYTES) {
    return "invalid register BAR or decoding did not stop";
  }
  /* The assigned prefix now fits the measured BAR; retain its owned mapping. */
  if (!pci_msix_discover(claim, &controller->msix) ||
      (controller->msix.table.bar == RTL_REGISTER_BAR &&
       controller->msix.table.offset < PCI_BOOTSTRAP_BAR_BYTES) ||
      (controller->msix.pba.bar == RTL_REGISTER_BAR &&
       controller->msix.pba.offset < PCI_BOOTSTRAP_BAR_BYTES) ||
      pci_msix_map(&controller->msix, boot) != MM_OK) {
    return "invalid or unmappable MSI-X resources";
  }
  pci_write16(claim, PCI_COMMAND, command | PCI_COMMAND_MEMORY);
  if (pci_read16(claim->device->address, PCI_COMMAND) != (command | PCI_COMMAND_MEMORY) ||
      !pci_msix_disable(&controller->msix)) {
    return "cannot keep DMA/message delivery disabled";
  }
  rtl_write16(controller, RTL_INTERRUPT_MASK, 0);
  if (!mac_power_policy(controller)) {
    return "MAC power/wake preparation failed";
  }
  if (!rtl_phy_prepare(controller)) {
    return "PHY preparation failed";
  }
  if (rtl_read8(controller, RTL_CHIP_COMMAND) &
        (RTL_COMMAND_RX | RTL_COMMAND_TX | RTL_COMMAND_RESET) ||
      rtl_read16(controller, RTL_INTERRUPT_MASK)) {
    return "controller did not remain stopped";
  }
  if (rtl_ring_allocate(&controller->rx, true) != MM_OK ||
      rtl_ring_allocate(&controller->tx, false) != MM_OK) {
    return "cannot allocate RX/TX rings";
  }
  if (!rtl_io_prepare(controller, controller->rx.storage.physical,
                      controller->tx.storage.physical) ||
      !pci_msix_prepare(&controller->msix, APIC_RTL8111_VECTOR)) {
    return "MAC/ring or interrupt route preparation failed";
  }
  for (unsigned i = 0; i < sizeof(controller->mac); ++i) {
    if (rtl_read8(controller, i) != controller->mac[i]) {
      return "MAC identity changed during preparation";
    }
  }
  controller->prepared = true;
  return NULL;
}

static bool release_failed_controller(struct rtl8111_controller *controller)
{
  struct pci_claim *claim = &controller->claim;
  if (!controller->mutated) {
    if (!pci_restore_mmio_probe(claim, &controller->probe)) {
      return false;
    }
    pci_cancel_reservation(claim);
    return true;
  }
  if (!claim->reserved && controller->quiesced && !claim->dma_started &&
      (pci_read16(claim->device->address, PCI_COMMAND) &
       (PCI_COMMAND_MASTER | PCI_COMMAND_INTX_DISABLE)) == PCI_COMMAND_INTX_DISABLE) {
    if (controller->msix.table.mapping.address &&
        !pci_msix_disable(&controller->msix)) {
      return false;
    }
    for (unsigned i = 0; i < claim->capability_count; ++i) {
      unsigned offset = claim->capabilities[i];
      unsigned id = pci_read8(claim->device->address, offset);
      unsigned enable = id == PCI_CAP_MSI ? PCI_MSI_ENABLE :
        id == PCI_CAP_MSIX ? PCI_MSIX_ENABLE : 0;
      if (enable && (pci_read16(claim->device->address, offset + PCI_MSI_CONTROL) & enable)) {
        return false;
      }
    }
    pci_release_device(claim);
    return true;
  }
  return false;
}

void rtl8111_prepare(const struct boot_info *boot)
{
  KASSERT(cpu_current() == cpu_bsp() && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  inventory_complete = pci_inventory_state() == PCI_INVENTORY_COMPLETE;
  for (size_t index = 0; index < pci_device_count(); ++index) {
    arch_clock_maintain();
    const struct pci_device *device = pci_device_at(index);
    if (device->vendor_id != RTL_VENDOR_ID || device->device_id != RTL_DEVICE_ID) {
      continue;
    }
    struct pci_address address = device->address;
    struct rtl8111_controller *controller = kmalloc(sizeof(*controller));
    if (!controller) {
      inventory_complete = false;
      klog("rtl8111 %x:%x.%u: no memory for controller state\n",
           address.bus, address.device, address.function);
      continue;
    }
    *controller = (struct rtl8111_controller){0};
    if (!pci_reserve_device_at(index, &controller->claim)) {
      inventory_complete = false;
      klog("rtl8111 %x:%x.%u: cannot reserve function; unavailable\n",
           address.bus, address.device, address.function);
      kfree(controller);
      continue;
    }
    const char *failure = prepare_controller(controller, boot);
    if (failure) {
      klog("rtl8111 %x:%x.%u XID=%x: %s\n",
           address.bus, address.device, address.function, controller->xid, failure);
      if (release_failed_controller(controller)) {
        rtl_ring_release(&controller->tx);
        rtl_ring_release(&controller->rx);
        if (controller->xid != RTL_XID_8168H) {
          if (!controller->identified) {
            inventory_complete = false;
          }
          kfree(controller);
          continue;
        }
      } else {
        klog("rtl8111 %x:%x.%u: ownership retained until reboot\n",
             address.bus, address.device, address.function);
      }
      if (!controller->identified) {
        inventory_complete = false;
      }
    } else {
      klog("rtl8111 %x:%x.%u XID=%x: firmware-free PHY prepared; "
           "RX/TX, DMA and delivery disabled\n", address.bus, address.device,
           address.function, controller->xid);
    }
    controller->next = controllers;
    controllers = controller;
  }
}

bool rtl8111_inventory_complete(void)
{
  return inventory_complete;
}

struct rtl8111_controller *rtl8111_next(const struct rtl8111_controller *controller)
{
  struct rtl8111_controller *next = controller ? controller->next : controllers;
  while (next && next->xid != RTL_XID_8168H) {
    next = next->next;
  }
  return next;
}

struct rtl8111_controller *rtl8111_first(void)
{
  return rtl8111_next(NULL);
}

const uint8_t *rtl8111_identity_mac(const struct rtl8111_controller *controller)
{
  return controller && controller->identity_known ? controller->mac : NULL;
}
