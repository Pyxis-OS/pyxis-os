/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Native DBCC subset of Linux rtw89 mac.c/mac_be.c/fw.c/rtw8922a.c at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, BSD alternative.
 * See kernel/wifi/NOTICE for the complete notice.
 */
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/panic.h>
#include <kernel/format.h>
#include <kernel/task.h>
#include <abi/wifi.h>
#include "rtl8922ae_io.h"
#include "rtl8922ae_mac.h"
#include "rtl8922ae_regs_mac.h"
#include "rtl8922ae_transport.h"

#define MAC_COUNT 2u
#define MAC_BAND1_OFFSET 0x4000u
#define MAC_PLE_B0_TX_QUOTA 0u
#define MAC_PLE_CMAC0_RX_QUOTA 6u
#define MAC_US UINT64_C(1000)
#define MAC_MS UINT64_C(1000000)
#define MAC_WDE_PAGES 3328u
#define MAC_PLE_PAGES 2688u
#define MAC_PLE_START 212992u
#define MAC_DLE_BOUND_BYTES 8192u
#define MAC_CSI_PAGES 107u
#define MAC_MPDU_INFO_PAGES 2u
#define MAC_HFC_GROUP_PAGES 1651u
#define MAC_HFC_PUBLIC_PAGES 3302u
#define MAC_HFC_CHANNEL_MIN 2u
#define MAC_HFC_CHANNEL_MAX 1641u
#define MAC_HFC_COMMAND_PRECEDENCE 32u
#define MAC_HFC_DATA_PRECEDENCE 2u
#define MAC_HFC_CHANNELS 12u
#define MAC_RPR_AGGREGATE 30u
#define MAC_RPR_TIMEOUT 255u
#define MAC_RX_LENGTH_UNITS 23u
#define MAC_CHANNEL_COUNT 14u
#define H2C_CATEGORY_MAC 1u
#define H2C_CLASS_MEDIA_REPORT 8u
#define H2C_NOTIFY_DBCC 5u
#define H2C_CLASS_OFFLOAD 9u
#define H2C_OFLD_CFG 0x14u

static uint32_t field(uint32_t mask, uint32_t value)
{
  return (value << __builtin_ctz(mask)) & mask;
}

/* First failure survives cleanup and later register activity. No log producer,
 * allocation or register reread is needed to inspect a failed transaction. */
static struct mac_failure {
  const char *step, *kind;
  uint32_t reg, width, observed, expected, mask;
  bool valid;
} mac_failure;
static char mac_detail[WIFI_DETAIL_BYTES];
static bool cmac1_enabled;

static unsigned mac_reg(unsigned reg, unsigned mac_idx)
{
  return reg + mac_idx * MAC_BAND1_OFFSET;
}

static bool failure(const char *step, const char *kind, unsigned reg, unsigned width,
    uint32_t observed, uint32_t expected, uint32_t mask)
{
  if (!mac_failure.valid) {
    mac_failure = (struct mac_failure){step, kind, reg, width, observed, expected, mask, true};
    /* Bounded static step/kind names, 5-digit BAR offset and 8-digit values:
     * the longest current message is shorter than WIFI_DETAIL_BYTES. */
    sprintf(mac_detail, "RTL MAC %s %s r=%05x/%u got=%08x want=%08x m=%08x",
        step, kind, reg, width, observed, expected, mask);
  }
  return false;
}

static bool refused(const char **detail, const char *fallback)
{
  *detail = mac_failure.valid ? mac_detail : fallback;
  return false;
}

static void write32(unsigned reg, uint32_t value)
{
  rtl8922ae_write32(reg, value);
}

static void change32(unsigned reg, uint32_t clear, uint32_t set)
{
  uint32_t value = rtl8922ae_read32(reg);
  write32(reg, (value & ~clear) | set);
}

static void change16(unsigned reg, uint16_t clear, uint16_t set)
{
  uint16_t value = rtl8922ae_read16(reg);
  rtl8922ae_write16(reg, (value & ~clear) | set);
}

static void change8(unsigned reg, uint8_t clear, uint8_t set)
{
  uint8_t value = rtl8922ae_read8(reg);
  rtl8922ae_write8(reg, (value & ~clear) | set);
}

/* docs/wip/rtl8922ae-wifi.md forbids probe/data TX in this milestone. These
 * masks close scheduler and autonomous-response
 * gates: Linux reg.h names CTN_TXEN, RESP_PKTCTL, RESPBA, QOSNULL, CTS2SELF and
 * TRIGGER_SS; rtw8922a_pwr_on_func enables the response engines. Verify only
 * the controlled gate bits, since other bits can change independently. */
static bool gate32(const char *step, unsigned reg, uint32_t mask)
{
  change32(reg, mask, 0);
  uint32_t observed = rtl8922ae_read32(reg);
  return !(observed & mask) || failure(step, "gate", reg, 4, observed, 0, mask);
}

static bool gate8(const char *step, unsigned reg, uint8_t mask)
{
  change8(reg, mask, 0);
  uint8_t observed = rtl8922ae_read8(reg);
  return !(observed & mask) || failure(step, "gate", reg, 1, observed, 0, mask);
}

static bool poll(const char *step, unsigned reg, uint32_t mask, uint32_t expected,
    uint64_t interval, uint64_t deadline)
{
  uint32_t observed;
  bool sampled;
  if (rtl8922ae_poll32_observe(reg, mask, expected, rtl8922ae_deadline(deadline, interval),
      &observed, &sampled)) return true;
  return failure(step, sampled ? "poll" : "none", reg, 4, observed, expected, mask);
}

static void quota(unsigned reg, uint16_t minimum, uint16_t maximum)
{
  write32(reg, field(B_BE_WDE_Q0_MIN_SIZE_MASK, minimum) |
      field(B_BE_WDE_Q0_MAX_SIZE_MASK, maximum));
}

/* RTL8922A selects these same sizes/quotas for SCC and DBCC. */
static void dle_quota_init(void)
{
  quota(R_BE_WDE_QTA0_CFG, 3302, 3302);
  quota(R_BE_WDE_QTA1_CFG, 6, 6);
  quota(R_BE_WDE_QTA2_CFG, 0, 0);
  quota(R_BE_WDE_QTA3_CFG, 0, 0);
  quota(R_BE_WDE_QTA4_CFG, 20, 20);
  quota(R_BE_PLE_QTA0_CFG, 320, 320);
  quota(R_BE_PLE_QTA1_CFG, 320, 320);
  quota(R_BE_PLE_QTA2_CFG, 32, 32);
  quota(R_BE_PLE_QTA3_CFG, 16, 16);
  quota(R_BE_PLE_QTA4_CFG, 13, 1316);
  quota(R_BE_PLE_QTA5_CFG, 13, 1316);
  quota(R_BE_PLE_QTA6_CFG, 292, 1595);
  quota(R_BE_PLE_QTA7_CFG, 292, 1595);
  quota(R_BE_PLE_QTA8_CFG, 64, 1367);
  quota(R_BE_PLE_QTA9_CFG, 18, 1321);
  quota(R_BE_PLE_QTA10_CFG, 1, 1);
  quota(R_BE_PLE_QTA11_CFG, 4, 1307);
  quota(R_BE_PLE_QTA12_CFG, 0, 0);
}

static bool dle_init(uint64_t deadline)
{
  change32(R_BE_DMAC_FUNC_EN, B_BE_DLE_WDE_EN | B_BE_DLE_PLE_EN, 0);
  /* This clock register is inaccessible on the qualified device, as on the
   * reference. Preserve its exact RMW; WDE/PLE readiness proves the transition. */
  uint32_t clock = rtl8922ae_read32(R_BE_DMAC_CLK_EN);
  rtl8922ae_write32(R_BE_DMAC_CLK_EN, clock | B_BE_DLE_WDE_CLK_EN | B_BE_DLE_PLE_CLK_EN);
  change32(R_BE_WDE_PKTBUF_CFG,
      B_BE_WDE_PAGE_SEL_MASK | B_BE_WDE_START_BOUND_MASK | B_BE_WDE_FREE_PAGE_NUM_MASK,
      field(B_BE_WDE_PAGE_SEL_MASK, S_AX_WDE_PAGE_SEL_64) |
      field(B_BE_WDE_FREE_PAGE_NUM_MASK, MAC_WDE_PAGES));
  change32(R_BE_PLE_PKTBUF_CFG,
      B_BE_PLE_PAGE_SEL_MASK | B_BE_PLE_START_BOUND_MASK | B_BE_PLE_FREE_PAGE_NUM_MASK,
      field(B_BE_PLE_PAGE_SEL_MASK, S_AX_PLE_PAGE_SEL_128) |
      field(B_BE_PLE_START_BOUND_MASK, MAC_PLE_START / MAC_DLE_BOUND_BYTES) |
      field(B_BE_PLE_FREE_PAGE_NUM_MASK, MAC_PLE_PAGES));

  dle_quota_init();
  change32(R_BE_DMAC_FUNC_EN, 0, B_BE_DLE_WDE_EN | B_BE_DLE_PLE_EN);
  return poll("wde-ready", R_AX_WDE_INI_STATUS, WDE_MGN_INI_RDY, WDE_MGN_INI_RDY, 2 * MAC_MS, deadline) &&
      poll("ple-ready", R_AX_PLE_INI_STATUS, PLE_MGN_INI_RDY, PLE_MGN_INI_RDY, 2 * MAC_MS, deadline);
}

static void preload_init(unsigned mac_idx)
{
  unsigned cfg0 = mac_idx ? R_BE_TXPKTCTL_B1_PRELD_CFG0 : R_BE_TXPKTCTL_B0_PRELD_CFG0;
  unsigned cfg1 = mac_idx ? R_BE_TXPKTCTL_B1_PRELD_CFG1 : R_BE_TXPKTCTL_B0_PRELD_CFG1;
  unsigned entries = mac_idx ? PRELD_B1_ENT_NUM : PRELD_B0_ENT_NUM;
  change32(cfg0, B_BE_B0_PRELD_USEMAXSZ_MASK,
      field(B_BE_B0_PRELD_USEMAXSZ_MASK, entries * PRELD_AMSDU_SIZE) | B_BE_B0_PRELD_FEN);
  change32(cfg1,
      B_BE_B0_PRELD_NXT_TXENDWIN_MASK | B_BE_B0_PRELD_NXT_RSVMINSZ_MASK,
      field(B_BE_B0_PRELD_NXT_TXENDWIN_MASK, PRELD_NEXT_WND) |
      field(B_BE_B0_PRELD_NXT_RSVMINSZ_MASK, PRELD_AMSDU_SIZE));
}

static bool hfc_init(uint64_t deadline)
{
  change32(R_BE_HCI_FC_CTRL, B_BE_HCI_FC_EN | B_BE_HCI_FC_CH12_EN, 0);
  for (unsigned channel = 0; channel < MAC_HFC_CHANNELS; ++channel) {
    bool group_one = (channel >= 4 && channel <= 7) || channel >= 10;
    write32(R_BE_CH0_PAGE_CTRL + channel * 4,
        field(B_AX_MIN_PG_MASK, MAC_HFC_CHANNEL_MIN) |
        field(B_AX_MAX_PG_MASK, MAC_HFC_CHANNEL_MAX) | (group_one ? B_AX_GRP : 0));
  }
  write32(R_BE_PUB_PAGE_CTRL1, field(B_AX_PUBPG_G0_MASK, MAC_HFC_GROUP_PAGES) |
      field(B_AX_PUBPG_G1_MASK, MAC_HFC_GROUP_PAGES));
  write32(R_BE_WP_PAGE_CTRL2, 0);
  write32(R_BE_CH_PAGE_CTRL, field(B_BE_PREC_PAGE_CH011_V1_MASK, MAC_HFC_DATA_PRECEDENCE) |
      field(B_BE_PREC_PAGE_CH12_V1_MASK, MAC_HFC_COMMAND_PRECEDENCE));
  write32(R_BE_PUB_PAGE_CTRL2, field(B_BE_PUBPG_ALL_MASK, MAC_HFC_PUBLIC_PAGES));
  write32(R_BE_WP_PAGE_CTRL1, 0);
  change32(R_BE_HCI_FC_CTRL, B_BE_HCI_FC_MODE_MASK | B_BE_HCI_FC_WD_FULL_COND_MASK |
      B_BE_HCI_FC_CH12_FULL_COND_MASK | B_BE_HCI_FC_WP_CH07_FULL_COND_MASK |
      B_BE_HCI_FC_WP_CH811_FULL_COND_MASK, B_BE_HCI_FC_EN | B_BE_HCI_FC_CH12_EN);
  return rtl8922ae_sleep(10 * MAC_US, deadline);
}

static bool dmac_init(uint64_t deadline)
{
  if (!dle_init(deadline)) {
    return false;
  }
  preload_init(0);
  if (!hfc_init(deadline)) {
    return false;
  }
  change8(R_BE_SS_CTRL, 0, B_BE_SS_EN);
  if (!poll("ss-ready", R_BE_SS_CTRL, B_BE_SS_INIT_DONE, B_BE_SS_INIT_DONE, 2 * MAC_MS, deadline)) {
    return false;
  }
  change32(R_BE_SS_CTRL, B_BE_BAND_TRIG_EN | B_BE_BAND1_TRIG_EN, B_BE_WARM_INIT);
  change32(R_BE_MPDU_PROC, 0, B_BE_APPEND_FCS);
  /* The reference writes without a readback requirement. The MAC-only VFIO
   * capture reads MPDU_CUT_CTRL_EN clear immediately after this write. */
  rtl8922ae_write32(R_BE_CUT_AMSDU_CTRL, TRXCFG_MPDU_PROC_CUT_CTRL);
  change32(R_BE_HDR_SHCUT_SETTING, B_BE_TX_ADDR_MLD_TO_LIK,
      B_BE_TX_HW_SEQ_EN | B_BE_TX_HW_ACK_POLICY_EN | B_BE_TX_MAC_MPDU_PROC_EN);
  write32(R_BE_RX_HDRTRNS, TRXCFG_MPDU_PROC_RX_HDR_CONV);
  change32(R_BE_DISP_FWD_WLAN_0, B_BE_FWD_WLAN_CPU_TYPE_0_DATA_MASK |
      B_BE_FWD_WLAN_CPU_TYPE_0_MNG_MASK | B_BE_FWD_WLAN_CPU_TYPE_0_CTL_MASK |
      B_BE_FWD_WLAN_CPU_TYPE_1_MASK,
      field(B_BE_FWD_WLAN_CPU_TYPE_0_DATA_MASK, 1) | field(B_BE_FWD_WLAN_CPU_TYPE_0_MNG_MASK, 1) |
      field(B_BE_FWD_WLAN_CPU_TYPE_0_CTL_MASK, 1) | field(B_BE_FWD_WLAN_CPU_TYPE_1_MASK, 1));
  change32(R_BE_SEC_ENG_CTRL, 0, B_BE_CLK_EN_CGCMP | B_BE_CLK_EN_WAPI |
      B_BE_CLK_EN_WEP_TKIP | B_BE_SEC_TX_ENC | B_BE_SEC_RX_DEC | B_BE_MC_DEC |
      B_BE_BC_DEC | B_BE_BMC_MGNT_DEC | B_BE_UC_MGNT_DEC | B_BE_SEC_PRE_ENQUE_TX);
  change32(R_BE_SEC_MPDU_PROC, 0, B_BE_APPEND_ICV | B_BE_APPEND_MIC);
  change32(R_BE_TXPKTCTL_MPDUINFO_CFG, B_BE_MPDUINFO_PKTID_MASK | B_BE_MPDUINFO_B1_BADDR_MASK,
      field(B_BE_MPDUINFO_PKTID_MASK, MAC_PLE_PAGES) |
      field(B_BE_MPDUINFO_B1_BADDR_MASK, MPDU_INFO_B1_OFST) | B_BE_MPDUINFO_FEN);
  /* Table reset has a self-clearing trigger, not an ordinary RMW readback. */
  uint32_t mlo = rtl8922ae_read32(R_BE_MLO_INIT_CTL);
  rtl8922ae_write32(R_BE_MLO_INIT_CTL, mlo | B_BE_MLO_TABLE_REINIT);
  rtl8922ae_write32(R_BE_MLO_INIT_CTL, mlo & ~B_BE_MLO_TABLE_REINIT);
  if (!poll("mlo-ready", R_BE_MLO_INIT_CTL, B_BE_MLO_TABLE_INIT_DONE,
      B_BE_MLO_TABLE_INIT_DONE, MAC_MS, deadline)) {
    return false;
  }
  change32(R_BE_SS_CTRL, 0, B_BE_MLO_HW_CHGLINK_EN);
  change32(R_BE_CMAC_SHARE_ACQCHK_CFG_0, 0, B_BE_R_MACID_ACQ_CHK_EN);
  return true;
}

static void scheduler_init(unsigned mac_idx)
{
  write32(mac_reg(R_BE_HE_CTN_CHK_CCA_NAV, mac_idx), B_BE_HE_CTN_CHK_CCA_P20 | B_BE_HE_CTN_CHK_EDCCA_P20 |
      B_BE_HE_CTN_CHK_CCA_BITMAP | B_BE_HE_CTN_CHK_EDCCA_BITMAP | B_BE_HE_CTN_CHK_NO_GNT_WL |
      B_BE_HE_CTN_CHK_BASIC_NAV | B_BE_HE_CTN_CHK_INTRA_NAV | B_BE_HE_CTN_CHK_TX_NAV);
  write32(mac_reg(R_BE_HE_SIFS_CHK_CCA_NAV, mac_idx), B_BE_HE_SIFS_CHK_EDCCA_P20 |
      B_BE_HE_SIFS_CHK_EDCCA_BITMAP | B_BE_HE_SIFS_CHK_NO_GNT_WL);
  write32(mac_reg(R_BE_TB_CHK_CCA_NAV, mac_idx), B_BE_TB_CHK_EDCCA_BITMAP | B_BE_TB_CHK_NO_GNT_WL |
      B_BE_TB_CHK_BASIC_NAV);
  change32(mac_reg(R_BE_CCA_CFG_0, mac_idx), B_BE_NO_GNT_WL_EN, 0);
  change32(mac_reg(R_BE_PREBKF_CFG_0, mac_idx), B_BE_PREBKF_TIME_MASK, field(B_BE_PREBKF_TIME_MASK, SCH_PREBKF_24US));
  change32(mac_reg(R_BE_CTN_CFG_0, mac_idx), B_BE_PREBKF_TIME_NONAC_MASK,
      field(B_BE_PREBKF_TIME_NONAC_MASK, SCH_PREBKF_24US));
  change32(mac_reg(R_BE_EDCA_BCNQ_PARAM, mac_idx), B_BE_BCNQ_CW_MASK | B_BE_BCNQ_AIFS_MASK,
      field(B_BE_BCNQ_CW_MASK, 0x32) | field(B_BE_BCNQ_AIFS_MASK, BCN_IFS_25US));
}

static bool cam_init(unsigned mac_idx, uint64_t deadline)
{
  uint32_t cam = rtl8922ae_read32(mac_reg(R_BE_ADDR_CAM_CTRL, mac_idx));
  rtl8922ae_write32(mac_reg(R_BE_ADDR_CAM_CTRL, mac_idx), (cam & ~B_BE_ADDR_CAM_RANGE_MASK) |
      field(B_BE_ADDR_CAM_RANGE_MASK, ADDR_CAM_SERCH_RANGE) | B_BE_ADDR_CAM_EN |
      (mac_idx == 0 ? B_BE_ADDR_CAM_CLR : 0));
  if (!poll("addr_cam_ctr", mac_reg(R_BE_ADDR_CAM_CTRL, mac_idx), B_BE_ADDR_CAM_CLR, 0, 2 * MAC_MS, deadline)) {
    return false;
  }
  return true;
}

static void receive_filter_init(unsigned mac_idx)
{
  write32(mac_reg(R_BE_MGNT_FLTR, mac_idx), RX_FLTR_FRAME_ACCEPT_BE);
  write32(mac_reg(R_BE_CTRL_FLTR, mac_idx), RX_FLTR_FRAME_ACCEPT_BE);
  write32(mac_reg(R_BE_DATA_FLTR, mac_idx), RX_FLTR_FRAME_ACCEPT_BE);
  write32(mac_reg(R_BE_RX_FLTR_OPT, mac_idx), B_BE_A_BC_CAM_MATCH | B_BE_A_UC_CAM_MATCH | B_BE_A_MC |
      B_BE_A_BC | B_BE_A_A1_MATCH | B_BE_SNIFFER_MODE | field(B_BE_UID_FILTER_MASK, 15));
  change16(mac_reg(R_BE_PLCP_HDR_FLTR, mac_idx), UINT16_MAX, B_BE_HE_SIGB_CRC_CHK |
      B_BE_VHT_MU_SIGB_CRC_CHK | B_BE_VHT_SU_SIGB_CRC_CHK | B_BE_SIGA_CRC_CHK |
      B_BE_LSIG_PARITY_CHK_EN | B_BE_CCK_SIG_CHK | B_BE_CCK_CRC_CHK);
  change32(mac_reg(R_BE_WMAC_NAV_CTL, mac_idx), B_BE_WMAC_PLCP_UP_NAV_EN | B_BE_WMAC_NAV_UPPER_MASK,
      B_BE_WMAC_TF_UP_NAV_EN | B_BE_WMAC_NAV_UPPER_EN | field(B_BE_WMAC_NAV_UPPER_MASK, NAV_25MS));
  change8(mac_reg(R_BE_RX_SR_CTRL, mac_idx), B_BE_SR_EN | B_BE_SR_CTRL_PLCP_EN, 0);
  change8(mac_reg(R_BE_BSSID_SRC_CTRL, mac_idx), 0, B_BE_PLCP_SRC_EN);
}

static void tmac_init(unsigned mac_idx)
{
  uint32_t response1 = rtl8922ae_read32(mac_reg(R_BE_TRXPTCL_RESP_1, mac_idx));
  uint32_t rates1 = rtl8922ae_read32(mac_reg(R_BE_PTCL_RRSR1, mac_idx));
  uint32_t rates0 = rtl8922ae_read32(mac_reg(R_BE_PTCL_RRSR0, mac_idx));
  uint32_t chip = rtl8922ae_read32(R_AX_SYS_CFG1);
  response1 &= B_BE_FTM_RRSR_RATE_EN_MASK | B_BE_WMAC_RESP_DOPPLEB_BE_EN |
      B_BE_WMAC_RESP_DCM_EN | B_BE_WMAC_RESP_REF_RATE_MASK;
  response1 &= ~B_BE_WMAC_RESP_REF_RATE_SEL;
  rates1 &= B_BE_RRSR_RATE_EN_MASK | B_BE_RRSR_CCK_MASK | B_BE_RSC_MASK;
  rates0 &= B_BE_RRSR_OFDM_MASK | B_BE_RRSR_HT_MASK | B_BE_RRSR_VHT_MASK | B_BE_RRSR_HE_MASK;
  if (!(chip & B_AX_CHIP_VER_MASK)) {
    rates1 = (rates1 & ~B_BE_RSC_MASK) | field(B_BE_RSC_MASK, 1);
  }
  change32(mac_reg(R_BE_TB_PPDU_CTRL, mac_idx), B_BE_QOSNULL_UPD_MUEDCA_EN, 0);
  change32(mac_reg(R_BE_WMTX_TCR_BE_4, mac_idx), B_BE_EHT_HE_PPDU_4XLTF_ZLD_USTIMER_MASK |
      B_BE_EHT_HE_PPDU_2XLTF_ZLD_USTIMER_MASK,
      field(B_BE_EHT_HE_PPDU_4XLTF_ZLD_USTIMER_MASK, 0x12) |
      field(B_BE_EHT_HE_PPDU_2XLTF_ZLD_USTIMER_MASK, 0xe));
  change32(mac_reg(R_BE_MAC_LOOPBACK, mac_idx), B_BE_MACLBK_PLCP_DLY_MASK | B_BE_MACLBK_EN,
      field(B_BE_MACLBK_PLCP_DLY_MASK, S_BE_MACLBK_PLCP_DLY_DEF));
  change32(mac_reg(R_BE_TRXPTCL_RESP_0, mac_idx), B_BE_WMAC_SPEC_SIFS_CCK_MASK | B_BE_WMAC_SPEC_SIFS_OFDM_MASK,
      field(B_BE_WMAC_SPEC_SIFS_CCK_MASK, WMAC_SPEC_SIFS_CCK) |
      field(B_BE_WMAC_SPEC_SIFS_OFDM_MASK, WMAC_SPEC_SIFS_OFDM_1115E));
  change32(mac_reg(R_BE_WMAC_ACK_BA_RESP_LEGACY, mac_idx), B_BE_ACK_BA_RESP_LEGACY_CHK_EDCCA, 0);
  change32(mac_reg(R_BE_WMAC_ACK_BA_RESP_HE, mac_idx), B_BE_ACK_BA_RESP_HE_CHK_EDCCA, 0);
  change32(mac_reg(R_BE_WMAC_ACK_BA_RESP_EHT_LEG_PUNC, mac_idx), B_BE_ACK_BA_EHT_LEG_PUNC_CHK_EDCCA, 0);
  change32(mac_reg(R_BE_RXTRIG_TEST_USER_2, mac_idx), 0, B_BE_RXTRIG_FCSCHK_EN);
  write32(mac_reg(R_BE_TRXPTCL_RESP_1, mac_idx), response1);
  write32(mac_reg(R_BE_PTCL_RRSR1, mac_idx), rates1);
  write32(mac_reg(R_BE_PTCL_RRSR0, mac_idx), rates0);
}

static void rmac_init(unsigned mac_idx)
{
  change16(mac_reg(R_BE_DLK_PROTECT_CTL, mac_idx), B_BE_RX_DLK_DATA_TIME_MASK | B_BE_RX_DLK_CCA_TIME_MASK,
      field(B_BE_RX_DLK_DATA_TIME_MASK, TRXCFG_RMAC_DATA_TO) |
      field(B_BE_RX_DLK_CCA_TIME_MASK, TRXCFG_RMAC_CCA_TO) | B_BE_RX_DLK_RST_EN);
  change32(mac_reg(R_BE_RX_FLTR_OPT, mac_idx), B_BE_RX_MPDU_MAX_LEN_MASK,
      field(B_BE_RX_MPDU_MAX_LEN_MASK, MAC_RX_LENGTH_UNITS));
  change8(mac_reg(R_BE_PLCP_HDR_FLTR, mac_idx), B_BE_VHT_SU_SIGB_CRC_CHK, 0);
  change16(mac_reg(R_BE_RCR, mac_idx), 0, B_BE_BUSY_CHKSN);
  change16(mac_reg(R_BE_RX_PLCP_EXT_OPTION_1, mac_idx), 0, B_BE_PLCP_SU_PSDU_LEN_SRC);
}

static void protocol_init(unsigned mac_idx)
{
  change32(mac_reg(R_BE_RESP_CSI_RESERVED_PAGE, mac_idx),
      B_BE_CSI_RESERVED_START_PAGE_MASK | B_BE_CSI_RESERVED_PAGE_NUM_MASK,
      field(B_BE_CSI_RESERVED_START_PAGE_MASK, MAC_PLE_PAGES + MAC_MPDU_INFO_PAGES + mac_idx * MAC_CSI_PAGES) |
      field(B_BE_CSI_RESERVED_PAGE_NUM_MASK, MAC_CSI_PAGES));
  change32(mac_reg(R_BE_TX_SUB_BAND_VALUE, mac_idx), B_BE_TXSB_20M_MASK | B_BE_TXSB_40M_MASK |
      B_BE_TXSB_80M_MASK | B_BE_TXSB_160M_MASK,
      field(B_BE_TXSB_20M_MASK, mac_idx ? S_BE_TXSB_20M_2 : S_BE_TXSB_20M_8) |
      field(B_BE_TXSB_40M_MASK, mac_idx ? S_BE_TXSB_40M_1 : S_BE_TXSB_40M_4) |
      field(B_BE_TXSB_80M_MASK, mac_idx ? S_BE_TXSB_80M_0 : S_BE_TXSB_80M_2) |
      field(B_BE_TXSB_160M_MASK, mac_idx ? S_BE_TXSB_160M_0 : S_BE_TXSB_160M_1));
  change32(mac_reg(R_BE_SIFS_SETTING, mac_idx), B_BE_HW_CTS2SELF_PKT_LEN_TH_MASK |
      B_BE_HW_CTS2SELF_PKT_LEN_TH_TWW_MASK,
      field(B_BE_HW_CTS2SELF_PKT_LEN_TH_MASK, S_AX_CTS2S_TH_1K) |
      field(B_BE_HW_CTS2SELF_PKT_LEN_TH_TWW_MASK, S_AX_CTS2S_TH_SEC_256B) | B_BE_HW_CTS2SELF_EN);
  change32(mac_reg(R_BE_PTCL_FSM_MON, mac_idx), B_BE_PTCL_TX_ARB_TO_THR_MASK | B_BE_PTCL_TX_ARB_TO_MODE,
      field(B_BE_PTCL_TX_ARB_TO_THR_MASK, S_AX_PTCL_TO_2MS));
  change8(mac_reg(R_BE_PTCL_COMMON_SETTING_0, mac_idx),
      B_BE_PTCL_TRIGGER_SS_EN_0 | B_BE_PTCL_TRIGGER_SS_EN_1 | B_BE_PTCL_TRIGGER_SS_EN_UL,
      B_BE_CMAC_TX_MODE_0 | B_BE_CMAC_TX_MODE_1);
  change32(mac_reg(R_BE_AMPDU_AGG_LIMIT, mac_idx), B_BE_AMPDU_MAX_TIME_MASK,
      field(B_BE_AMPDU_MAX_TIME_MASK, AMPDU_MAX_TIME));
  change32(mac_reg(R_BE_RX_CTRL_1, mac_idx), B_BE_RXDMA_TXRPT_QUEUE_ID_SW_MASK | B_BE_RXDMA_F2PCMDRPT_QUEUE_ID_SW_MASK,
      field(B_BE_RXDMA_TXRPT_QUEUE_ID_SW_MASK, WLCPU_RXCH2_QID) |
      field(B_BE_RXDMA_F2PCMDRPT_QUEUE_ID_SW_MASK, WLCPU_RXCH2_QID));
}

static void interrupt_init(void)
{
  change32(R_BE_DISP_HOST_IMR, B_BE_DISP_HOST_IMR_CLR, B_BE_DISP_HOST_IMR_SET);
  change32(R_BE_DISP_CPU_IMR, B_BE_DISP_CPU_IMR_CLR, B_BE_DISP_CPU_IMR_SET);
  change32(R_BE_DISP_OTHER_IMR, B_BE_DISP_OTHER_IMR_CLR, B_BE_DISP_OTHER_IMR_SET);
  change32(R_BE_PKTIN_ERR_IMR, B_BE_PKTIN_ERR_IMR_CLR, B_BE_PKTIN_ERR_IMR_SET);
  change32(R_BE_INTERRUPT_MASK_REG, B_BE_INTERRUPT_MASK_REG_CLR, B_BE_INTERRUPT_MASK_REG_SET);
  change32(R_BE_MLO_ERR_IDCT_IMR, B_BE_MLO_ERR_IDCT_IMR_CLR, B_BE_MLO_ERR_IDCT_IMR_SET);
  change32(R_BE_MPDU_TX_ERR_IMR, B_BE_MPDU_TX_ERR_IMR_CLR, B_BE_MPDU_TX_ERR_IMR_SET);
  change32(R_BE_MPDU_RX_ERR_IMR, B_BE_MPDU_RX_ERR_IMR_CLR, B_BE_MPDU_RX_ERR_IMR_SET);
  change32(R_BE_SEC_ERROR_IMR, B_BE_SEC_ERROR_IMR_CLR, B_BE_SEC_ERROR_IMR_SET);
  change32(R_BE_CPUIO_ERR_IMR, B_BE_CPUIO_ERR_IMR_CLR, B_BE_CPUIO_ERR_IMR_SET);
  change32(R_BE_WDE_ERR_IMR, B_BE_WDE_ERR_IMR_CLR, B_BE_WDE_ERR_IMR_SET);
  change32(R_BE_WDE_ERR1_IMR, B_BE_WDE_ERR1_IMR_CLR, B_BE_WDE_ERR1_IMR_SET);
  change32(R_BE_PLE_ERR_IMR, B_BE_PLE_ERR_IMR_CLR, B_BE_PLE_ERR_IMR_SET);
  change32(R_BE_PLE_ERRFLAG1_IMR, B_BE_PLE_ERRFLAG1_IMR_CLR, B_BE_PLE_ERRFLAG1_IMR_SET);
  change32(R_BE_WDRLS_ERR_IMR, B_BE_WDRLS_ERR_IMR_CLR, B_BE_WDRLS_ERR_IMR_SET);
  change32(R_BE_TXPKTCTL_B0_ERRFLAG_IMR, B_BE_TXPKTCTL_B0_ERRFLAG_IMR_CLR, B_BE_TXPKTCTL_B0_ERRFLAG_IMR_SET);
  change32(R_BE_TXPKTCTL_B1_ERRFLAG_IMR, B_BE_TXPKTCTL_B1_ERRFLAG_IMR_CLR, B_BE_TXPKTCTL_B1_ERRFLAG_IMR_SET);
  change32(R_BE_BBRPT_COM_ERR_IMR, B_BE_BBRPT_COM_ERR_IMR_CLR, B_BE_BBRPT_COM_ERR_IMR_SET);
  change32(R_BE_BBRPT_CHINFO_ERR_IMR, B_BE_BBRPT_CHINFO_ERR_IMR_CLR, B_BE_BBRPT_CHINFO_ERR_IMR_SET);
  change32(R_BE_BBRPT_DFS_ERR_IMR, B_BE_BBRPT_DFS_ERR_IMR_CLR, B_BE_BBRPT_DFS_ERR_IMR_SET);
  change32(R_BE_LA_ERRFLAG_IMR, B_BE_LA_ERRFLAG_IMR_CLR, B_BE_LA_ERRFLAG_IMR_SET);
  change32(R_BE_CH_INFO_DBGFLAG_IMR, B_BE_CH_INFO_DBGFLAG_IMR_CLR, B_BE_CH_INFO_DBGFLAG_IMR_SET);
  change32(R_BE_PLRLS_ERR_IMR, B_BE_PLRLS_ERR_IMR_CLR, B_BE_PLRLS_ERR_IMR_SET);
  change32(R_BE_HAXI_IDCT_MSK, B_BE_HAXI_IDCT_MSK_CLR, B_BE_HAXI_IDCT_MSK_SET);
  write32(R_BE_DMAC_ERR_IMR, DMAC_ERR_IMR_EN & ~B_BE_DMAC_NOTX_ERR_INT_EN);
}

static void cmac_interrupt_init(unsigned mac_idx)
{
  change32(mac_reg(R_BE_RESP_IMR, mac_idx), B_BE_RESP_IMR_CLR, B_BE_RESP_IMR_SET);
  change32(mac_reg(R_BE_RX_ERROR_FLAG_IMR, mac_idx), B_BE_RX_ERROR_FLAG_IMR_CLR, B_BE_RX_ERROR_FLAG_IMR_SET);
  change32(mac_reg(R_BE_TX_ERROR_FLAG_IMR, mac_idx), B_BE_TX_ERROR_FLAG_IMR_CLR, B_BE_TX_ERROR_FLAG_IMR_SET);
  change32(mac_reg(R_BE_RX_ERROR_FLAG_IMR_1, mac_idx), B_BE_TX_ERROR_FLAG_IMR_1_CLR, B_BE_TX_ERROR_FLAG_IMR_1_SET);
  change32(mac_reg(R_BE_PTCL_IMR1, mac_idx), B_BE_PTCL_IMR1_CLR, B_BE_PTCL_IMR1_SET);
  change32(mac_reg(R_BE_PTCL_IMR0, mac_idx), B_BE_PTCL_IMR0_CLR, B_BE_PTCL_IMR0_SET);
  change32(mac_reg(R_BE_PTCL_IMR_2, mac_idx), B_BE_PTCL_IMR_2_CLR, B_BE_PTCL_IMR_2_SET);
  change32(mac_reg(R_BE_SCHEDULE_ERR_IMR, mac_idx), B_BE_SCHEDULE_ERR_IMR_CLR, B_BE_SCHEDULE_ERR_IMR_SET);
  change32(mac_reg(R_BE_C0_TXPWR_IMR, mac_idx), B_BE_C0_TXPWR_IMR_CLR, B_BE_C0_TXPWR_IMR_SET);
  change32(mac_reg(R_BE_TRXPTCL_ERROR_INDICA_MASK, mac_idx), B_BE_TRXPTCL_ERROR_INDICA_MASK_CLR, B_BE_TRXPTCL_ERROR_INDICA_MASK_SET);
  change32(mac_reg(R_BE_RX_ERR_IMR, mac_idx), B_BE_RX_ERR_IMR_CLR, B_BE_RX_ERR_IMR_SET);
  change32(mac_reg(R_BE_PHYINFO_ERR_IMR_V1, mac_idx), B_BE_PHYINFO_ERR_IMR_V1_CLR, B_BE_PHYINFO_ERR_IMR_V1_SET);
}

static void host_report_init(void)
{
  change32(R_BE_WDRLS_CFG, B_BE_WDRLS_MODE_MASK, 0);
  change32(R_BE_RLSRPT0_CFG1,
      B_BE_RLSRPT0_FLTR_MAP_MASK | B_BE_RLSRPT0_AGGNUM_MASK | B_BE_RLSRPT0_TO_MASK,
      field(B_BE_RLSRPT0_FLTR_MAP_MASK, S_BE_WDRLS_FLTR_TXOK | S_BE_WDRLS_FLTR_RTYLMT |
      S_BE_WDRLS_FLTR_LIFTIM | S_BE_WDRLS_FLTR_MACID) |
      field(B_BE_RLSRPT0_AGGNUM_MASK, MAC_RPR_AGGREGATE) |
      field(B_BE_RLSRPT0_TO_MASK, MAC_RPR_TIMEOUT));
  change32(R_BE_RSP_CHK_SIG, B_BE_RSP_STATIC_RTS_CHK_SERV_BW_EN, 0);
}

static bool response_disable(unsigned mac_idx)
{
  /* The BA engine is common to both CMACs; other response gates are banked. */
  bool stopped = gate32("tx-gate", mac_reg(R_BE_CTN_DRV_TXEN, mac_idx), B_BE_CTN_TXEN_ALL_MASK);
  stopped = gate32("response-gate", mac_reg(R_BE_CMAC_FUNC_EN, mac_idx), B_BE_RESP_PKTCTL_EN) && stopped;
  stopped = gate32("qosnull-gate", mac_reg(R_BE_TB_PPDU_CTRL, mac_idx), B_BE_QOSNULL_UPD_MUEDCA_EN) && stopped;
  stopped = gate32("cts-gate", mac_reg(R_BE_SIFS_SETTING, mac_idx), B_BE_HW_CTS2SELF_EN) && stopped;
  return gate8("trigger-gate", mac_reg(R_BE_PTCL_COMMON_SETTING_0, mac_idx),
      B_BE_PTCL_TRIGGER_SS_EN_0 | B_BE_PTCL_TRIGGER_SS_EN_1 | B_BE_PTCL_TRIGGER_SS_EN_UL) && stopped;
}

bool rtl8922ae_mac_quiesce(uint64_t deadline, const char **detail)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  *detail = "RTL8922AE MAC scheduler/response stop unconfirmed";
  bool stopped = gate32("ba-gate", R_BE_CMAC_SHARE_FUNC_EN, B_BE_RESPBA_EN);
  for (unsigned mac_idx = 0; mac_idx < (cmac1_enabled ? MAC_COUNT : 1u); ++mac_idx) {
    stopped = response_disable(mac_idx) && stopped;
  }
  if (!stopped) return refused(detail, *detail);
  for (unsigned mac_idx = 0; mac_idx < (cmac1_enabled ? MAC_COUNT : 1u); ++mac_idx) {
    if (!poll("tx-idle", mac_reg(R_BE_PTCL_TX_CTN_SEL, mac_idx), B_BE_PTCL_BUSY, 0,
        66 * MAC_MS, deadline)) {
      return refused(detail, "RTL8922AE MAC protocol idle unconfirmed");
    }
  }
  return true;
}

static bool cmac_init(unsigned mac_idx, uint64_t deadline)
{
  scheduler_init(mac_idx);
  if (!cam_init(mac_idx, deadline)) return false;
  receive_filter_init(mac_idx);
  tmac_init(mac_idx);
  /* Linux resets the shared BA CAM once, from CMAC0 RMAC setup. */
  if (mac_idx == 0) {
    change32(R_BE_RESPBA_CAM_CTRL, B_BE_BACAM_RST_MASK,
        field(B_BE_BACAM_RST_MASK, S_BE_BACAM_RST_ALL));
    if (!poll("respba_cam_c", R_BE_RESPBA_CAM_CTRL, B_BE_BACAM_RST_MASK,
        field(B_BE_BACAM_RST_MASK, S_BE_BACAM_RST_DONE), MAC_MS, deadline)) return false;
  }
  rmac_init(mac_idx);
  protocol_init(mac_idx);
  return response_disable(mac_idx);
}

static bool ple_quota_refresh(unsigned quota_id, uint64_t deadline)
{
  change32(R_BE_PLE_BUFMGN_CTL, B_BE_PLE_AVAL_UPD_QTAID_MASK,
      field(B_BE_PLE_AVAL_UPD_QTAID_MASK, quota_id));
  change32(R_BE_PLE_BUFMGN_CTL, 0, B_BE_PLE_AVAL_UPD_REQ);
  return poll("ple-quota", R_BE_PLE_BUFMGN_CTL, B_BE_PLE_AVAL_UPD_REQ, 0, 2 * MAC_MS, deadline);
}

static bool band1_enable(uint64_t deadline)
{
  if (!poll("tx-idle", R_BE_PTCL_TX_CTN_SEL, B_BE_PTCL_BUSY, 0, 66 * MAC_MS, deadline)) return false;
  dle_quota_init();
  if (!ple_quota_refresh(MAC_PLE_B0_TX_QUOTA, deadline) ||
      !ple_quota_refresh(MAC_PLE_CMAC0_RX_QUOTA, deadline)) return false;
  preload_init(1);
  change32(R_BE_AFE_CTRL1, 0, B_BE_AFE_CTRL1_SET);
  change32(R_BE_SYS_ISO_CTRL_EXTEND, B_BE_R_SYM_ISO_CMAC12PP, 0);
  change32(R_BE_FEN_RST_ENABLE, 0, B_BE_CMAC1_FEN);
  change32(mac_reg(R_BE_CK_EN, 1), 0, B_BE_CK_EN_SET);
  change32(mac_reg(R_BE_CMAC_FUNC_EN, 1), 0, B_BE_CMAC_FUNC_EN_SET);
  cmac1_enabled = true;
  /* Close the newly enabled response engine before context setup. */
  if (!response_disable(1) || !cmac_init(1, deadline)) return false;
  change32(R_BE_FEN_RST_ENABLE, 0, B_BE_FEN_BB1PLAT_RSTB | B_BE_FEN_BB1_IP_RSTN);
  cmac_interrupt_init(1);
  return true;
}

bool rtl8922ae_mac_init(uint64_t deadline, const char **detail)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  mac_failure = (struct mac_failure){0};
  cmac1_enabled = false;
  *detail = "RTL8922AE MAC initialization interrupted";
  if (!rtl8922ae_operation_continue() || task_deadline_expired(deadline) ||
      !gate32("tx-gate", R_BE_CTN_DRV_TXEN, B_BE_CTN_TXEN_ALL_MASK)) {
    return refused(detail, *detail);
  }
  /* The BE system-init callbacks for DMAC/CMAC0 are empty in the reference;
   * power-on already establishes the function enables. */
  *detail = "RTL8922AE full DBCC DMAC/TRX initialization failed";
  if (!dmac_init(deadline)) return refused(detail, *detail);
  *detail = "RTL8922AE CMAC0 initialization failed";
  if (!cmac_init(0, deadline)) return refused(detail, *detail);
  *detail = "RTL8922AE CMAC1/BB1 initialization failed";
  if (!band1_enable(deadline)) return refused(detail, *detail);
  /* Linux notifies firmware only after the DBCC resources are enabled. The
   * command requests DONE_ACK without waiting for that firmware reply. */
  static const uint8_t dbcc_enable[] = {1, 0, 0, 0};
  if (!rtl8922ae_transport_h2c(H2C_CATEGORY_MAC, H2C_CLASS_MEDIA_REPORT,
      H2C_NOTIFY_DBCC, false, true, dbcc_enable, sizeof(dbcc_enable), NULL,
      deadline, detail)) return false;
  *detail = "RTL8922AE MAC error/report initialization failed";
  interrupt_init();
  cmac_interrupt_init(0);
  write32(R_BE_CMAC_ERR_IMR, CMAC0_ERR_IMR_EN);
  write32(mac_reg(R_BE_CMAC_ERR_IMR, 1), CMAC1_ERR_IMR_EN);
  host_report_init();
  if (!rtl8922ae_mac_quiesce(deadline, detail)) return false;
  /* Reference MAC init sends OFLD_CFG before BB/RF setup. */
  static const uint8_t offload_config[] = {0x09, 0, 0, 0, 0x5e, 0, 0, 0};
  return rtl8922ae_transport_h2c(H2C_CATEGORY_MAC, H2C_CLASS_OFFLOAD,
      H2C_OFLD_CFG, false, true, offload_config, sizeof(offload_config), NULL,
      deadline, detail);
}

bool rtl8922ae_mac_channel(uint8_t channel, uint64_t deadline, const char **detail)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  *detail = "RTL8922AE MAC 2.4 GHz/20 MHz channel context refused";
  if (!channel || channel > MAC_CHANNEL_COUNT || task_deadline_expired(deadline) ||
      !rtl8922ae_operation_continue()) {
    return false;
  }
  for (unsigned mac_idx = 0; mac_idx < MAC_COUNT; ++mac_idx) {
    change8(mac_reg(R_BE_WMAC_RFMOD, mac_idx), B_BE_WMAC_RFMOD_MASK, BE_WMAC_RFMOD_20M);
    write32(mac_reg(R_BE_TX_SUB_BAND_VALUE, mac_idx), field(B_BE_PRI20_BITMAP_MASK, 1));
    change8(mac_reg(R_BE_TXRATE_CHK, mac_idx), B_BE_BAND_MODE | B_BE_CHECK_CCK_EN | B_BE_RTS_LIMIT_IN_OFDM6,
        B_BE_BAND_MODE);
    change32(mac_reg(R_BE_PREBKF_CFG_1, mac_idx), B_BE_SIFS_MACTXEN_T1_MASK,
        field(B_BE_SIFS_MACTXEN_T1_MASK, 0x3f));
    change32(mac_reg(R_BE_MUEDCA_EN, mac_idx), B_BE_SIFS_MACTXEN_TB_T1_MASK,
        field(B_BE_SIFS_MACTXEN_TB_T1_MASK, 0x3e));
  }
  bool stopped = gate32("ba-gate", R_BE_CMAC_SHARE_FUNC_EN, B_BE_RESPBA_EN);
  for (unsigned mac_idx = 0; mac_idx < MAC_COUNT; ++mac_idx) {
    stopped = response_disable(mac_idx) && stopped;
  }
  return stopped || refused(detail, *detail);
}
