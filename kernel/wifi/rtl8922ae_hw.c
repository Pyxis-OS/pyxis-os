/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89 mac.c/mac_be.c, efuse.c/efuse_be.c,
 * rtw8922a.c and register definitions at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, BSD alternative.
 * Complete notice: kernel/wifi/NOTICE.
 */
#include <stddef.h>
#include <kernel/memory.h>
#include <kernel/format.h>
#include "rtl8922ae_hw.h"
#include "rtl8922ae_io.h"
#include "rtl8922ae_regs_hw.h"

#define NS_PER_MS UINT64_C(1000000)
#define EFUSE_PHYSICAL_BYTES 0x1300u
#define EFUSE_RF_BYTES 0x240u
#define EFUSE_SECURITY_ADDRESS 0x1580u
#define EFUSE_TRIM_ADDRESS 0x1700u
#define EFUSE_TRIM_BYTES 0x38u
#define EFUSE_SYSTEM_ACTIVE 0x220u
#define EFUSE_RF_PAGE 1u
#define EFUSE_CONTROL_BYTES 4u
#define EFUSE_RF_XTAL_OFFSET 0xb9u
#define EFUSE_RF_RFE_OFFSET 0xcau
#define EFUSE_TSSI_A_OFFSET 0x10u
#define EFUSE_TSSI_PATH_BYTES 42u
#define EFUSE_THERMAL_A_OFFSET 0xd0u
#define EFUSE_GAIN_2G5G_A_OFFSET 0xd4u
#define EFUSE_GAIN_6G_A_OFFSET 0x1a8u
#define EFUSE_GAIN_2G5G_COUNT 5u
#define EFUSE_GAIN_6G_COUNT 8u
#define GAIN_OFFSET_SIGN UINT8_C(0x80)
#define GAIN_OFFSET_MAGNITUDE UINT8_C(0x7f)
#define DLE_RESERVED_WDE_PAGES 3328u
#define DLE_WDE_PAGE_BYTES 64u
#define DLE_BOUND_BYTES 8192u
#define DLE_PLE_LINKED_PAGES 2928u
#define DLE_WCPU_QUOTA 6u
#define HFC_COMMAND_PRECEDENCE 256u
#define PCI_ANALOG_TIMER 0x800u
#define PCI_MAC_TIMER 0x27100u
#define PCI_AUX_TIMER 0x1e848u
#define POWER_OFF_WAIT_NS (UINT64_C(3000) * NS_PER_MS)
#define POWER_OFF_LPS_OPTION UINT32_C(0x0000a1b2)
#define MAC_POWER_ACTIVE 1u
#define FW_GET_FEATURE 3u
#define FW_PHY_CAP 3u
#define FW_MAILBOX_WORDS 4u
#define FW_MAILBOX_BYTES 16u
#define FW_MAILBOX_REPLY_MS 1000u
#define FW_MAILBOX_FUNCTION_MASK UINT32_C(0x7f)
#define FW_MAILBOX_LENGTH_MASK UINT32_C(0xf00)
#define FW_PHY_CAP_RX_NSS_MASK UINT32_C(0xff0000)
#define FW_PHY_CAP_TX_NSS_MASK UINT32_C(0xff)
#define FW_PHY_CAP_TX_ANT_MASK UINT32_C(0xff00)
#define FW_PHY_CAP_RX_ANT_MASK UINT32_C(0xff0000)

static struct {
  uint8_t physical[EFUSE_PHYSICAL_BYTES];
  uint8_t rf[EFUSE_RF_BYTES];
  uint8_t trim[EFUSE_TRIM_BYTES];
} scratch;

static bool fail(const char **detail, const char *message)
{
  *detail = message;
  return false;
}

static uint32_t field(uint32_t mask, uint32_t value)
{
  return (value << __builtin_ctz(mask)) & mask;
}

static uint32_t extract(uint32_t mask, uint32_t value)
{
  return (value & mask) >> __builtin_ctz(mask);
}

static void change32(unsigned offset, uint32_t mask, uint32_t value)
{
  uint32_t old = rtl8922ae_read32(offset);
  rtl8922ae_write32(offset, (old & ~mask) | (value & mask));
}

static void set32(unsigned offset, uint32_t flags)
{
  change32(offset, flags, flags);
}

static void clear32(unsigned offset, uint32_t flags)
{
  change32(offset, flags, 0);
}

static void change16(unsigned offset, uint16_t mask, uint16_t value)
{
  uint16_t old = rtl8922ae_read16(offset);
  rtl8922ae_write16(offset, (old & ~mask) | (value & mask));
}

static void change8(unsigned offset, uint8_t mask, uint8_t value)
{
  uint8_t old = rtl8922ae_read8(offset);
  rtl8922ae_write8(offset, (old & ~mask) | (value & mask));
}

static void set8(unsigned offset, uint8_t flags)
{
  change8(offset, flags, flags);
}

static bool wait32(unsigned offset, uint32_t mask, uint32_t value,
    uint64_t interval, uint64_t deadline)
{
  return rtl8922ae_poll32(offset, mask, value,
      rtl8922ae_deadline(deadline, interval));
}

bool rtl8922ae_hw_xtal_write(uint8_t offset, uint8_t value, uint8_t mask,
    uint64_t deadline)
{
  uint32_t command = field(B_BE_WL_XTAL_SI_ADDR_MASK, offset) |
      field(B_BE_WL_XTAL_SI_DATA_MASK, value) |
      field(B_BE_WL_XTAL_SI_BITMASK_MASK, mask) | B_BE_WL_XTAL_SI_CMD_POLL;
  rtl8922ae_write32(R_BE_WLAN_XTAL_SI_CTRL, command);
  return wait32(R_BE_WLAN_XTAL_SI_CTRL, B_BE_WL_XTAL_SI_CMD_POLL, 0,
      50 * NS_PER_MS, deadline);
}

bool rtl8922ae_hw_xtal_read(uint8_t offset, uint8_t *value, uint64_t deadline)
{
  *value = 0;
  uint32_t command = field(B_BE_WL_XTAL_SI_ADDR_MASK, offset) |
      field(B_BE_WL_XTAL_SI_MODE_MASK, XTAL_SI_NORMAL_READ) | B_BE_WL_XTAL_SI_CMD_POLL;
  rtl8922ae_write32(R_BE_WLAN_XTAL_SI_CTRL, command);
  if (!wait32(R_BE_WLAN_XTAL_SI_CTRL, B_BE_WL_XTAL_SI_CMD_POLL, 0,
      50 * NS_PER_MS, deadline)) {
    return false;
  }
  uint32_t result = rtl8922ae_read32(R_BE_WLAN_XTAL_SI_CTRL);
  *value = extract(B_BE_WL_XTAL_SI_DATA_MASK, result);
  return true;
}

void rtl8922ae_hw_rfkill_prepare(void)
{
  change16(R_BE_GPIO8_15_FUNC_SEL, B_BE_PINMUX_GPIO9_FUNC_SEL_MASK,
          field(B_BE_PINMUX_GPIO9_FUNC_SEL_MASK, 15));
  change16(R_BE_GPIO_EXT_CTRL + 2, (B_BE_GPIO_MOD_9 | B_BE_GPIO_IO_SEL_9) >> 16, 0);
}

bool rtl8922ae_hw_rfkill(void)
{
  return !(rtl8922ae_read32(R_BE_GPIO_EXT_CTRL) & B_BE_GPIO_IN_9);
}

uint8_t rtl8922ae_hw_cut(void)
{
  return extract(B_AX_CHIP_VER_MASK, rtl8922ae_read32(R_AX_SYS_CFG1));
}

bool rtl8922ae_hw_power_on(uint8_t cut, uint64_t deadline, const char **detail)
{
  if (cut > 15) {
    return fail(detail, "RTL8922AE unsupported chip cut");
  }

  /* mac.c:rtw89_mac_power_switch rejects the active state only. This cut
   * still makes one attempt, without the wrapper's power-off/retry. */
  uint32_t power = rtl8922ae_read32(R_BE_IC_PWR_STATE);
  if (extract(B_BE_WLMAC_PWR_STE_MASK, power) == MAC_POWER_ACTIVE) {
    return fail(detail, "RTL8922AE MAC already powered on");
  }

  clear32(R_BE_SYS_PW_CTRL, B_BE_AFSM_WLSUS_EN |
      B_BE_AFSM_PCIE_SUS_EN);
  set32(R_BE_SYS_PW_CTRL, B_BE_DIS_WLBT_PDNSUSEN_SOPC);
  set32(R_BE_WLLPS_CTRL, B_BE_DIS_WLBT_LPSEN_LOPC);
  clear32(R_BE_SYS_PW_CTRL, B_BE_APDM_HPDN);
  clear32(R_BE_SYS_PW_CTRL, B_BE_APFM_SWLPS);

  if (!wait32(R_BE_SYS_PW_CTRL, B_BE_RDY_SYSPWR, B_BE_RDY_SYSPWR,
      3000 * NS_PER_MS, deadline)) {
    goto failed;
  }

  set32(R_BE_SYS_PW_CTRL, B_BE_EN_WLON);
  set32(R_BE_WLRESUME_CTRL, B_BE_LPSROP_CMAC0 |
      B_BE_LPSROP_CMAC1);
  set32(R_BE_SYS_PW_CTRL, B_BE_APFN_ONMAC);

  if (!wait32(R_BE_SYS_PW_CTRL, B_BE_APFN_ONMAC, 0,
      3000 * NS_PER_MS, deadline)) {
    goto failed;
  }

  clear32(R_BE_AFE_ON_CTRL1, B_BE_REG_CK_MON_CK960M_EN);
  set8(R_BE_ANAPAR_POW_MAC, B_BE_POW_PC_LDO_PORT0 |
      B_BE_POW_PC_LDO_PORT1);
  clear32(R_BE_FEN_RST_ENABLE, B_BE_R_SYM_ISO_ADDA_P02PP |
      B_BE_R_SYM_ISO_ADDA_P12PP);
  set8(R_BE_PLATFORM_ENABLE, B_BE_PLATFORM_EN);
  set32(R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_IO_EN);

  if (!wait32(R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_IO_ST, B_BE_HAXIDMA_IO_ST,
      3000 * NS_PER_MS, deadline)) {
    goto failed;
  }

  if (!wait32(R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_BACKUP_RESTORE_ST, 0,
      3000 * NS_PER_MS, deadline)) {
    goto failed;
  }

  set32(R_BE_HCI_OPT_CTRL, B_BE_HCI_WLAN_IO_EN);

  if (!wait32(R_BE_HCI_OPT_CTRL, B_BE_HCI_WLAN_IO_ST, B_BE_HCI_WLAN_IO_ST,
      3000 * NS_PER_MS, deadline)) {
    goto failed;
  }

  clear32(R_BE_SYS_SDIO_CTRL, B_BE_PCIE_FORCE_IBX_EN);

  if (!rtl8922ae_hw_xtal_write(XTAL_SI_PLL, 0x02, 0x02, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_PLL, 0x01, 0x01, deadline)) {
    goto failed;
  }

  set32(R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC1_1P3);

  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x40, 0x40, deadline)) {
    goto failed;
  }

  set32(R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC0_1P3);

  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x20, 0x20, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x04, 0x04, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x08, 0x08, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x10, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_WL_RFC_S0, 0xEB, 0xFF, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_WL_RFC_S1, 0xEB, 0xFF, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x01, 0x01, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x02, 0x02, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x80, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_XREF_RF1, 0, 0x40, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_XREF_RF2, 0, 0x40, deadline)) {
    goto failed;
  }
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_PLL_1, 0x40, 0x60, deadline)) {
    goto failed;
  }

  if (cut != 0) {
    set32(R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);
    set32(R_BE_SYS_ISO_CTRL, B_BE_ISO_EB2CORE);
    clear32(R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_B);

    if (!rtl8922ae_sleep(NS_PER_MS, deadline)) {
      goto failed;
    }

    clear32(R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_S);
    clear32(R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK);
  }

  set32(R_BE_DMAC_FUNC_EN, B_BE_MAC_FUNC_EN | B_BE_DMAC_FUNC_EN | B_BE_MPDU_PROC_EN |
      B_BE_WD_RLS_EN | B_BE_DLE_WDE_EN | B_BE_TXPKT_CTRL_EN |
      B_BE_STA_SCH_EN | B_BE_DLE_PLE_EN | B_BE_PKT_BUF_EN |
      B_BE_DMAC_TBL_EN | B_BE_PKT_IN_EN | B_BE_DLE_CPUIO_EN |
      B_BE_DISPATCHER_EN | B_BE_BBRPT_EN | B_BE_MAC_SEC_EN |
      B_BE_H_AXIDMA_EN | B_BE_DMAC_MLO_EN | B_BE_PLRLS_EN |
      B_BE_P_AXIDMA_EN | B_BE_DLE_DATACPUIO_EN | B_BE_LTR_CTL_EN);

  set32(R_BE_CMAC_SHARE_FUNC_EN, B_BE_CMAC_SHARE_EN | B_BE_RESPBA_EN | B_BE_ADDRSRCH_EN |
      B_BE_BTCOEX_EN);
  set32(R_BE_CMAC_FUNC_EN, B_BE_CMAC_EN | B_BE_CMAC_TXEN |  B_BE_CMAC_RXEN |
      B_BE_SIGB_EN | B_BE_PHYINTF_EN | B_BE_CMAC_DMA_EN |
      B_BE_PTCLTOP_EN | B_BE_SCHEDULER_EN | B_BE_TMAC_EN |
      B_BE_RMAC_EN | B_BE_TXTIME_EN | B_BE_RESP_PKTCTL_EN);

  set32(R_BE_FEN_RST_ENABLE, B_BE_FEN_BB_IP_RSTN |
      B_BE_FEN_BBPLAT_RSTB);

  return true;

failed:
  return fail(detail, "RTL8922AE chip power-on transaction failed");
}

bool rtl8922ae_hw_power_off(uint64_t deadline, const char **detail)
{
  /* rtw8922a_pwr_off_func runs even after a failed power-on. Do not infer
   * completion from a startup-success flag or retry a failed shutdown step. */
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x10, 0x10, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x08, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x04, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_WL_RFC_S0, 0xc6, 0xff, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_WL_RFC_S1, 0xc6, 0xff, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0x80, 0x80, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x02, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x01, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_PLL, 0x02, 0xff, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_PLL, 0, 0xff, deadline)) {
    return fail(detail, "RTL8922AE power-off XTAL transaction failed");
  }

  set32(R_BE_FEN_RST_ENABLE, B_BE_R_SYM_ISO_ADDA_P02PP | B_BE_R_SYM_ISO_ADDA_P12PP);
  change8(R_BE_ANAPAR_POW_MAC, B_BE_POW_PC_LDO_PORT0 | B_BE_POW_PC_LDO_PORT1, 0);
  set32(R_BE_SYS_PW_CTRL, B_BE_EN_WLON);
  change8(R_BE_FEN_RST_ENABLE, B_BE_FEN_BB_IP_RSTN | B_BE_FEN_BBPLAT_RSTB, 0);
  clear32(R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC0_1P3);
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x20, deadline)) {
    return fail(detail, "RTL8922AE power-off analog isolation or BB reset failed");
  }
  clear32(R_BE_SYS_ADIE_PAD_PWR_CTRL, B_BE_SYM_PADPDN_WL_RFC1_1P3);
  if (!rtl8922ae_hw_xtal_write(XTAL_SI_ANAPAR_WL, 0, 0x40, deadline)) {
    return fail(detail, "RTL8922AE power-off analog isolation or BB reset failed");
  }

  clear32(R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_IO_EN);
  if (!wait32(R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_IO_ST, 0, POWER_OFF_WAIT_NS, deadline)) {
    return fail(detail, "RTL8922AE power-off HAXI IO shutdown unconfirmed");
  }
  if (!wait32(R_BE_HCI_OPT_CTRL, B_BE_HAXIDMA_BACKUP_RESTORE_ST, 0,
          POWER_OFF_WAIT_NS, deadline)) {
    return fail(detail, "RTL8922AE power-off HAXI IO shutdown unconfirmed");
  }
  clear32(R_BE_HCI_OPT_CTRL, B_BE_HCI_WLAN_IO_EN);
  if (!wait32(R_BE_HCI_OPT_CTRL, B_BE_HCI_WLAN_IO_ST, 0, POWER_OFF_WAIT_NS, deadline)) {
    return fail(detail, "RTL8922AE power-off WLAN IO shutdown unconfirmed");
  }
  set32(R_BE_SYS_PW_CTRL, B_BE_APFM_OFFMAC);
  if (!wait32(R_BE_SYS_PW_CTRL, B_BE_APFM_OFFMAC, 0, POWER_OFF_WAIT_NS, deadline)) {
    return fail(detail, "RTL8922AE power-off MAC handshake unconfirmed");
  }

  rtl8922ae_write32(R_BE_WLLPS_CTRL, POWER_OFF_LPS_OPTION);
  set32(R_BE_SYS_PW_CTRL, B_BE_XTAL_OFF_A_DIE);
  set32(R_BE_SYS_PW_CTRL, B_BE_APFM_SWLPS);
  rtl8922ae_write32(R_BE_UDM1, 0);
  /* The PCIe power-switch wrapper's only additional register write on off.
   * This first cut never enters firmware power-save mode. */
  rtl8922ae_write8(R_AX_SCOREBOARD + 3, MAC_AX_NOTIFY_PWR_MAJOR);
  return true;
}

static bool efuse_state(bool idle, uint64_t deadline)
{
  change32(R_BE_WL_BT_PWR_CTRL, B_BE_BT_DISN_EN,
          idle ? B_BE_BT_DISN_EN : 0);
  if (idle) {
    return true;
  }
  return wait32(R_BE_IC_PWR_STATE, B_BE_WHOLE_SYS_PWR_STE_MASK,
      field(B_BE_WHOLE_SYS_PWR_STE_MASK, EFUSE_SYSTEM_ACTIVE), 5 * NS_PER_MS, deadline);
}

static bool efuse_power(bool enabled, uint8_t cut, uint64_t deadline)
{
  if (enabled) {
    change8(R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK,
            B_BE_SYSON_DIS_PMCR_BE_WRMSK);
    if (cut != 0) {
      change16(R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_S, B_BE_PWC_EV2EF_S);
      if (!rtl8922ae_sleep(NS_PER_MS, deadline)) {
        return false;
      }
      change16(R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_B, B_BE_PWC_EV2EF_B);
      change16(R_BE_SYS_ISO_CTRL, B_BE_ISO_EB2CORE, 0);
    }
    change32(R_BE_EFUSE_CTRL_2_V1, B_BE_EF_BURST, B_BE_EF_BURST);
    return true;
  }
  if (cut != 0) {
    change16(R_BE_SYS_ISO_CTRL, B_BE_ISO_EB2CORE, B_BE_ISO_EB2CORE);
    change16(R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_B, 0);
    if (!rtl8922ae_sleep(NS_PER_MS, deadline)) {
      return false;
    }
    change16(R_BE_SYS_ISO_CTRL, B_BE_PWC_EV2EF_S, 0);
  }
  change8(R_BE_PMC_DBG_CTRL2, B_BE_SYSON_DIS_PMCR_BE_WRMSK, 0);
  change32(R_BE_EFUSE_CTRL_2_V1, B_BE_EF_BURST, 0);
  return true;
}

static bool efuse_read(unsigned address, uint8_t *data, size_t bytes,
    uint8_t cut, uint64_t deadline)
{
  if ((address & 3) || (bytes & 3) || address > UINT16_MAX ||
      bytes > (size_t)UINT16_MAX + 1 - address) {
    return false;
  }
  (void)efuse_state(false, deadline);
  if (!efuse_power(true, cut, deadline)) return false;
  for (size_t i = 0; i < bytes; i += 4) {
    rtl8922ae_write32(R_BE_EFUSE_CTRL, field(B_BE_EF_ADDR_MASK, address + i));
    if (!wait32(R_BE_EFUSE_CTRL, B_BE_EF_RDY, B_BE_EF_RDY,
            1000 * NS_PER_MS, deadline)) {
      return false;
    }
    /* An acknowledged raw eFuse data word can legitimately be all ones. */
    uint32_t value = rtl8922ae_read32(R_BE_EFUSE_CTRL_1_V1);
    for (unsigned j = 0; j < 4; ++j) {
      data[i + j] = value >> (j * 8);
    }
    if ((i / 4 + 1) % 16 == 0 && !rtl8922ae_sleep(1, deadline)) {
      return false;
    }
  }
  return efuse_power(false, cut, deadline) && efuse_state(true, deadline);
}

bool rtl8922ae_hw_security(uint8_t bytes[4], uint8_t cut, uint64_t deadline,
    const char **detail)
{
  memset(bytes, 0, 4);
  if (!efuse_read(EFUSE_SECURITY_ADDRESS, bytes, 4, cut, deadline)) {
    memset(bytes, 0, 4);
    return fail(detail, "RTL8922AE security eFuse transaction failed");
  }
  /* Complete the power-switch wrapper after its security eFuse transaction. */
  rtl8922ae_write8(R_AX_SCOREBOARD + 3, MAC_AX_NOTIFY_TP_MAJOR);
  return true;
}

/* The worker copies detail after checked stop, so this storage must outlive it.
 * Fixed stage names below keep the complete message within status's 96 bytes. */
static char download_detail[96];

static bool download_failed(const char **detail, const char *stage, unsigned reg,
    uint32_t value, uint32_t mask, uint32_t expected)
{
  sprintf(download_detail, "RTL8922AE DMAC %s reg=%04x value=%08x mask=%08x want=%08x",
      stage, reg, value, mask, expected);
  return fail(detail, download_detail);
}

static bool download_wait(const char *stage, unsigned reg, uint32_t mask,
    uint64_t deadline, const char **detail)
{
  if (wait32(reg, mask, mask, 2 * NS_PER_MS, deadline)) {
    return true;
  }
  /* Snapshot before stop/reset, not a claim about the last polling sample. */
  return download_failed(detail, stage, reg, rtl8922ae_read32(reg), mask, mask);
}

static void quota(unsigned reg, uint32_t minimum_mask, uint32_t maximum_mask,
    unsigned minimum, unsigned maximum)
{
  rtl8922ae_write32(reg, field(minimum_mask, minimum) | field(maximum_mask, maximum));
}

bool rtl8922ae_hw_download_prepare(uint64_t deadline, const char **detail)
{
  change32(R_BE_HCI_FUNC_EN, B_BE_HCI_TXDMA_EN | B_BE_HCI_RXDMA_EN, B_BE_HCI_TXDMA_EN | B_BE_HCI_RXDMA_EN);
  change32(R_BE_HAXI_INIT_CFG1, B_BE_DMA_MODE_MASK, field(B_BE_DMA_MODE_MASK, S_BE_DMA_MOD_PCIE_NO_DATA_CPU));
  change32(R_BE_HAXI_DMA_STOP1, B_BE_TX_STOP1_MASK | B_BE_STOP_CH13 | B_BE_STOP_CH14, 0);
  change32(R_BE_DMAC_TABLE_CTRL, B_BE_DMAC_ADDR_MODE, B_BE_DMAC_ADDR_MODE);
  change32(R_BE_DMAC_FUNC_EN, B_BE_DLE_WDE_EN | B_BE_DLE_PLE_EN, 0);
  set32(R_BE_DMAC_CLK_EN, B_BE_DLE_WDE_CLK_EN | B_BE_DLE_PLE_CLK_EN);
  change32(R_BE_WDE_PKTBUF_CFG, B_BE_WDE_PAGE_SEL_MASK | B_BE_WDE_START_BOUND_MASK | B_BE_WDE_FREE_PAGE_NUM_MASK, field(B_BE_WDE_PAGE_SEL_MASK, S_AX_WDE_PAGE_SEL_64));
  change32(R_BE_PLE_PKTBUF_CFG, B_BE_PLE_PAGE_SEL_MASK | B_BE_PLE_START_BOUND_MASK | B_BE_PLE_FREE_PAGE_NUM_MASK, field(B_BE_PLE_PAGE_SEL_MASK, S_AX_PLE_PAGE_SEL_128) | field(B_BE_PLE_START_BOUND_MASK, DLE_RESERVED_WDE_PAGES * DLE_WDE_PAGE_BYTES / DLE_BOUND_BYTES) | field(B_BE_PLE_FREE_PAGE_NUM_MASK, DLE_PLE_LINKED_PAGES));
  quota(R_BE_WDE_QTA0_CFG, B_BE_WDE_Q0_MIN_SIZE_MASK, B_BE_WDE_Q0_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_WDE_QTA1_CFG, B_BE_WDE_Q1_MIN_SIZE_MASK, B_BE_WDE_Q1_MAX_SIZE_MASK, DLE_WCPU_QUOTA, DLE_WCPU_QUOTA);
  quota(R_BE_WDE_QTA2_CFG, B_BE_WDE_Q2_MIN_SIZE_MASK, B_BE_WDE_Q2_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_WDE_QTA3_CFG, B_BE_WDE_Q3_MIN_SIZE_MASK, B_BE_WDE_Q3_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_WDE_QTA4_CFG, B_BE_WDE_Q4_MIN_SIZE_MASK, B_BE_WDE_Q4_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA0_CFG, B_BE_PLE_Q0_MIN_SIZE_MASK, B_BE_PLE_Q0_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA1_CFG, B_BE_PLE_Q1_MIN_SIZE_MASK, B_BE_PLE_Q1_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA2_CFG, B_BE_PLE_Q2_MIN_SIZE_MASK, B_BE_PLE_Q2_MAX_SIZE_MASK, 32, 32);
  quota(R_BE_PLE_QTA3_CFG, B_BE_PLE_Q3_MIN_SIZE_MASK, B_BE_PLE_Q3_MAX_SIZE_MASK, 256, 256);
  quota(R_BE_PLE_QTA4_CFG, B_BE_PLE_Q4_MIN_SIZE_MASK, B_BE_PLE_Q4_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA5_CFG, B_BE_PLE_Q5_MIN_SIZE_MASK, B_BE_PLE_Q5_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA6_CFG, B_BE_PLE_Q6_MIN_SIZE_MASK, B_BE_PLE_Q6_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA7_CFG, B_BE_PLE_Q7_MIN_SIZE_MASK, B_BE_PLE_Q7_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA8_CFG, B_BE_PLE_Q8_MIN_SIZE_MASK, B_BE_PLE_Q8_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA9_CFG, B_BE_PLE_Q9_MIN_SIZE_MASK, B_BE_PLE_Q9_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA10_CFG, B_BE_PLE_Q10_MIN_SIZE_MASK, B_BE_PLE_Q10_MAX_SIZE_MASK, 1, 1);
  quota(R_BE_PLE_QTA11_CFG, B_BE_PLE_Q11_MIN_SIZE_MASK, B_BE_PLE_Q11_MAX_SIZE_MASK, 0, 0);
  quota(R_BE_PLE_QTA12_CFG, B_BE_PLE_Q12_MIN_SIZE_MASK, B_BE_PLE_Q12_MAX_SIZE_MASK, 0, 0);
  change32(R_BE_DMAC_FUNC_EN, B_BE_DLE_WDE_EN | B_BE_DLE_PLE_EN, B_BE_DLE_WDE_EN | B_BE_DLE_PLE_EN);
  if (!download_wait("wde-ready", R_AX_WDE_INI_STATUS, WDE_MGN_INI_RDY, deadline, detail)) {
    return false;
  }
  if (!download_wait("ple-ready", R_AX_PLE_INI_STATUS, PLE_MGN_INI_RDY, deadline, detail)) {
    return false;
  }
  change32(R_BE_HCI_FC_CTRL, B_BE_HCI_FC_EN | B_BE_HCI_FC_CH12_EN, 0);
  rtl8922ae_write32(R_BE_CH_PAGE_CTRL, field(B_BE_PREC_PAGE_CH12_V1_MASK, HFC_COMMAND_PRECEDENCE));
  change32(R_BE_HCI_FC_CTRL, B_BE_HCI_FC_EN | B_BE_HCI_FC_CH12_EN, B_BE_HCI_FC_CH12_EN);
  return true;
}

static bool decode_rf(void)
{
  memset(scratch.rf, UINT8_MAX, sizeof(scratch.rf));
  size_t cursor = EFUSE_CONTROL_BYTES;
  while (cursor < sizeof(scratch.physical)) {
    if (sizeof(scratch.physical) - cursor < 3) {
      return false;
    }
    const uint8_t *header = scratch.physical + cursor;
    if (header[0] == UINT8_MAX || header[1] == UINT8_MAX || header[2] == UINT8_MAX) {
      return true;
    }
    uint32_t value = ((uint32_t)header[0] << 16) | ((uint32_t)header[1] << 8) | header[2];
    unsigned page = (value >> 17) & 7;
    unsigned block = (value >> 4) & 0x1fff;
    unsigned absent_words = value & 15;
    cursor += 3;
    for (unsigned word = 0; word < 4; ++word) {
      if (absent_words & (1u << word)) {
        continue;
      }
      if (sizeof(scratch.physical) - cursor < 2) {
        return false;
      }
      unsigned logical = block * 8 + word * 2;
      if (page == EFUSE_RF_PAGE && logical < sizeof(scratch.rf)) {
        scratch.rf[logical] = scratch.physical[cursor];
        scratch.rf[logical + 1] = scratch.physical[cursor + 1];
      }
      cursor += 2;
    }
  }
  return true;
}

static bool phy_cap(struct rtl8922ae_board *board, uint64_t deadline)
{
  (void)efuse_state(false, deadline);
  if (!wait32(R_BE_H2CREG_CTRL, UINT8_MAX, 0, 5 * NS_PER_MS, deadline)) {
    return false;
  }
  /* First mailbox transaction after WCPU startup: part zero, two-byte
   * content plus two-byte mailbox header. No previous mailbox counters exist. */
  uint32_t command = FW_GET_FEATURE | field(FW_MAILBOX_LENGTH_MASK, 1);
  rtl8922ae_write32(R_BE_H2CREG_DATA0, command);
  rtl8922ae_write32(R_BE_H2CREG_DATA1, 0);
  rtl8922ae_write32(R_BE_H2CREG_DATA2, 0);
  rtl8922ae_write32(R_BE_H2CREG_DATA3, 0);
  change8(R_BE_UDM1 + 1, B_BE_UDM1_HALMAC_H2C_DEQ_CNT_MASK >> 8, 1);
  rtl8922ae_write8(R_BE_H2CREG_CTRL, B_AX_H2CREG_TRIGGER);
  uint64_t reply_deadline = rtl8922ae_deadline(deadline, FW_MAILBOX_REPLY_MS * NS_PER_MS);
  for (;;) {
    if (!rtl8922ae_sleep(0, reply_deadline)) return false;
    if (rtl8922ae_read8(R_BE_C2HREG_CTRL)) break;
    if (!rtl8922ae_sleep(NS_PER_US, reply_deadline)) return false;
  }
  uint32_t reply[FW_MAILBOX_WORDS];
  for (unsigned i = 0; i < FW_MAILBOX_WORDS; ++i) {
    reply[i] = rtl8922ae_read32(R_BE_C2HREG_DATA0 + i * sizeof(uint32_t));
  }
  rtl8922ae_write8(R_BE_C2HREG_CTRL, 0);
  unsigned bytes = extract(FW_MAILBOX_LENGTH_MASK, reply[0]) * sizeof(uint32_t);
  change8(R_BE_UDM1 + 1, B_BE_UDM1_HALMAC_C2H_ENQ_CNT_MASK >> 8, 1u << 4);
  (void)efuse_state(true, deadline);
  if (extract(FW_MAILBOX_FUNCTION_MASK, reply[0]) != FW_PHY_CAP) {
    return false;
  }
  if (bytes != FW_MAILBOX_BYTES) {
    return false;
  }
  board->rx_nss = extract(FW_PHY_CAP_RX_NSS_MASK, reply[0]);
  board->tx_nss = extract(FW_PHY_CAP_TX_NSS_MASK, reply[1]);
  board->tx_antennas = extract(FW_PHY_CAP_TX_ANT_MASK, reply[3]);
  board->rx_antennas = extract(FW_PHY_CAP_RX_ANT_MASK, reply[3]);
  /* This chip has two paths. Missing/unknown firmware capability is not a
   * request to substitute the chip maximum as Linux does. */
  return board->rx_nss >= 1 && board->rx_nss <= 2 &&
      board->tx_nss >= 1 && board->tx_nss <= 2 &&
      board->tx_antennas >= 1 && board->tx_antennas <= 2 &&
      board->rx_antennas >= 1 && board->rx_antennas <= 2;
}

bool rtl8922ae_hw_board(struct rtl8922ae_board *board, uint8_t cut,
    uint64_t deadline, const char **detail)
{
  struct rtl8922ae_board result = {0};
  *board = result;
  memset(&scratch, 0, sizeof(scratch));
  /* Linux records this validity bit and can substitute defaults later. Native
   * RF admission requires factory board/trim inputs, without that fallback. */
  if (!(rtl8922ae_read16(R_BE_SYS_WL_EFUSE_CTRL) & B_BE_AUTOLOAD_SUS)) {
    *detail = "RTL8922AE eFuse autoload unconfirmed";
    goto failed;
  }
  if (!efuse_read(0, scratch.physical, sizeof(scratch.physical), cut, deadline) ||
      !decode_rf() ||
      !efuse_read(EFUSE_TRIM_ADDRESS, scratch.trim, sizeof(scratch.trim), cut, deadline)) {
    *detail = "RTL8922AE board/trim eFuse unavailable or malformed";
    goto failed;
  }
  /* RF logical block layout: rtw8922a.h, 0x10000 page, 0x240 bytes. */
  result.country[0] = scratch.rf[0];
  result.country[1] = scratch.rf[1];
  result.xtal = scratch.rf[EFUSE_RF_XTAL_OFFSET];
  result.rfe = scratch.rf[EFUSE_RF_RFE_OFFSET];
  if (result.xtal == UINT8_MAX || result.rfe == UINT8_MAX) {
    *detail = "RTL8922AE board identity fields unprogrammed";
    goto failed;
  }
  /* Reference validity spans all 26 gain offsets, including bands not used
   * here. All-zero/all-ones means an unprogrammed optional adjustment. */
  bool gains_ff = true, gains_zero = true;
  for (unsigned path = 0; path < 2; ++path) {
    unsigned base_2g5g = EFUSE_GAIN_2G5G_A_OFFSET + EFUSE_GAIN_2G5G_COUNT * path;
    unsigned base_6g = EFUSE_GAIN_6G_A_OFFSET + EFUSE_GAIN_6G_COUNT * path;
    for (unsigned i = 0; i < EFUSE_GAIN_2G5G_COUNT + EFUSE_GAIN_6G_COUNT; ++i) {
      uint8_t value = scratch.rf[i < EFUSE_GAIN_2G5G_COUNT ? base_2g5g + i :
          base_6g + i - EFUSE_GAIN_2G5G_COUNT];
      gains_ff &= value == UINT8_MAX;
      gains_zero &= value == 0;
    }
    uint8_t ofdm = scratch.rf[base_2g5g], cck = scratch.rf[base_2g5g + 1];
    result.gain_offset_2g_ofdm[path] = ofdm & GAIN_OFFSET_SIGN ?
        (int8_t)((ofdm ^ GAIN_OFFSET_MAGNITUDE) + 1) : (int8_t)ofdm;
    result.gain_offset_2g_cck[path] = cck & GAIN_OFFSET_SIGN ?
        (int8_t)((cck ^ GAIN_OFFSET_MAGNITUDE) + 1) : (int8_t)cck;
  }
  result.gain_offset_known = true;
  result.gain_offset_present = !gains_ff && !gains_zero;
  for (unsigned path = 0; path < 2; ++path) {
    unsigned base = EFUSE_TSSI_A_OFFSET + EFUSE_TSSI_PATH_BYTES * path;
    memcpy(result.tssi_cck[path], scratch.rf + base, sizeof(result.tssi_cck[path]));
    memcpy(result.tssi_ofdm_2g[path], scratch.rf + base + sizeof(result.tssi_cck[path]),
        sizeof(result.tssi_ofdm_2g[path]));
    result.tssi_thermal[path] = scratch.rf[EFUSE_THERMAL_A_OFFSET + path];
  }
  result.tssi_known = true;
  result.thermal_trim_present = scratch.trim[6] != UINT8_MAX && scratch.trim[0x33] != UINT8_MAX;
  result.pa_bias_present = scratch.trim[0] != UINT8_MAX;
  for (unsigned i = 0; i < 2; ++i) {
    unsigned offset = i ? 0x33 : 6;
    if (result.thermal_trim_present) {
      int8_t value = scratch.trim[offset] & 0x3f;
      result.thermal_trim[i] = scratch.trim[offset] & 0x40 ? value : -value;
    }
    result.pa_bias[i] = scratch.trim[offset + 1];
    result.pad_bias[i] = scratch.trim[offset + 2];
  }
  result.rfkill = rtl8922ae_hw_rfkill();
  if (!phy_cap(&result, deadline)) {
    *detail = "RTL8922AE firmware PHY capability reply unavailable or unsupported";
    goto failed;
  }
  memset(&scratch, 0, sizeof(scratch));
  *board = result;
  return true;
failed:
  memset(&scratch, 0, sizeof(scratch));
  return false;
}

void rtl8922ae_hw_pci_prepare(void)
{
  /* Fixed 8922AE profile: recovery enabled, default ASIC timer scale one. */
  rtl8922ae_write32(R_BE_AON_WDT_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_MDIO_WDT_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_LA_MODE_WDT_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_WDT_AR_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_WDT_AW_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_WDT_W_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_WDT_B_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_WDT_R_TMR, PCI_ANALOG_TIMER);
  rtl8922ae_write32(R_BE_WLAN_WDT_TMR, PCI_MAC_TIMER);
  rtl8922ae_write32(R_BE_AXIDMA_WDT_TMR, PCI_MAC_TIMER);
  rtl8922ae_write32(R_BE_LOCAL_WDT_TMR, PCI_AUX_TIMER);
  change32(R_BE_HCI_OPT_CTRL, BIT_WAKE_CTRL_V1, BIT_WAKE_CTRL_V1);
}

void rtl8922ae_hw_pci_mode(void)
{
  uint32_t init = rtl8922ae_read32(R_BE_HAXI_INIT_CFG1);
  uint32_t append = rtl8922ae_read32(R_BE_RX_APPEND_MODE);
  uint32_t expansion = rtl8922ae_read32(R_BE_HAXI_EXP_CTRL_V1);
  uint32_t mask = B_BE_RXQ_RXBD_MODE_MASK | B_BE_MAX_TXDMA_MASK | B_BE_MAX_RXDMA_MASK |
      B_BE_CFG_WD_PERIOD_IDLE_MASK | B_BE_CFG_WD_PERIOD_ACTIVE_MASK;
  init = (init & ~mask) | field(B_BE_RXQ_RXBD_MODE_MASK, 0) |
      field(B_BE_MAX_TXDMA_MASK, MAC_AX_TX_BURST_V1_256B) |
      field(B_BE_MAX_RXDMA_MASK, MAC_AX_RX_BURST_V1_128B) |
      field(B_BE_CFG_WD_PERIOD_IDLE_MASK, MAC_AX_WD_DMA_INTVL_256NS) |
      field(B_BE_CFG_WD_PERIOD_ACTIVE_MASK, MAC_AX_WD_DMA_INTVL_256NS);
  expansion = (expansion & ~B_BE_MAX_TAG_NUM_MASK) |
      field(B_BE_MAX_TAG_NUM_MASK, MAC_AX_TAG_NUM_8);
  rtl8922ae_write32(R_BE_HAXI_INIT_CFG1, init);
  rtl8922ae_write32(R_BE_RX_APPEND_MODE, append);
  rtl8922ae_write32(R_BE_HAXI_EXP_CTRL_V1, expansion);
}

void rtl8922ae_hw_pci_finish(uint8_t cut)
{
  change32(R_BE_SYS_PAGE_CLK_GATED,
          B_BE_PCIE_PRST_DEBUNC_PERIOD_MASK | B_BE_SYM_PRST_DEBUNC_SEL,
          B_BE_SYM_PRST_DEBUNC_SEL);
  change32(R_BE_SYS_PW_CTRL, B_BE_PSUS_OFF_CAPC_EN, B_BE_PSUS_OFF_CAPC_EN);
  change32(R_BE_SYS_PAGE_CLK_GATED,
          B_BE_SOP_OFFPOOBS_PC | B_BE_CPHY_AUXCLK_OP | B_BE_CPHY_POWER_READY_CHK,
          B_BE_SOP_OFFPOOBS_PC | B_BE_CPHY_AUXCLK_OP | B_BE_CPHY_POWER_READY_CHK);
  change32(R_BE_SYS_SDIO_CTRL,
          B_BE_PCIE_FORCE_IBX_EN | B_BE_PCIE_DIS_L2_RTK_PERST | B_BE_PCIE_DIS_L2__CTRL_LDO_HCI, 0);
  change32(R_BE_L1_2_CTRL_HCILDO, B_BE_PCIE_DIS_L1_2_CTRL_HCILDO, 0);
  change32(R_BE_PCIE_FRZ_CLK, B_BE_PCIE_EN_AUX_CLK, B_BE_PCIE_EN_AUX_CLK);
  change32(R_BE_PCIE_PS_CTRL, B_BE_CMAC_EXIT_L1_EN, 0);
  if (cut != 0) {
    change32(R_BE_EFUSE_CTRL_2_V1, B_BE_R_SYM_AUTOLOAD_WITH_PMC_SEL,
           B_BE_R_SYM_AUTOLOAD_WITH_PMC_SEL);
    change32(R_BE_PCIE_LAT_CTRL, B_BE_SYM_AUX_CLK_SEL, B_BE_SYM_AUX_CLK_SEL);
  }
  rtl8922ae_write32(R_BE_PL1_DBG_INFO, 0);
  change32(R_BE_FWS1IMR, B_BE_PCIE_SER_TIMEOUT_INDIC_EN, B_BE_PCIE_SER_TIMEOUT_INDIC_EN);
  set32(R_BE_SER_PL1_CTRL, B_BE_PL1_SER_PL1_EN);
  change32(R_BE_SER_PL1_CTRL, B_BE_PL1_TIMER_UNIT_MASK,
          field(B_BE_PL1_TIMER_UNIT_MASK, 1));
  change32(R_BE_REG_PL1_MASK,
          B_BE_SER_PMU_IMR | B_BE_SER_L1SUB_IMR | B_BE_SER_PM_MASTER_IMR |
          B_BE_SER_LTSSM_IMR | B_BE_SER_PM_CLK_MASK | B_BE_SER_PCLKREQ_ACK_MASK,
          B_BE_SER_PMU_IMR | B_BE_SER_L1SUB_IMR | B_BE_SER_PM_MASTER_IMR |
          B_BE_SER_LTSSM_IMR | B_BE_SER_PM_CLK_MASK | B_BE_SER_PCLKREQ_ACK_MASK);
}
