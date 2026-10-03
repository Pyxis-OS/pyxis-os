#include <arch/clock.h>
#include "internal.h"
#include "io_registers.h"
#include "io_setup.h"

static bool wait_access(struct rtl8111_controller *controller, unsigned reg,
                         bool read, uint64_t timeout)
{
  uint64_t start = arch_monotonic_ns();
  do {
    uint32_t value = rtl_read32(controller, reg);
    if (value == UINT32_MAX) {
      return false;
    }
    if (((value & RTL_INDIRECT_FLAG) != 0) == read) {
      return true;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < timeout);
  return false;
}

static bool ephy_write(struct rtl8111_controller *controller, unsigned address, uint16_t value)
{
  rtl_write32(controller, RTL_EPHY_COMMAND, RTL_INDIRECT_FLAG |
              (address << RTL_EPHY_ADDRESS_SHIFT) | value);
  if (!wait_access(controller, RTL_EPHY_COMMAND, false, RTL_EPHY_TIMEOUT_NS)) {
    return false;
  }
  rtl_delay(RTL_EPHY_SETTLE_NS);
  return true;
}

static bool ephy_modify(struct rtl8111_controller *controller, unsigned address,
                         uint16_t clear, uint16_t set)
{
  rtl_write32(controller, RTL_EPHY_COMMAND, address << RTL_EPHY_ADDRESS_SHIFT);
  if (!wait_access(controller, RTL_EPHY_COMMAND, true, RTL_EPHY_TIMEOUT_NS)) {
    return false;
  }
  uint16_t value = rtl_read32(controller, RTL_EPHY_COMMAND);
  return ephy_write(controller, address, (value & ~clear) | set);
}

static bool eri_read(struct rtl8111_controller *controller, unsigned address, uint32_t *value)
{
  rtl_write32(controller, RTL_ERI_COMMAND, RTL_ERI_ALL_BYTES | address);
  if (!wait_access(controller, RTL_ERI_COMMAND, true, RTL_ERI_TIMEOUT_NS)) {
    return false;
  }
  *value = rtl_read32(controller, RTL_ERI_DATA);
  return true;
}

static bool eri_write(struct rtl8111_controller *controller, unsigned address,
                       uint32_t bytes, uint32_t value)
{
  rtl_write32(controller, RTL_ERI_DATA, value);
  rtl_write32(controller, RTL_ERI_COMMAND, RTL_INDIRECT_FLAG | bytes | address);
  return wait_access(controller, RTL_ERI_COMMAND, false, RTL_ERI_TIMEOUT_NS);
}

static bool eri_modify(struct rtl8111_controller *controller, unsigned address,
                        uint32_t clear, uint32_t set)
{
  uint32_t value;
  return eri_read(controller, address, &value) &&
    eri_write(controller, address, RTL_ERI_ALL_BYTES, (value & ~clear) | set);
}

static bool configure_ephy(struct rtl8111_controller *controller)
{
  return ephy_modify(controller, RTL_EPHY_TUNE_1E,
                       RTL_EPHY_TUNE_1E_CLEAR, RTL_EPHY_TUNE_1E_SET) &&
    ephy_modify(controller, RTL_EPHY_TUNE_1D, 0, RTL_EPHY_TUNE_1D_SET) &&
    ephy_write(controller, RTL_EPHY_TUNE_05, RTL_EPHY_TUNE_05_VALUE) &&
    ephy_write(controller, RTL_EPHY_TUNE_06, RTL_EPHY_TUNE_06_VALUE) &&
    ephy_write(controller, RTL_EPHY_TUNE_04, RTL_EPHY_TUNE_04_VALUE) &&
    ephy_write(controller, RTL_EPHY_TUNE_01, RTL_EPHY_TUNE_01_VALUE);
}

static bool configure_fifo(struct rtl8111_controller *controller)
{
  return eri_write(controller, RTL_ERI_RX_FIFO, RTL_ERI_ALL_BYTES, RTL_ERI_RX_FIFO_8168H) &&
    eri_write(controller, RTL_ERI_TX_FIFO, RTL_ERI_ALL_BYTES, RTL_ERI_TX_FIFO_8168H) &&
    eri_write(controller, RTL_ERI_PAUSE_LOW, RTL_ERI_LOW_BYTE, RTL_ERI_PAUSE_LOW_8168H) &&
    eri_write(controller, RTL_ERI_PAUSE_HIGH, RTL_ERI_LOW_BYTE, RTL_ERI_PAUSE_HIGH_8168H) &&
    eri_modify(controller, RTL_ERI_FILTER_CONTROL, RTL_ERI_FILTER_RESET, 0) &&
    eri_modify(controller, RTL_ERI_FILTER_CONTROL, 0, RTL_ERI_FILTER_RESET) &&
    eri_modify(controller, RTL_ERI_FILTER_CONTROL, 0, RTL_ERI_FILTER_8168H) &&
    eri_write(controller, RTL_ERI_TUNE, RTL_ERI_LOW_HALF, RTL_ERI_TUNE_8168H);
}

static bool configure_mac(struct rtl8111_controller *controller)
{
  uint16_t count;
  if (!rtl_phy_read(controller, RTL_PHY_SAW_COUNT, &count) || count == UINT16_MAX) {
    return false;
  }
  count &= RTL_PHY_SAW_COUNT_MASK;
  if (count) {
    uint16_t timer = (RTL_MAC_SAW_CLOCK_HZ / count) & RTL_MAC_TIMER_MASK;
    rtl_mac_modify(controller, RTL_MAC_SAW_TIMER, RTL_MAC_TIMER_MASK, timer);
  }

  rtl_mac_modify(controller, RTL_MAC_TUNE_E056, RTL_MAC_TUNE_E056_CLEAR, 0);
  rtl_mac_modify(controller, RTL_MAC_TUNE_E052,
                   RTL_MAC_TUNE_E052_CLEAR, RTL_MAC_TUNE_E052_SET);
  rtl_mac_modify(controller, RTL_MAC_TUNE_E0D6,
                   RTL_MAC_TUNE_E0D6_CLEAR, RTL_MAC_TUNE_E0D6_SET);
  rtl_mac_modify(controller, RTL_MAC_TUNE_D420, RTL_MAC_TIMER_MASK, RTL_MAC_TUNE_D420_SET);
  rtl_mac_write(controller, RTL_MAC_TUNE_E63E, RTL_MAC_TUNE_E63E_START);
  rtl_mac_write(controller, RTL_MAC_TUNE_E63E, 0);
  rtl_mac_write(controller, RTL_MAC_TUNE_C094, 0);
  rtl_mac_write(controller, RTL_MAC_TUNE_C09E, 0);
  return !(rtl_mac_read(controller, RTL_MAC_TUNE_E056) & RTL_MAC_TUNE_E056_CLEAR) &&
    (rtl_mac_read(controller, RTL_MAC_TUNE_E052) &
       (RTL_MAC_TUNE_E052_CLEAR | RTL_MAC_TUNE_E052_SET)) == RTL_MAC_TUNE_E052_SET &&
    (rtl_mac_read(controller, RTL_MAC_TUNE_E0D6) & RTL_MAC_TUNE_E0D6_CLEAR) ==
      RTL_MAC_TUNE_E0D6_SET &&
    (rtl_mac_read(controller, RTL_MAC_TUNE_D420) & RTL_MAC_TIMER_MASK) ==
      RTL_MAC_TUNE_D420_SET &&
    !rtl_mac_read(controller, RTL_MAC_TUNE_C094) &&
    !rtl_mac_read(controller, RTL_MAC_TUNE_C09E);
}

bool rtl_io_prepare(struct rtl8111_controller *controller, phys_addr_t rx, phys_addr_t tx)
{
  rtl_write8(controller, RTL_CONFIG_LOCK, RTL_CONFIG_UNLOCK);
  bool configured = configure_ephy(controller) && configure_fifo(controller);
  if (configured) {
    rtl_write32(controller, RTL_MISC, rtl_read32(controller, RTL_MISC) & ~RTL_MISC_RX_GATE);
    configured = eri_write(controller, RTL_ERI_RX_CONTROL, RTL_ERI_LOW_HALF, 0) &&
      eri_write(controller, RTL_ERI_TX_CONTROL, RTL_ERI_LOW_HALF, 0) &&
      configure_mac(controller);
  }
  if (configured) {
    rtl_write16(controller, RTL_CPLUS_COMMAND, 0);
    rtl_write8(controller, RTL_TX_MAXIMUM, RTL_TX_EARLY_SIZE);
    rtl_write16(controller, RTL_INTERRUPT_MITIGATION, 0);
    /* Descriptor capacities bound DMA; oversize frames span descriptors and
     * the worker drops them. Retain the H reference's permissive length filter. */
    rtl_write16(controller, RTL_RX_MAXIMUM, RTL_RX_MAXIMUM_8168H);
    rtl_write32(controller, RTL_TX_RING_HIGH, tx >> 32);
    rtl_write32(controller, RTL_TX_RING_LOW, tx);
    rtl_write32(controller, RTL_RX_RING_HIGH, rx >> 32);
    rtl_write32(controller, RTL_RX_RING_LOW, rx);
    rtl_write32(controller, RTL_MULTICAST_HIGH, 0);
    rtl_write32(controller, RTL_MULTICAST_LOW, 0);
    rtl_write32(controller, RTL_RX_CONFIG, RTL_RX_CONFIG_8168H);
    /* TxConfig is programmed after TX enablement by the activation path. */
  }
  rtl_write8(controller, RTL_CONFIG_LOCK, 0);
  uint8_t command = rtl_read8(controller, RTL_CHIP_COMMAND);
  return configured && !(command & (RTL_COMMAND_RX | RTL_COMMAND_TX | RTL_COMMAND_RESET)) &&
    !rtl_read16(controller, RTL_INTERRUPT_MASK) &&
    (rtl_read32(controller, RTL_RX_CONFIG) &
       (RTL_RX_CONFIG_8168H | RTL_RX_ACCEPT_MASK)) == RTL_RX_CONFIG_8168H &&
    !rtl_read16(controller, RTL_CPLUS_COMMAND);
}
