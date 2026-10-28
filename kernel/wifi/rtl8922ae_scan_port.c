/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Linux v6.18 rtw89 mac.c/mac_be.c at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, BSD alternative.
 * Complete notice: kernel/wifi/NOTICE.
 */
#include <kernel/task.h>
#include "rtl8922ae_io.h"
#include "rtl8922ae_mac.h"
#include "rtl8922ae_regs_mac.h"

#define PORT_BEACON_INTERVAL 100u
#define PORT_BEACON_SETUP 2u
#define PORT_BEACON_HOLD 200u
#define PORT_TBTT_EARLY 5u
#define PORT_BEACON_EARLY 160u
#define PORT_BEACON_EARLY_US 20u
#define PHY_REPORT_EIGHT_BYTES 1u
#define MAC_BAND1_OFFSET 0x4000u
#define R_BE_HW_PPDU_STATUS 0x9c30u
#define B_BE_FWD_PPDU_STAT_MASK 0xffu
#define R_BE_PPDU_STAT 0x11440u
#define B_BE_PPDU_STAT_RPT_DMA (UINT32_C(1) << 6)
#define B_BE_PPDU_STAT_RPT_CRC32 (UINT32_C(1) << 5)
#define B_BE_APP_PLCP_HDR_RPT (UINT32_C(1) << 3)
#define B_BE_APP_RX_CNT_RPT (UINT32_C(1) << 2)
#define B_BE_PPDU_MAC_INFO (UINT32_C(1) << 1)
#define B_BE_PPDU_STAT_RPT_EN UINT32_C(1)
#define NS_PER_MS UINT64_C(1000000)
#define NS_PER_US UINT64_C(1000)

static uint32_t scan_filter;

static uint32_t field(uint32_t mask, uint32_t value)
{
  return (value << __builtin_ctz(mask)) & mask;
}

static void change32(unsigned reg, uint32_t clear, uint32_t set)
{
  uint32_t value = rtl8922ae_read32(reg);
  rtl8922ae_write32(reg, (value & ~clear) | set);
}

static void change16(unsigned reg, uint16_t clear, uint16_t set)
{
  uint16_t value = rtl8922ae_read16(reg);
  rtl8922ae_write16(reg, (value & ~clear) | set);
}

void rtl8922ae_mac_ppdu(bool enable)
{
  if (enable) change32(R_BE_HW_PPDU_STATUS, B_BE_FWD_PPDU_STAT_MASK, 3);
  for (unsigned phy = 0; phy < 2; ++phy) {
    unsigned reg = R_BE_PPDU_STAT + phy * MAC_BAND1_OFFSET;
    if (enable) {
      rtl8922ae_write32(reg, B_BE_PPDU_STAT_RPT_EN | B_BE_PPDU_MAC_INFO |
          B_BE_APP_RX_CNT_RPT | B_BE_APP_PLCP_HDR_RPT |
          B_BE_PPDU_STAT_RPT_CRC32 | B_BE_PPDU_STAT_RPT_DMA);
    } else {
      change32(reg, B_BE_PPDU_STAT_RPT_EN, 0);
    }
  }
}

bool rtl8922ae_mac_scan_prepare(uint64_t deadline, const char **detail)
{
  *detail = "RTL8922AE NOLINK port setup interrupted";
  /* Reference mac_port_cfg_func_sw, with no associated beacon interval. */
  if (rtl8922ae_read32(R_BE_PORT_CFG_P0) & B_AX_PORT_FUNC_EN) {
    if (!rtl8922ae_sleep(NS_PER_MS, deadline)) return false;
    change32(R_BE_PORT_CFG_P0, B_AX_PORT_FUNC_EN | B_AX_BRK_SETUP, 0);
    change32(R_BE_PORT_CFG_P0, 0, B_AX_TSFTR_RST);
    rtl8922ae_write32(R_BE_BCN_CNT_TMR_P0, 0);
  }
  /* Reference mac_port_update: unassociated station, port zero, no HE/BSS. */
  change32(R_BE_PORT_CFG_P0, B_AX_TXBCN_RPT_EN, 0);
  change32(R_BE_PORT_CFG_P0, B_AX_RXBCN_RPT_EN, 0);
  change32(R_BE_PORT_CFG_P0, B_AX_NET_TYPE_MASK, 0);
  change32(R_BE_PORT_CFG_P0, B_AX_TBTT_PROHIB_EN | B_AX_BRK_SETUP, 0);
  change32(R_BE_PORT_CFG_P0, B_AX_RX_BSSID_FIT_EN, 0);
  change32(R_BE_PORT_CFG_P0, B_AX_TSF_UDT_EN, 0);
  change32(R_BE_PORT_CFG_P0, B_AX_BCNTX_EN, 0);
  change32(R_BE_BCN_SPACE_CFG_P0, B_AX_BCN_SPACE_MASK,
      field(B_AX_BCN_SPACE_MASK, PORT_BEACON_INTERVAL));
  rtl8922ae_write8(R_BE_P0MB_HGQ_WINDOW_CFG_0, 0);
  uint8_t more = rtl8922ae_read8(R_BE_WMTX_MOREDATA_TSFT_STMP_CTL);
  rtl8922ae_write8(R_BE_WMTX_MOREDATA_TSFT_STMP_CTL, more | B_AX_UPD_HGQMD | B_AX_UPD_TIMIE);
  change16(R_BE_DTIM_CTRL_P0, B_AX_DTIM_NUM_MASK, 0);
  change32(R_BE_MBSSID_DROP_0, field(B_AX_PORT_DROP_4_0_MASK, 1) | 1u, 0);
  change32(R_BE_TBTT_PROHIB_P0, B_AX_TBTT_SETUP_MASK,
      field(B_AX_TBTT_SETUP_MASK, PORT_BEACON_SETUP));
  change32(R_BE_TBTT_PROHIB_P0, B_AX_TBTT_HOLD_MASK,
      field(B_AX_TBTT_HOLD_MASK, PORT_BEACON_HOLD));
  change32(R_BE_BCN_AREA_P0, B_AX_BCN_MSK_AREA_MASK, 0);
  change16(R_BE_TBTTERLYINT_CFG_P0, B_AX_TBTTERLY_MASK,
      field(B_AX_TBTTERLY_MASK, PORT_TBTT_EARLY));
  change32(R_BE_PTCL_BSS_COLOR_0, B_AX_BSS_COLOB_AX_PORT_0_MASK, 0);
  change32(R_BE_MBSSID_CTRL, B_AX_P0MB_ALL_MASK, 0);
  change32(R_BE_PORT_CFG_P0, 0, B_AX_PORT_FUNC_EN);
  if (!rtl8922ae_sleep(PORT_BEACON_EARLY_US * NS_PER_US, deadline)) return false;
  change32(R_BE_BCNERLYINT_CFG_P0, B_AX_BCNERLY_MASK,
      field(B_AX_BCNERLY_MASK, PORT_BEACON_EARLY));
  change32(R_BE_BCN_PSR_RPT_P0, B_AX_BCAID_P0_MASK, 0);
  /* The BE reference enables an eight-byte inline PHY report, no conversion. */
  change32(R_BE_RCR, B_BE_PHY_RPT_SZ_MASK,
      field(B_BE_PHY_RPT_SZ_MASK, PHY_REPORT_EIGHT_BYTES));
  change32(R_BE_RCR, B_BE_HDR_CNV_SZ_MASK, 0);
  change32(R_BE_DRV_INFO_OPTION, B_BE_DRV_INFO_PHYRPT_EN, B_BE_DRV_INFO_PHYRPT_EN);
  return rtl8922ae_operation_continue() && !task_deadline_expired(deadline);
}

void rtl8922ae_mac_scan_rx_sync(bool enable)
{
  change32(R_BE_PORT_CFG_P0, B_AX_TSF_UDT_EN, enable ? B_AX_TSF_UDT_EN : 0);
}

void rtl8922ae_mac_scan_filter(bool scanning)
{
  if (scanning) {
    scan_filter = rtl8922ae_read32(R_BE_RX_FLTR_OPT) & B_AX_RX_FLTR_CFG_MASK;
    change32(R_BE_RX_FLTR_OPT, B_AX_RX_FLTR_CFG_MASK,
        scan_filter & ~(B_AX_A_BCN_CHK_EN | B_AX_A_BC | B_AX_A_A1_MATCH));
  } else {
    change32(R_BE_RX_FLTR_OPT, B_AX_RX_FLTR_CFG_MASK, scan_filter);
  }
}
