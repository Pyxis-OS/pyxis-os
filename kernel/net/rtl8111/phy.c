#include <arch/clock.h>
#include "internal.h"

static bool wait_phy_access(struct rtl8111_controller *controller, bool read)
{
  uint64_t start = arch_monotonic_ns();
  do {
    uint32_t value = rtl_read32(controller, RTL_PHY_OCP);
    if (value == UINT32_MAX) {
      return false;
    }
    if (((value & RTL_INDIRECT_FLAG) != 0) == read) {
      return true;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < RTL_PHY_ACCESS_TIMEOUT_NS);
  return false;
}

bool rtl_phy_read(struct rtl8111_controller *controller, unsigned address, uint16_t *value)
{
  rtl_write32(controller, RTL_PHY_OCP, address << RTL_OCP_ADDRESS_SHIFT);
  if (!wait_phy_access(controller, true)) {
    return false;
  }
  *value = rtl_read32(controller, RTL_PHY_OCP);
  return true;
}

static bool phy_write(struct rtl8111_controller *controller, unsigned address, uint16_t value)
{
  rtl_write32(controller, RTL_PHY_OCP,
              RTL_INDIRECT_FLAG | (address << RTL_OCP_ADDRESS_SHIFT) | value);
  return wait_phy_access(controller, false);
}

static bool phy_modify(struct rtl8111_controller *controller, unsigned address,
                       uint16_t clear, uint16_t set)
{
  uint16_t value;
  return rtl_phy_read(controller, address, &value) &&
    phy_write(controller, address, (value & ~clear) | set);
}

static bool phy_parameter(struct rtl8111_controller *controller, uint16_t parameter,
                          uint16_t clear, uint16_t set)
{
  return phy_write(controller, RTL_PHY_PARAM_SELECT, parameter) &&
    phy_modify(controller, RTL_PHY_PARAM_DATA, clear, set);
}

static bool wait_reset(struct rtl8111_controller *controller)
{
  uint64_t start = arch_monotonic_ns();
  do {
    uint16_t control;
    if (!rtl_phy_read(controller, RTL_PHY_CONTROL, &control) || control == UINT16_MAX) {
      return false;
    }
    if (!(control & RTL_PHY_CONTROL_RESET)) {
      rtl_delay(RTL_PHY_RESET_SETTLE_NS);
      return true;
    }
    __asm__ volatile("pause");
  } while (arch_monotonic_ns() - start < RTL_PHY_RESET_TIMEOUT_NS);
  return false;
}

bool rtl_phy_prepare(struct rtl8111_controller *controller)
{
  uint16_t control;
  if (!rtl_phy_read(controller, RTL_PHY_CONTROL, &control) || control == UINT16_MAX ||
      !phy_write(controller, RTL_PHY_CONTROL, control & ~RTL_PHY_CONTROL_POWER_DOWN)) {
    return false;
  }
  rtl_delay(RTL_PHY_WAKE_SETTLE_NS);
  if (!wait_reset(controller)) {
    return false;
  }

  if (!phy_parameter(controller, RTL_PHY_ESTIMATOR_PARAM,
                       RTL_PHY_ESTIMATOR_MASK, RTL_PHY_ESTIMATOR_VALUE) ||
      !phy_parameter(controller, RTL_PHY_RETUNE_PARAM, 0, RTL_PHY_RETUNE_ENABLE) ||
      !phy_modify(controller, RTL_PHY_RETUNE_CONTROL, 0, RTL_PHY_RETUNE_START) ||
      !phy_modify(controller, RTL_PHY_10M_POWER, 0, RTL_PHY_10M_ENABLE)) {
    return false;
  }

  rtl_mac_write(controller, RTL_MAC_ADC_SELECT, RTL_ADC_SELECT_VALUE);
  uint16_t selector = rtl_mac_read(controller, RTL_MAC_ADC_SELECT);
  uint16_t sample = rtl_mac_read(controller, RTL_MAC_ADC_SAMPLE);
  uint16_t bias = ((sample >> 1) & RTL_ADC_BIAS_HIGH) | (sample & RTL_ADC_BIAS_LOW);
  if (selector & RTL_ADC_SIGN) {
    bias |= RTL_ADC_BIAS_SIGN;
  }
  if (bias != UINT16_MAX && !phy_write(controller, RTL_PHY_ADC_BIAS, bias)) {
    return false;
  }
  uint16_t lpf;
  if (!rtl_phy_read(controller, RTL_PHY_TX_LPF_SAMPLE, &lpf)) {
    return false;
  }
  lpf &= RTL_PHY_TX_LPF_MASK;
  lpf = lpf > RTL_PHY_TX_LPF_BIAS ? lpf - RTL_PHY_TX_LPF_BIAS : 0;
  uint16_t tune = lpf | (lpf << 4) | (lpf << 8) | (lpf << 12);
  if (!phy_write(controller, RTL_PHY_TX_LPF_TUNE, tune) ||
      !phy_modify(controller, RTL_PHY_10M_POWER, RTL_PHY_PFM, 0) ||
      !phy_modify(controller, RTL_PHY_POWER, RTL_PHY_PLL_OFF | RTL_PHY_ALDPS, 0) ||
      !phy_modify(controller, RTL_PHY_EEE_TUNE, 0, RTL_PHY_EEE_TUNE_ENABLE) ||
      !phy_modify(controller, RTL_PHY_CONTROL, RTL_PHY_CONTROL_ISOLATE,
                    RTL_PHY_CONTROL_RESET | RTL_PHY_CONTROL_AUTONEG | RTL_PHY_CONTROL_RESTART) ||
      !wait_reset(controller) ||
      !phy_write(controller, RTL_PHY_EEE_ADVERTISE, 0) ||
      !phy_modify(controller, RTL_PHY_CONTROL,
                    RTL_PHY_CONTROL_POWER_DOWN | RTL_PHY_CONTROL_ISOLATE,
                    RTL_PHY_CONTROL_AUTONEG | RTL_PHY_CONTROL_RESTART)) {
    return false;
  }

  uint16_t eee, power;
  return rtl_phy_read(controller, RTL_PHY_EEE_ADVERTISE, &eee) && !eee &&
    rtl_phy_read(controller, RTL_PHY_POWER, &power) &&
    !(power & (RTL_PHY_PLL_OFF | RTL_PHY_ALDPS)) &&
    rtl_phy_read(controller, RTL_PHY_10M_POWER, &power) && !(power & RTL_PHY_PFM) &&
    rtl_phy_read(controller, RTL_PHY_CONTROL, &control) &&
    (control & RTL_PHY_CONTROL_AUTONEG) &&
    !(control & (RTL_PHY_CONTROL_RESET | RTL_PHY_CONTROL_POWER_DOWN | RTL_PHY_CONTROL_ISOLATE));
}
