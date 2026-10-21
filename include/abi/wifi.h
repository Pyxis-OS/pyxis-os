#ifndef ABI_WIFI_H
#define ABI_WIFI_H

#include <abi/handle.h>
#include <abi/message.h>

#define WIFI_RIGHT_READ (UINT64_C(1) << 0)
#define WIFI_RIGHT_SCAN (UINT64_C(1) << 1)
#define WIFI_RIGHT_POLICY_ONCE (UINT64_C(1) << 2)
#define WIFI_RIGHT_CONNECT (UINT64_C(1) << 3)
/* Private trusted-startup setup authority; never part of ordinary WIFI_RIGHTS. */
#define WIFI_RIGHT_SUPPLICANT (UINT64_C(1) << 4)
#define WIFI_RIGHTS (WIFI_RIGHT_READ | WIFI_RIGHT_SCAN | WIFI_RIGHT_POLICY_ONCE | WIFI_RIGHT_CONNECT)
#define WIFI_STATUS UINT64_C(1)
#define WIFI_SCAN_START UINT64_C(2)
#define WIFI_INSTALL_POLICY UINT64_C(3)
#define WIFI_CONNECT_START UINT64_C(4)
#define WIFI_CREDENTIAL_SEAL UINT64_C(5)
#define WIFI_OPEN_AUTH UINT64_C(6)

#define WIFI_AUTH_RIGHT_RUN (UINT64_C(1) << 0)
#define WIFI_AUTH_RIGHT_OBSERVE (UINT64_C(1) << 1)
#define WIFI_AUTH_CLAIM UINT64_C(1)
#define WIFI_AUTH_STATUS UINT64_C(2)
#define WIFI_AUTH_READ UINT64_C(3)
#define WIFI_AUTH_SEND UINT64_C(4)
#define WIFI_AUTH_KEY UINT64_C(5)
#define WIFI_AUTH_AUTHORIZE UINT64_C(6)
#define WIFI_AUTH_FAIL UINT64_C(7)
#define WIFI_AUTH_CLAIMED (UINT32_C(1) << 0)
#define WIFI_AUTH_CLOSED (UINT32_C(1) << 1)
#define WIFI_AUTH_ACTIVE (UINT32_C(1) << 2)
#define WIFI_AUTH_BUSY (UINT32_C(1) << 3)
#define WIFI_AUTH_BEGIN UINT32_C(1)
#define WIFI_AUTH_EAPOL UINT32_C(2)
#define WIFI_AUTH_DISASSOC UINT32_C(3)
#define WIFI_AUTH_KEY_INSTALL UINT32_C(1)
#define WIFI_AUTH_KEY_REMOVE UINT32_C(2)
#define WIFI_AUTH_EAPOL_ENCRYPT (UINT32_C(1) << 0)
#define WIFI_AUTH_EVENT_ENCRYPTED (UINT32_C(1) << 0)
#define WIFI_AUTH_DATA_BYTES 2048u
#define WIFI_AUTH_EAPOL_BYTES 1500u
#define WIFI_AUTH_RSN_BYTES 256u

/* OPEN_AUTH requires private setup authority through a revocable root. Both
 * descendants publish atomically. OBSERVE permits metadata/CLOSED only; RUN
 * is transferable to the declared worker before CLAIM, then process-bound. */
struct wifi_auth_open_reply { handle_t auth, observation; };
struct wifi_auth_status_reply {
  uint64_t generation;
  uint32_t flags, queued, status, reserved;
};
struct wifi_auth_begin {
  uint8_t local_address[6], bssid[6], channel, ssid_bytes;
  uint16_t ap_rsn_bytes, association_rsn_bytes;
  uint8_t reserved[6], ssid[32];
  uint8_t ap_rsn[WIFI_AUTH_RSN_BYTES], association_rsn[WIFI_AUTH_RSN_BYTES];
};
/* READ is nonblocking and reserves a slot before a BEGIN can be consumed.
 * credential is one immutable READ-only FILE, never credential bytes. */
struct wifi_auth_event {
  uint64_t generation, deadline_ns;
  uint32_t kind, status, length, flags;
  handle_t credential;
  union {
    struct wifi_auth_begin begin;
    uint8_t data[WIFI_AUTH_DATA_BYTES];
  } body;
};
struct wifi_auth_send_request {
  struct message_header header;
  uint64_t generation;
  uint32_t length, flags;
  uint8_t data[WIFI_AUTH_DATA_BYTES];
};
struct wifi_auth_key_request {
  struct message_header header;
  uint64_t generation, receive_pn;
  uint32_t action, key_id, group, reserved;
  uint8_t key[16];
};
struct wifi_auth_authorize_request {
  struct message_header header;
  uint64_t generation;
  uint32_t enabled, reserved;
};
struct wifi_auth_fail_request {
  struct message_header header;
  uint64_t generation;
  uint32_t status, reserved;
};

#define WIFI_CREDENTIAL_PASSPHRASE UINT32_C(1)
#define WIFI_CREDENTIAL_PMK UINT32_C(2)
#define WIFI_CREDENTIAL_BYTES 64u
/* CONNECT authority constructs only caller-supplied bytes, not stored keys.
 * Returns an anonymous immutable native FILE (READ only); its backing is erased
 * on final release. Neither a root name nor an ordinary environment entry is
 * created. bytes past length must be zero; passphrases contain no NUL/CR/LF. */
struct wifi_credential {
  uint32_t format, length;
  uint8_t bytes[WIFI_CREDENTIAL_BYTES];
};
struct wifi_credential_request {
  struct message_header header;
  struct wifi_credential credential;
};
struct wifi_credential_reply { handle_t file; };
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
#define WIFI_SECURITY_WPA2_PSK UINT32_C(2)
#define WIFI_CONNECT_MAX_NS UINT64_C(30000000000)
#define WIFI_LINK_ATTEMPTING (UINT32_C(1) << 0)
#define WIFI_LINK_ASSOCIATED (UINT32_C(1) << 1)
#define WIFI_LINK_OPEN (UINT32_C(1) << 2)
#define WIFI_LINK_DATA_READY (UINT32_C(1) << 3)
#define WIFI_LINK_TERMINAL (UINT32_C(1) << 4)
#define WIFI_LINK_CLOSE_REQUESTED (UINT32_C(1) << 5)
#define WIFI_LINK_AUTHENTICATED (UINT32_C(1) << 6)

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
#define WIFI_CHANNEL_24_COUNT 14u
#define WIFI_SCAN_CHANNELS ((UINT32_C(1) << WIFI_CHANNEL_24_COUNT) - 1)
#define WIFI_SCAN_FALLBACK_CHANNELS UINT32_C(0x7ff)
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
 * admitted 2.4 GHz passive set; otherwise it must be a nonempty subset.
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
struct wifi_target {
  uint8_t bssid[6];
  uint8_t channel, ssid_bytes;
  uint8_t ssid[32];
  uint32_t security, reserved;
};
struct wifi_connect_request {
  struct message_header header;
  uint64_t deadline_ns;
  struct wifi_target target;
  handle_t credential; /* OPEN requires HANDLE_INVALID. WPA2 captures READ. */
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
_Static_assert(sizeof(struct wifi_target) == 48, "Wi-Fi target");
_Static_assert(sizeof(struct wifi_credential) == 72, "Wi-Fi credential");
_Static_assert(sizeof(struct wifi_credential_request) == 88, "Wi-Fi credential request");
_Static_assert(sizeof(struct wifi_connect_request) == 80, "Wi-Fi connect request");
_Static_assert(sizeof(struct wifi_auth_event) == 2088, "Wi-Fi auth event");
_Static_assert(sizeof(struct wifi_auth_key_request) == 64, "Wi-Fi auth key request");
_Static_assert(sizeof(struct wifi_link_status_reply) == 32, "Wi-Fi link status");

_Static_assert(sizeof(struct wifi_scan_request) == 32, "Wi-Fi scan request");
_Static_assert(sizeof(struct wifi_policy_request) == 24, "Wi-Fi policy request");
_Static_assert(sizeof(struct wifi_bss_record) == 48, "Wi-Fi BSS record");
_Static_assert(sizeof(struct wifi_scan_file_header) == 16, "Wi-Fi result header");
#endif
