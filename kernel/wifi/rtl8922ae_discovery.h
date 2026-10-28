#ifndef RTL8922AE_DISCOVERY_H
#define RTL8922AE_DISCOVERY_H

#include <abi/wifi.h>
#include "rtl8922ae_ppdu.h"

#define RTL8922AE_PENDING_RECORDS 512u

struct rtl8922ae_pending_record {
  struct wifi_bss_record record;
  struct rtl8922ae_ppdu_identity identity;
  uint8_t ie_channel;
  uint16_t next;
};

struct rtl8922ae_pending_group {
  uint16_t head, tail;
  uint8_t counter;
};

struct rtl8922ae_discovery {
  struct wifi_bss_record records[WIFI_SCAN_RECORDS];
  struct rtl8922ae_pending_record pending[RTL8922AE_PENDING_RECORDS];
  struct rtl8922ae_pending_group group[2];
  size_t count;
  uint32_t channels, dropped_records, unattributed, reports, matched;
  uint16_t free_head, pending_count, pending_peak;
  uint8_t expected_channel;
  bool collecting;
};

_Static_assert(sizeof(struct rtl8922ae_pending_record) <= 64,
    "RTL8922AE pending candidate budget");

/* Worker-owned, fixed backing. No borrowed frame or DMA pointer survives a
 * callback. Reset clears the operation and both BBs' PPDU correlation state. */
void rtl8922ae_discovery_init(struct rtl8922ae_discovery *discovery);
void rtl8922ae_discovery_reset(struct rtl8922ae_discovery *discovery);
void rtl8922ae_discovery_start(struct rtl8922ae_discovery *discovery,
    uint32_t channels);
/* Caller has retired the captured RX endpoint before normal publication.
 * Missing reports use fixed-channel fallback only for single-channel scans. */
void rtl8922ae_discovery_stop(struct rtl8922ae_discovery *discovery);
void rtl8922ae_discovery_receive(struct rtl8922ae_discovery *discovery,
    const uint8_t *descriptor, size_t descriptor_bytes,
    const uint8_t *frame, size_t frame_bytes);
void rtl8922ae_discovery_report(struct rtl8922ae_discovery *discovery,
    const uint8_t *descriptor, size_t descriptor_bytes,
    const uint8_t *payload, size_t payload_bytes);

#endif
