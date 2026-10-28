#ifndef RTL8922AE_DISCOVERY_H
#define RTL8922AE_DISCOVERY_H

#include <abi/wifi.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rtl8922ae_discovery {
  struct wifi_bss_record records[WIFI_SCAN_RECORDS];
  size_t count;
  uint8_t expected_channel;
  bool collecting, overflow;
};

/* Caller-owned state, used only by the radio worker. No DMA or frame pointer
 * survives receive. Reset starts an empty operation; stop retires collection
 * before the caller publishes its immutable result FILE. Missing or invalid
 * inline RSSI keeps the existing native -256 unknown-signal sentinel. */
void rtl8922ae_discovery_init(struct rtl8922ae_discovery *discovery);
void rtl8922ae_discovery_reset(struct rtl8922ae_discovery *discovery);
/* Open only after firmware-confirmed channel entry, or on the retained healthy
 * context already qualified for this channel, after draining stale RX. BE
 * ordinary RX carries no channel; DS/HT IEs, when present, must agree. */
void rtl8922ae_discovery_channel_start(struct rtl8922ae_discovery *discovery,
    uint8_t channel);
void rtl8922ae_discovery_stop(struct rtl8922ae_discovery *discovery);
/* The transport has bounded the complete RX envelope. descriptor_bytes covers
 * the RXD and optional inline PHY report up to the ordinary frame's offset;
 * frame_bytes covers the descriptor's declared packet, including its FCS. */
void rtl8922ae_discovery_receive(struct rtl8922ae_discovery *discovery,
    const uint8_t *descriptor, size_t descriptor_bytes,
    const uint8_t *frame, size_t frame_bytes);

#endif
