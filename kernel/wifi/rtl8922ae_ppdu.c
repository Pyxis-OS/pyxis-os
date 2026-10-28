/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2020, 2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89 core.c/core.h, phy.c and txrx.h at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, BSD alternative.
 * Complete notice: kernel/wifi/NOTICE.
 */
#include "rtl8922ae_ppdu.h"

#define RXD_SHORT_BYTES 24u
#define RXD_LONG_BYTES 40u
#define RXD_PACKET_TYPE_SHIFT 24u
#define RXD_PACKET_TYPE_MASK 0x3fu
#define RXD_PACKET_PPDU 1u
#define RXD_BB_SELECT (UINT32_C(1) << 30)
#define RXD_LONG (UINT32_C(1) << 31)
#define RXD_COUNTER_SHIFT 8u
#define RXD_COUNTER_MASK 7u
#define RXD_BANDWIDTH_SHIFT 12u
#define RXD_BANDWIDTH_MASK 7u
#define RXD_GI_SHIFT 16u
#define RXD_GI_MASK 7u
#define RXD_RATE_SHIFT 20u
#define RXD_RATE_MODE_SHIFT 8u
#define RXD_RATE_MODE_MASK 7u
#define RXD_LEGACY_RATE_MASK 0x0fu
#define RXD_MCS_RATE_MASK 0x1fu
#define RATE_MODE_LEGACY 0u
#define RATE_MODE_EHT 4u
#define BANDWIDTH_20 0u
#define BANDWIDTH_160 3u
#define GI_0_8 0u
#define GI_1_6 1u
#define GI_3_2 2u
#define GI_FIELD_MASK 3u
#define GILTF_LGI_4XHE32 0u
#define GILTF_SGI_4XHE08 1u
#define GILTF_2XHE16 2u
#define GILTF_2XHE08 3u
#define GILTF_1XHE16 4u
#define GILTF_1XHE08 5u
#define MAC_INFO_BYTES 8u
#define MAC_USER_BYTES 4u
#define MAC_USERS_MAX 16u
#define MAC_USERS_MASK 0x1fu
#define MAC_PLCP_LENGTH_SHIFT 16u
#define MAC_PLCP_LENGTH_MASK 0xffu
#define MAC_INVALID (UINT32_C(1) << 27)
#define MAC_RX_COUNT_VALID (UINT32_C(1) << 29)
#define MAC_RX_COUNT_BYTES 128u
#define PHY_HEADER_BYTES 8u
#define PHY_PAGE_MASK 0x1fu
#define PHY_PAGE_CCK 11u
#define PHY_SECOND_HEADER (UINT32_C(1) << 5)
#define PHY_VALID (UINT32_C(1) << 7)
#define PHY_LENGTH_SHIFT 8u
#define PHY_LENGTH_MASK 0xffu
#define PHY_IE_TYPE_MASK 0x1fu
#define PHY_IE_LENGTH_SHIFT 5u
#define PHY_IE_LENGTH_MASK 0x7fu
#define PHY_IE_VARIABLE UINT8_MAX
#define PHY_IE_OFDM 1u
#define PHY_CHANNEL_SHIFT 16u
#define PHY_CHANNEL_MASK 0xffu
#define PHY_CHANNEL_BAND_SHIFT 4u
#define PHY_CHANNEL_2G 0u
#define PHY_CHANNEL_OFFSET_MASK 0x0fu
#define PHY_LENGTH_UNIT 8u

static uint32_t read32(const uint8_t *bytes)
{
  return bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
      (uint32_t)bytes[3] << 24;
}

static uint8_t normalize_gi(unsigned gi, bool report)
{
  switch (gi) {
  case GILTF_SGI_4XHE08:
  case GILTF_2XHE08:
  case GILTF_1XHE08:
    return GI_0_8;
  case GILTF_2XHE16:
  case GILTF_1XHE16:
    return GI_1_6;
  case GILTF_LGI_4XHE32:
    return GI_3_2;
  default:
    return report ? UINT8_MAX : GI_3_2;
  }
}

bool rtl8922ae_ppdu_identity(const uint8_t *rxd, size_t bytes,
    struct rtl8922ae_ppdu_identity *identity)
{
  *identity = (struct rtl8922ae_ppdu_identity){0};
  if (bytes < RXD_SHORT_BYTES) return false;
  uint32_t dword0 = read32(rxd);
  uint32_t dword4 = read32(rxd + 16);
  bool long_rxdesc = (dword0 & RXD_LONG) != 0;
  if (long_rxdesc && bytes < RXD_LONG_BYTES) return false;
  bool report = ((dword0 >> RXD_PACKET_TYPE_SHIFT) & RXD_PACKET_TYPE_MASK) == RXD_PACKET_PPDU;
  unsigned rate = dword4 >> RXD_RATE_SHIFT;
  unsigned mode = (rate >> RXD_RATE_MODE_SHIFT) & RXD_RATE_MODE_MASK;
  unsigned bandwidth = (dword4 >> RXD_BANDWIDTH_SHIFT) & RXD_BANDWIDTH_MASK;
  identity->phy = (dword0 & RXD_BB_SELECT) != 0;
  identity->counter = (dword4 >> RXD_COUNTER_SHIFT) & RXD_COUNTER_MASK;
  identity->bandwidth = bandwidth <= BANDWIDTH_160 ? bandwidth : BANDWIDTH_20;
  identity->long_rxdesc = long_rxdesc;
  identity->eht = mode == RATE_MODE_EHT;
  identity->gi = normalize_gi((dword4 >> RXD_GI_SHIFT) & RXD_GI_MASK, report);
  if (mode == RATE_MODE_LEGACY) {
    identity->rate = rate & RXD_LEGACY_RATE_MASK;
  } else if (mode <= RATE_MODE_EHT) {
    identity->rate = rate & RXD_MCS_RATE_MASK;
  } else {
    /* Linux's frame status starts zeroed; the report matcher starts at 255. */
    identity->rate = report ? UINT8_MAX : 0;
  }
  return true;
}

bool rtl8922ae_ppdu_match(const struct rtl8922ae_ppdu_identity *frame,
    const struct rtl8922ae_ppdu_identity *report)
{
  uint8_t gi = frame->gi;
  /* The reference stores HE GI in status bits 3..4 and EHT GI in bits 4..5.
   * Its matcher selects by report mode without requiring equal encodings. */
  if (frame->eht && !report->eht) {
    gi = (gi << 1) & GI_FIELD_MASK;
  } else if (!frame->eht && report->eht) {
    gi >>= 1;
  }
  return frame->phy == report->phy && frame->counter == report->counter &&
      frame->rate == report->rate && frame->bandwidth == report->bandwidth &&
      gi == report->gi;
}

uint8_t rtl8922ae_ppdu_channel(const uint8_t *payload, size_t bytes)
{
  static const uint8_t ie_lengths[32] = {
    32, 40, 24, 24, 8, 8, 8, 8, PHY_IE_VARIABLE, 8, PHY_IE_VARIABLE, 176,
    PHY_IE_VARIABLE, PHY_IE_VARIABLE, PHY_IE_VARIABLE, PHY_IE_VARIABLE,
    PHY_IE_VARIABLE, PHY_IE_VARIABLE, 88, 56, PHY_IE_VARIABLE, PHY_IE_VARIABLE,
    PHY_IE_VARIABLE, 0, 24, 24, 24, 24, 32, 32, 32, 32,
  };
  if (bytes < MAC_INFO_BYTES) return 0;
  uint32_t mac = read32(payload);
  unsigned users = mac & MAC_USERS_MASK;
  if ((mac & MAC_INVALID) || users > MAC_USERS_MAX) return 0;
  /* BE MAC prefix: 8 bytes, paired 4-byte users, optional 128-byte counters,
   * then the PLCP length in 8-byte units. No prefix is a fixed PHY offset. */
  size_t offset = MAC_INFO_BYTES + (users + (users & 1u)) * MAC_USER_BYTES;
  if (mac & MAC_RX_COUNT_VALID) offset += MAC_RX_COUNT_BYTES;
  offset += ((mac >> MAC_PLCP_LENGTH_SHIFT) & MAC_PLCP_LENGTH_MASK) * PHY_LENGTH_UNIT;
  if (offset > bytes || bytes - offset < PHY_HEADER_BYTES) return 0;
  payload += offset;
  bytes -= offset;
  uint32_t header = read32(payload);
  size_t length = ((header >> PHY_LENGTH_SHIFT) & PHY_LENGTH_MASK) * PHY_LENGTH_UNIT +
      PHY_HEADER_BYTES;
  if (!(header & PHY_VALID) || length != bytes ||
      (header & PHY_PAGE_MASK) < PHY_PAGE_CCK) return 0;
  offset = PHY_HEADER_BYTES;
  if (header & PHY_SECOND_HEADER) offset += PHY_HEADER_BYTES;
  if (offset > bytes) return 0;
  uint8_t channel_index = 0;
  while (offset < bytes) {
    if (bytes - offset < sizeof(uint32_t)) return 0;
    uint32_t ie = read32(payload + offset);
    unsigned type = ie & PHY_IE_TYPE_MASK;
    size_t ie_bytes = ie_lengths[type];
    if (ie_bytes == PHY_IE_VARIABLE) {
      ie_bytes = ((ie >> PHY_IE_LENGTH_SHIFT) & PHY_IE_LENGTH_MASK) * PHY_LENGTH_UNIT;
    }
    if (!ie_bytes || ie_bytes > bytes - offset) return 0;
    if (type == PHY_IE_OFDM) {
      channel_index = (ie >> PHY_CHANNEL_SHIFT) & PHY_CHANNEL_MASK;
    }
    offset += ie_bytes;
  }
  /* phy.c's channel index has a base-table selector in the high nibble.
   * Selector zero is 2.4 GHz, whose low nibble is the channel directly. */
  if ((channel_index >> PHY_CHANNEL_BAND_SHIFT) != PHY_CHANNEL_2G) return 0;
  return channel_index & PHY_CHANNEL_OFFSET_MASK;
}
