#ifndef ABI_WIFI_H
#define ABI_WIFI_H

#include <abi/handle.h>
#include <abi/message.h>

#define WIFI_RIGHT_READ (UINT64_C(1) << 0)
#define WIFI_RIGHT_SCAN (UINT64_C(1) << 1)
#define WIFI_RIGHT_POLICY_ONCE (UINT64_C(1) << 2)
#define WIFI_RIGHT_CONNECT (UINT64_C(1) << 3)
#define WIFI_RIGHTS (WIFI_RIGHT_READ | WIFI_RIGHT_SCAN | WIFI_RIGHT_POLICY_ONCE | WIFI_RIGHT_CONNECT)
#define WIFI_STATUS UINT64_C(1)
#define WIFI_SCAN_START UINT64_C(2)
#define WIFI_INSTALL_POLICY UINT64_C(3)
#define WIFI_CONNECT_START UINT64_C(4)
#define WIFI_SCAN_RIGHT_COLLECT UINT64_C(1)
#define WIFI_SCAN_STATUS UINT64_C(1)
#define WIFI_SCAN_TAKE UINT64_C(2)
#define WIFI_SCAN_CANCEL UINT64_C(3)

#define WIFI_CONNECT_RIGHT_COLLECT UINT64_C(1)
#define WIFI_CONNECT_STATUS UINT64_C(1)
#define WIFI_CONNECT_TAKE UINT64_C(2)
#define WIFI_CONNECT_CANCEL UINT64_C(3)
#define WIFI_LINK_RIGHT_READ (UINT64_C(1) << 0)
#define WIFI_LINK_RIGHT_CONTROL (UINT64_C(1) << 1)
#define WIFI_LINK_STATUS UINT64_C(1)
#define WIFI_LINK_DISCONNECT UINT64_C(2)
#define WIFI_SECURITY_OPEN UINT32_C(1)
#define WIFI_CONNECT_MAX_NS UINT64_C(30000000000)
#define WIFI_LINK_ATTEMPTING (UINT32_C(1) << 0)
#define WIFI_LINK_ASSOCIATED (UINT32_C(1) << 1)
#define WIFI_LINK_OPEN (UINT32_C(1) << 2)
#define WIFI_LINK_DATA_READY (UINT32_C(1) << 3)
#define WIFI_LINK_TERMINAL (UINT32_C(1) << 4)
#define WIFI_LINK_CLOSE_REQUESTED (UINT32_C(1) << 5)

#define WIFI_PRESENT (UINT32_C(1) << 0)
#define WIFI_ENABLED (UINT32_C(1) << 1)
#define WIFI_PREPARED (UINT32_C(1) << 2)
#define WIFI_READY (UINT32_C(1) << 3)
#define WIFI_RFKILL (UINT32_C(1) << 4)
#define WIFI_FAILED (UINT32_C(1) << 5)
#define WIFI_POLICY_INSTALLED (UINT32_C(1) << 6)
#define WIFI_SCAN_COMPLETE (UINT32_C(1) << 0)
#define WIFI_SCAN_TAKEN (UINT32_C(1) << 1)
#define WIFI_SCAN_CANCEL_REQUESTED (UINT32_C(1) << 2)
#define WIFI_SCAN_CHANNELS UINT32_C(0x7ff)
#define WIFI_SCAN_RECORDS 128u
#define WIFI_SCAN_MAX_NS UINT64_C(15000000000)
#define WIFI_DETAIL_BYTES 96u

/* READ is observational. Counters expose no nearby SSID/BSSID or scan bytes.
 * dma_bytes is actual retained allocation extents, not a predicted budget. */
struct wifi_status_reply {
  uint32_t flags, controller_id;
  uint32_t pci_bus, pci_device, pci_function, reserved;
  uint64_t dma_bytes, firmware_dma_bytes, interrupts, received_packets;
  uint32_t rx_entries, rx_buffers, command_entries, command_queues;
  uint32_t rx_high_water, command_high_water, allowed_channels, policy_channels;
  char country[2];
  uint8_t reserved_country[6];
  char detail[WIFI_DETAIL_BYTES];
  uint64_t tx_queued, tx_completed, tx_failed, tx_dropped, rx_queued, rx_dropped;
  uint32_t data_tx_pending, data_tx_high_water, data_rx_high_water, data_ring_entries;

};

/* SCAN alone, no policy/packet authority. channels zero selects the controller's
 * admitted passive1-11 intersection; otherwise it must be a nonempty subset.
 * deadline zero selects the current maximum, else an absolute monotonic bound.
 * Reply installs one process-owned noncopyable/nondelegatable operation handle.
 * An admitted operation holds no user pointer or process-table loan. */
struct wifi_scan_request {
  struct message_header header;
  uint64_t deadline_ns;
  uint32_t channels, reserved;
};
struct wifi_scan_start_reply { handle_t operation; };

/* Boot init only, not inherited. Installs a narrowing passive channel profile
 * once; passive scanning may operate without it. TX requires this channel policy
 * and live NVM/firmware/platform restrictions. Country is an explicit host hint,
 * never an MCC setter or an override of the authoritative firmware LAR profile.
 * Upper-case hint excludes ZZ; no inferred/default host country. */
struct wifi_policy_request {
  struct message_header header;
  uint32_t channels;
  char country[2];
  uint8_t reserved[2];
};

/* STATUS never consumes. status is CALL_WOULD_BLOCK before terminal drain,
 * then the operation's explicit call_status (including FILE_TOO_LARGE, timed
 * out, abandoned or uncertain). TAKE is nonblocking and installs an immutable
 * FILE only on successful completion; local admission failure preserves it.
 * Readiness COMPLETE means terminal drain/quarantine, not just an interrupt. */
struct wifi_scan_status_reply {
  uint32_t flags, status, record_count, reserved;
};
struct wifi_scan_take_reply { handle_t file; };

/* Immutable FILE bytes: one header followed by count records. SSIDs are counted
 * bytes, not NUL-terminated text. Reserved fields zero. No hidden global cache. */
struct wifi_scan_file_header {
  uint32_t count, record_bytes, channels, reserved;
};
struct wifi_bss_record {
  uint8_t bssid[6];
  uint8_t channel, ssid_bytes;
  int32_t signal_dbm;
  uint32_t capabilities;
  uint8_t ssid[32];
};

/* Copied and revalidated before effects. The request cannot install country,
 * broaden channel permissions, configure L3 or select another controller. */
struct wifi_open_target {
  uint8_t bssid[6];
  uint8_t channel, ssid_bytes;
  uint8_t ssid[32];
  uint32_t security, reserved;
};
struct wifi_connect_request {
  struct message_header header;
  uint64_t deadline_ns;
  struct wifi_open_target target;
};
struct wifi_connect_start_reply { handle_t operation; };
struct wifi_connect_status_reply { uint32_t flags, status, reserved[2]; };
struct wifi_connect_take_reply { handle_t link; };
/* No nearby identity is exposed by observation. DATA_READY is L2 admission,
 * not address readiness or cryptographic/WPA authentication. */
struct wifi_link_status_reply {
  uint64_t generation, interface_id;
  uint32_t controller_id, flags, status, reserved;
};
_Static_assert(sizeof(struct wifi_open_target) == 48, "Wi-Fi open target");
_Static_assert(sizeof(struct wifi_connect_request) == 72, "Wi-Fi connect request");
_Static_assert(sizeof(struct wifi_link_status_reply) == 32, "Wi-Fi link status");

_Static_assert(sizeof(struct wifi_scan_request) == 32, "Wi-Fi scan request");
_Static_assert(sizeof(struct wifi_policy_request) == 24, "Wi-Fi policy request");
_Static_assert(sizeof(struct wifi_bss_record) == 48, "Wi-Fi BSS record");
_Static_assert(sizeof(struct wifi_scan_file_header) == 16, "Wi-Fi result header");
#endif
