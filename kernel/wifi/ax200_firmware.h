/*
 * Firmware layouts/constants derived from OpenBSD if_iwxreg.h, under its BSD
 * license alternative:
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name Intel Corporation nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef AX200_FIRMWARE_PRIVATE_H
#define AX200_FIRMWARE_PRIVATE_H

#include <stdint.h>
#include <stddef.h>

/* CC77 payload layouts from OpenBSD 65596250e06438442ce07bad87e5ba129229607d,
 * if_iwxreg.h. Firmware command15 uses the reference's v14 request layout. */
enum ax200_group {
  AX200_LEGACY = 0,
  AX200_LONG = 1,
  AX200_SYSTEM = 2,
  AX200_PHY = 4,
  AX200_NVM = 12,
};

enum ax200_api {
  AX200_API_WIFI_MCC_UPDATE = 9,
  AX200_API_WIDE_HEADER = 14,
  AX200_API_BEACON_FILTER_V4 = 47,
  AX200_API_REGULATORY_NVM = 48,
  AX200_API_REDUCED_SCAN_CONFIG = 56,
  AX200_API_SCAN_CHANNEL_V2 = 58,
};

enum ax200_capability {
  AX200_CAP_LAR = 1,
  AX200_CAP_UMAC_SCAN = 2,
  AX200_CAP_DS_IE = 9,
  AX200_CAP_DQA = 12,
  AX200_CAP_MULTIQUEUE_RX = 68,
  AX200_CAP_THERMAL_KILL = 74,
  AX200_CAP_MCC_11AX = 89,
};

enum ax200_command {
  AX200_SCAN_CONFIG = 0x0c,
  AX200_SCAN_REQUEST = 0x0d,
  AX200_SCAN_ABORT = 0x0e,
  AX200_SCAN_COMPLETE = 0x0f,
  AX200_POWER_TABLE = 0x77,
  AX200_TX_ANTENNA = 0x98,
  AX200_BT_CONFIG = 0x9b,
  AX200_SCAN_ITERATION = 0xb5,
  AX200_RX_MPDU = 0xc1,
  AX200_MCC_UPDATE = 0xc8,
  AX200_MCC_CHANGED = 0xc9,
  AX200_BEACON_FILTER = 0xd2,
  AX200_LTR_CONFIG = 0xee,
};

#define AX200_INIT_COMPLETE 0x04
#define AX200_INIT_EXTENDED 0x03
#define AX200_INIT_NVM (UINT32_C(1) << 1)
#define AX200_SOC_CONFIG 0x01
#define AX200_NVM_COMPLETE 0x00
#define AX200_NVM_GET 0x02
#define AX200_TEMPERATURE_THRESHOLDS 0x04
#define AX200_THERMAL_KILL 0xfe
#define AX200_NVM_CHANNELS 110u
#define AX200_NVM_REPLY_BYTES 468u
#define AX200_MCC_CHANNELS 51u
#define AX200_MCC_REPLY_BYTES (20u + 4u * AX200_MCC_CHANNELS)
#define AX200_NVM_EMPTY_OTP (UINT32_C(1) << 0)
#define AX200_NVM_BAND_24 (UINT32_C(1) << 0)
#define AX200_CHANNEL_VALID (UINT32_C(1) << 0)
#define AX200_CHANNEL_IBSS (UINT32_C(1) << 1)
#define AX200_CHANNEL_ACTIVE (UINT32_C(1) << 3)
#define AX200_CHANNEL_RADAR (UINT32_C(1) << 4)
#define AX200_CHANNEL_INDOOR (UINT32_C(1) << 5)
#define AX200_CHANNEL_GO_CONCURRENT (UINT32_C(1) << 6)
#define AX200_CHANNEL_DFS (UINT32_C(1) << 7)
#define AX200_CHANNEL_WIDE (UINT32_C(1) << 8)
#define AX200_CHANNEL_40MHZ (UINT32_C(1) << 9)
#define AX200_CHANNEL_80MHZ (UINT32_C(1) << 10)
#define AX200_CHANNEL_160MHZ (UINT32_C(1) << 11)
#define AX200_CHANNEL_DC_HIGH (UINT32_C(1) << 12)
#define AX200_CHANNEL_KNOWN (AX200_CHANNEL_VALID | AX200_CHANNEL_IBSS | \
    AX200_CHANNEL_ACTIVE | AX200_CHANNEL_RADAR | AX200_CHANNEL_INDOOR | \
    AX200_CHANNEL_GO_CONCURRENT | AX200_CHANNEL_DFS | AX200_CHANNEL_WIDE | \
    AX200_CHANNEL_40MHZ | AX200_CHANNEL_80MHZ | AX200_CHANNEL_160MHZ | AX200_CHANNEL_DC_HIGH)
#define AX200_CHANNEL_UNQUALIFIED (AX200_CHANNEL_RADAR | AX200_CHANNEL_INDOOR | \
    AX200_CHANNEL_DFS | AX200_CHANNEL_DC_HIGH)
#define AX200_VALID_ANTENNAS UINT32_C(3)
#define AX200_FW_TX_CHAIN_SHIFT 16u
#define AX200_FW_RX_CHAIN_SHIFT 20u
#define AX200_FW_CHAIN_MASK UINT32_C(0xf)
#define AX200_BT_WIFI UINT32_C(3)
#define AX200_SOC_DISCRETE UINT32_C(1)
#define AX200_LTR_ENABLE UINT32_C(1)
enum ax200_mcc_source {
  AX200_MCC_OLD_FIRMWARE = 0,
  AX200_MCC_MANAGEMENT_ENGINE = 1,
  AX200_MCC_BIOS = 2,
  AX200_MCC_CELLULAR_HOST = 3,
  AX200_MCC_CELLULAR_DEVICE = 4,
  AX200_MCC_WIFI = 5,
  AX200_MCC_DEFAULT = 7,
  AX200_MCC_API = 9,
  AX200_MCC_GET_CURRENT = 0x10,
};
#define AX200_MCC_SAME_PROFILE UINT32_C(1)
#define AX200_BAND_24 1u
#define AX200_SCAN_PASS_ALL (UINT16_C(1) << 1)
#define AX200_SCAN_ITERATION_NOTIFY (UINT16_C(1) << 2)
#define AX200_SCAN_ADAPTIVE_DWELL (UINT16_C(1) << 7)
#define AX200_SCAN_FORCE_PASSIVE (UINT16_C(1) << 11)
#define AX200_SCAN_CHANNEL_ORDER (UINT8_C(1) << 5)
#define AX200_SCAN_DONE 1u
#define AX200_SCAN_ABORTED 2u
#define AX200_SCAN_PRIORITY 6u
#define AX200_SCAN_PASSIVE_DWELL 110u
#define AX200_SCAN_ACTIVE_DWELL 10u
#define AX200_RX_DESCRIPTOR_BYTES 48u
#define AX200_RX_CRC_OK (UINT32_C(1) << 0)
#define AX200_RX_OVERRUN_OK (UINT32_C(1) << 1)
#define AX200_RX_SECURITY_MASK UINT32_C(0x700)
#define AX200_RX_RFKILL (UINT32_C(1) << 29)
#define AX200_RX_PAD UINT8_C(0x20)
#define AX200_RX_AMSDU UINT8_C(0x40)
#define AX200_FRAME_HEADER_BYTES 24u
#define AX200_FRAME_BEACON_BYTES 12u
#define AX200_FRAME_PROTECTED UINT8_C(0x40)
#define AX200_FRAME_BEACON UINT8_C(0x80)
#define AX200_FRAME_PROBE_REPLY UINT8_C(0x50)
#define AX200_FRAME_PROBE_REQUEST UINT8_C(0x40)
#define AX200_FRAME_DIRECTION_MASK UINT8_C(3)
#define AX200_IE_SSID 0u
#define AX200_IE_RATES 1u
#define AX200_IE_DS 3u
#define AX200_IE_HT_OPERATION 61u

/* AX200 uses the common 20-byte RX prefix plus the 28-byte v1 tail. */
struct ax200_rx_descriptor_v1 {
  uint16_t frame_bytes;
  uint8_t mac_flags1, mac_flags2, amsdu_info;
  uint16_t phy_info;
  uint8_t mac_phy;
  uint16_t checksum, l3_l4;
  uint32_t status, reorder, rss, filter, rate;
  uint8_t energy_a, energy_b, channel, mac_context;
  uint32_t gp2, tsf[2];
} __attribute__((packed));

_Static_assert(sizeof(struct ax200_rx_descriptor_v1) == AX200_RX_DESCRIPTOR_BYTES,
    "AX200 RX descriptor v1 extent");
_Static_assert(offsetof(struct ax200_rx_descriptor_v1, status) == 12,
    "AX200 RX status offset");
_Static_assert(offsetof(struct ax200_rx_descriptor_v1, energy_a) == 32,
    "AX200 RX energy offset");
_Static_assert(offsetof(struct ax200_rx_descriptor_v1, channel) == 34,
    "AX200 RX channel offset");

struct ax200_scan_general {
  uint16_t flags;
  uint8_t reserved, start_mac;
  uint8_t active_dwell[2];
  uint8_t adaptive_24, adaptive_5, adaptive_social, reserved1;
  uint16_t adaptive_budget;
  uint32_t max_out[2], suspend[2], priority;
  uint8_t passive_dwell[2], fragments[2];
} __attribute__((packed));

struct ax200_scan_channel {
  uint32_t flags;
  uint8_t channel, band, iterations, interval;
} __attribute__((packed));

struct ax200_scan_channels {
  uint8_t flags, count, ap_override[2];
  struct ax200_scan_channel channels[67];
} __attribute__((packed));

struct ax200_scan_schedule {
  uint16_t interval;
  uint8_t iterations, reserved;
} __attribute__((packed));

struct ax200_scan_periodic {
  struct ax200_scan_schedule schedule[2];
  uint16_t delay, reserved;
} __attribute__((packed));

struct ax200_probe_segment {
  uint16_t offset, length;
} __attribute__((packed));

struct ax200_probe_template {
  struct ax200_probe_segment mac, band[3], common;
  uint8_t bytes[512];
} __attribute__((packed));

struct ax200_scan_probe {
  struct ax200_probe_template frame;
  uint8_t short_count, bssid_count;
  uint16_t reserved;
  uint8_t directed_ssids[20][34];
  uint32_t short_ssids[8];
  uint8_t bssids[16][6];
} __attribute__((packed));

struct ax200_scan_command {
  uint32_t uid, priority;
  struct ax200_scan_general general;
  struct ax200_scan_channels channels;
  struct ax200_scan_periodic periodic;
  struct ax200_scan_probe probe;
} __attribute__((packed));

_Static_assert(sizeof(struct ax200_scan_general) == 36, "scan general v10");
_Static_assert(sizeof(struct ax200_scan_channels) == 540, "scan channels v6");
_Static_assert(sizeof(struct ax200_scan_probe) == 1344, "scan probe v4");
_Static_assert(sizeof(struct ax200_scan_command) == 1940, "scan command v14");

#endif
