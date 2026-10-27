/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2021-2023 Realtek Corporation
 * BE RX format and RSSI reference: Linux rtw89 core.c/core.h, txrx.h and
 * rtw8922a.c at 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, BSD alternative.
 * See kernel/wifi/NOTICE for the complete notice.
 */
#include "rtl8922ae_discovery.h"

#include <kernel/memory.h>
#include "wifi_rsn.h"

#define RXD_SHORT_BYTES 24u
#define RXD_LONG_BYTES 40u
#define RXD_PHY_REPORT_BYTES 8u
#define RXD_PACKET_LENGTH_MASK UINT32_C(0x3fff)
#define RXD_PHY_REPORT_SHIFT 22u
#define RXD_PHY_REPORT_MASK UINT32_C(3)
#define RXD_PACKET_TYPE_SHIFT 24u
#define RXD_PACKET_TYPE_MASK UINT32_C(0x3f)
#define RXD_PACKET_TYPE_WIFI 0u
#define RXD_LONG (UINT32_C(1) << 31)
#define RXD_CRC_ERROR (UINT32_C(1) << 6)
#define RXD_ICV_ERROR (UINT32_C(1) << 7)
#define RXD_PHY_RSSI_MASK UINT32_C(0xfff)
#define RXD_RSSI_MAX 110u
#define SIGNAL_UNAVAILABLE (-256)
#define FRAME_HEADER_BYTES 24u
#define FRAME_BEACON_BYTES 12u
#define FRAME_FCS_BYTES 4u
#define FRAME_BEACON 0x80u
#define FRAME_PROBE_RESPONSE 0x50u
#define FRAME_DIRECTION_MASK 0x03u
#define FRAME_PROTECTED 0x40u
#define IE_SSID 0u
#define IE_RATES 1u
#define IE_DS 3u
#define IE_HT_OPERATION 61u

static uint16_t read_le16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static uint32_t read_le32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
      (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static bool hidden_ssid(const uint8_t *ssid, size_t bytes)
{
  for (size_t i = 0; i < bytes; ++i) {
    if (ssid[i]) {
      return false;
    }
  }
  return true;
}

void rtl8922ae_discovery_init(struct rtl8922ae_discovery *discovery)
{
  rtl8922ae_discovery_reset(discovery);
}

void rtl8922ae_discovery_reset(struct rtl8922ae_discovery *discovery)
{
  memset(discovery, 0, sizeof(*discovery));
}

void rtl8922ae_discovery_channel_start(struct rtl8922ae_discovery *discovery,
    uint8_t channel)
{
  discovery->expected_channel = channel;
  discovery->collecting = true;
}

void rtl8922ae_discovery_stop(struct rtl8922ae_discovery *discovery)
{
  discovery->collecting = false;
  discovery->expected_channel = 0;
}

void rtl8922ae_discovery_receive(struct rtl8922ae_discovery *discovery,
    const uint8_t *descriptor, size_t descriptor_bytes,
    const uint8_t *frame, size_t frame_bytes)
{
  if (!discovery->collecting || discovery->overflow || descriptor_bytes < RXD_SHORT_BYTES) {
    return;
  }
  uint32_t dword0 = read_le32(descriptor);
  uint32_t dword3 = read_le32(descriptor + 12);
  size_t rxd_bytes = dword0 & RXD_LONG ? RXD_LONG_BYTES : RXD_SHORT_BYTES;
  size_t packet_bytes = dword0 & RXD_PACKET_LENGTH_MASK;
  size_t phy_bytes = ((dword0 >> RXD_PHY_REPORT_SHIFT) & RXD_PHY_REPORT_MASK) * 8;
  if (((dword0 >> RXD_PACKET_TYPE_SHIFT) & RXD_PACKET_TYPE_MASK) != RXD_PACKET_TYPE_WIFI ||
      (dword3 & (RXD_CRC_ERROR | RXD_ICV_ERROR)) || descriptor_bytes < rxd_bytes ||
      packet_bytes > frame_bytes ||
      packet_bytes < FRAME_HEADER_BYTES + FRAME_BEACON_BYTES + FRAME_FCS_BYTES ||
      descriptor_bytes - rxd_bytes < phy_bytes) {
    return;
  }
  int signal = SIGNAL_UNAVAILABLE;
  if (phy_bytes == RXD_PHY_REPORT_BYTES) {
    uint32_t rssi = read_le32(descriptor + rxd_bytes) & RXD_PHY_RSSI_MASK;
    if (rssi > 1 && (rssi >> 2) <= RXD_RSSI_MAX) {
      signal = (int)(rssi >> 2) - (int)RXD_RSSI_MAX;
    }
  }
  if ((frame[0] != FRAME_BEACON && frame[0] != FRAME_PROBE_RESPONSE) ||
      (frame[1] & (FRAME_DIRECTION_MASK | FRAME_PROTECTED)) || (frame[16] & 1)) {
    return;
  }
  static const uint8_t zero_bssid[6];
  if (!memcmp(frame + 16, zero_bssid, sizeof(zero_bssid))) {
    return;
  }
  /* Native MAC appends the FCS, as in rtw89's RX_INCLUDES_FCS contract. */
  packet_bytes -= FRAME_FCS_BYTES;
  const uint8_t *ssid = NULL;
  size_t ssid_bytes = 0;
  bool rates = false, duplicate_rsn = false;
  uint32_t security_flags = 0;
  uint8_t channel = discovery->expected_channel;
  size_t offset = FRAME_HEADER_BYTES + FRAME_BEACON_BYTES;
  while (offset < packet_bytes) {
    if (packet_bytes - offset < 2) {
      return;
    }
    uint8_t id = frame[offset], length = frame[offset + 1];
    offset += 2;
    if (length > packet_bytes - offset) {
      return;
    }
    const uint8_t *value = frame + offset;
    if (id == IE_SSID) {
      if (length > 32 || (ssid && (length != ssid_bytes || memcmp(ssid, value, length)))) {
        return;
      }
      ssid = value;
      ssid_bytes = length;
    } else if (id == AX_RSN_ELEMENT_ID) {
      duplicate_rsn |= (security_flags & WIFI_BSS_RSN_PRESENT) != 0;
      security_flags |= wifi_rsn_security(value - 2, (size_t)length + 2);
    } else if (id == IE_RATES) {
      if (!length || length > 8) {
        return;
      }
      rates = true;
    } else if (id == IE_DS || id == IE_HT_OPERATION) {
      if (length != (id == IE_DS ? 1 : 22) || value[0] != channel) {
        return;
      }
    }
    offset += length;
  }
  if (!ssid || !rates || !channel || channel > WIFI_CHANNEL_24_COUNT) {
    return;
  }
  if (duplicate_rsn) {
    security_flags &= ~WIFI_BSS_RSN_VALID;
  }
  struct wifi_bss_record *record = NULL;
  for (size_t i = 0; i < discovery->count; ++i) {
    if (discovery->records[i].channel == channel &&
        !memcmp(discovery->records[i].bssid, frame + 16, 6)) {
      record = &discovery->records[i];
      break;
    }
  }
  if (!record) {
    if (discovery->count == WIFI_SCAN_RECORDS) {
      discovery->overflow = true;
      return;
    }
    record = &discovery->records[discovery->count++];
    memcpy(record->bssid, frame + 16, 6);
    record->channel = channel;
    record->signal_dbm = signal;
    record->capabilities = read_le16(frame + FRAME_HEADER_BYTES + 10);
    record->security_flags = security_flags;
    record->ssid_bytes = ssid_bytes;
    memcpy(record->ssid, ssid, ssid_bytes);
  } else {
    /* Conflicting security observations remain unusable for this scan. */
    if (record->security_flags != security_flags) {
      record->security_flags = (record->security_flags | security_flags) & ~WIFI_BSS_RSN_VALID;
    }
    if (record->signal_dbm < signal) {
      record->signal_dbm = signal;
    }
    if (hidden_ssid(record->ssid, record->ssid_bytes) && !hidden_ssid(ssid, ssid_bytes)) {
      memset(record->ssid, 0, sizeof(record->ssid));
      record->ssid_bytes = ssid_bytes;
      memcpy(record->ssid, ssid, ssid_bytes);
    }
  }
}
