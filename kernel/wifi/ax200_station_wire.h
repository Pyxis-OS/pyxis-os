/* AX200 station wire subset derived from the pinned OpenBSD if_iwxreg.h.
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * BSD-3-Clause alternative selected; full retained notice in kernel/wifi/NOTICE.
 */
#ifndef AX200_STATION_WIRE_H
#define AX200_STATION_WIRE_H
#include <stdint.h>

#define AX_STATION_MAC_GROUP 3u
#define AX_STATION_DATA_GROUP 5u
#define AX_STATION_ADD 1u
#define AX_STATION_MODIFY 2u
#define AX_STATION_REMOVE 3u
#define AX_STATION_PHY 0x08u
#define AX_STATION_MAC 0x28u
#define AX_STATION_BINDING 0x2bu
#define AX_STATION_ADD_PEER 0x18u
#define AX_STATION_REMOVE_PEER 0x19u
#define AX_STATION_CAP_ULTRA_HB_CHANNELS 48u
#define AX_STATION_SESSION 0x05u
#define AX_STATION_RLC 0x08u
#define AX_STATION_TLC 0x0fu
#define AX_STATION_SMART_FIFO 0xd1u
#define AX_STATION_MULTICAST 0xd0u
#define AX_STATION_MAC_POWER 0xa9u
#define AX_STATION_TX_POWER 0x9fu
#define AX_STATION_GEO_POWER 5u
#define AX_STATION_LARI 1u
#define AX_STATION_BSS_STA 5u
#define AX_STATION_IE_SSID 0u
#define AX_STATION_IE_RATES 1u
#define AX_STATION_IE_CHANNEL 3u
#define AX_STATION_IE_TIM 5u
#define AX_STATION_IE_CHANNEL_SWITCH 37u
#define AX_STATION_IE_ERP 42u
#define AX_STATION_IE_RSN 48u
#define AX_STATION_IE_EXTENDED_RATES 50u
#define AX_STATION_IE_HT_OPERATION 61u
#define AX_STATION_HT_SECONDARY_MASK 0x03u
#define AX_STATION_HT_SECONDARY_RESERVED 0x02u
#define AX_STATION_IE_VENDOR 221u
#define AX_STATION_IE_RSNXE 244u
#define AX_STATION_RX_AMPDU (UINT16_C(1) << 5)
#define AX_STATION_API_TX_POWER 45u
#define AX_STATION_MTU 1500u
#define AX_STATION_MPDU_BYTES 1532u
#define AX_STATION_FRAME_BYTES 2346u
#define AX_STATION_RATE_COUNT 12u
#define AX_STATION_DATA_HIGH_WATER 224u
#define AX_STATION_DRAIN (UINT32_C(1) << 12)
#define AX_STATION_WIDTH_MASK (UINT32_C(3) << 26)
#define AX_STATION_MIMO_MASK (UINT32_C(3) << 28)
#define AX_STATION_FILTER_GROUP (UINT32_C(1) << 2)
#define AX_STATION_FILTER_BEACON (UINT32_C(1) << 6)
#define AX_STATION_ERP_PROTECT (UINT32_C(1) << 3)
#define AX_STATION_SHORT_SLOT (UINT32_C(1) << 4)
#define AX_STATION_SHORT_PREAMBLE (UINT32_C(1) << 5)
#define AX_STATION_RATE_ANTENNA_SHIFT 14u
#define AX_STATION_RATE_OFDM (UINT32_C(1) << 8)
#define AX_STATION_CAP_ESS UINT16_C(1)
#define AX_STATION_CAP_PRIVACY UINT16_C(0x10)
#define AX_STATION_CAP_SHORT_PREAMBLE UINT16_C(0x20)
#define AX_STATION_CAP_SHORT_SLOT UINT16_C(0x400)
#define AX_STATION_AUTH UINT8_C(0xb0)
#define AX_STATION_ASSOC_REQUEST UINT8_C(0)
#define AX_STATION_ASSOC_RESPONSE UINT8_C(0x10)
#define AX_STATION_DEAUTH UINT8_C(0xc0)
#define AX_STATION_DISASSOC UINT8_C(0xa0)
#define AX_STATION_DATA UINT8_C(0x08)
#define AX_STATION_MORE_FRAG UINT8_C(0x04)
#define AX_STATION_RETRY UINT8_C(0x08)

struct ax_station_phy {
  uint32_t id_color, action;
  uint8_t band, channel, width, control;
  uint32_t lmac, rx_chain, dsp, reserved;
} __attribute__((packed));
struct ax_station_phy_uhb {
  uint32_t id_color, action, channel;
  uint8_t band, width, control, channel_reserved;
  uint32_t lmac, rx_chain, dsp, reserved;
} __attribute__((packed));
struct ax_station_rlc {
  uint32_t phy, rx_chain, reserved;
  uint32_t sad_a, sad_b, mac, sad_reserved;
  uint8_t flags, padding[3];
} __attribute__((packed));
struct ax_station_binding {
  uint32_t id_color, action, macs[3], phy, lmac;
} __attribute__((packed));
struct ax_station_peer {
  uint8_t modify, awake;
  uint16_t disable_tid;
  uint32_t mac_color;
  uint8_t address[6];
  uint16_t reserved1;
  uint8_t station, modify_mask;
  uint16_t reserved2;
  uint32_t flags, flags_mask;
  uint8_t add_ba, remove_ba;
  uint16_t ba_sequence, sleep_count;
  uint8_t sleep_flags, type;
  uint16_t aid, beamforming;
  uint32_t queues;
  uint16_t ba_window;
  uint8_t service_period, uapsd;
} __attribute__((packed));
struct ax_station_mac_data {
  uint32_t associated, dtim_time;
  uint64_t dtim_tsf;
  uint32_t beacon_interval, reserved, dtim_interval, policy, listen_interval, aid, beacon_arrival;
} __attribute__((packed));
struct ax_station_edca {
  uint16_t minimum, maximum;
  uint8_t aifs, fifos;
  uint16_t txop;
} __attribute__((packed));
struct ax_station_mac {
  uint32_t id_color, action, type, tsf;
  uint8_t address[6];
  uint16_t reserved1;
  uint8_t bssid[6];
  uint16_t reserved2;
  uint32_t cck, ofdm, protection, preamble, slot, filter, qos;
  struct ax_station_edca ac[5];
  union { struct ax_station_mac_data sta; uint8_t specific[48]; };
} __attribute__((packed));
struct ax_station_session {
  uint32_t id_color, action, configuration, duration, repetitions, interval;
} __attribute__((packed));
struct ax_station_tlc {
  uint8_t station, reserved[3], width, mode, chains, sgi;
  uint16_t flags, legacy_rates, ht_rates[2][3], max_mpdu, max_txop;
} __attribute__((packed));
_Static_assert(sizeof(struct ax_station_phy) == 28, "AX200 PHY4");
_Static_assert(sizeof(struct ax_station_phy_uhb) == 32, "AX200 PHY4 UHB channel layout");
_Static_assert(sizeof(struct ax_station_rlc) == 32, "AX200 RLC2");
_Static_assert(sizeof(struct ax_station_binding) == 28, "AX200 binding2");
_Static_assert(sizeof(struct ax_station_peer) == 48, "AX200 peer12");
_Static_assert(sizeof(struct ax_station_mac) == 148, "AX200 MAC5");
_Static_assert(sizeof(struct ax_station_tlc) == 28, "AX200 TLC4");
#endif
