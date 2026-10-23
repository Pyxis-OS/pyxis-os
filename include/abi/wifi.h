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

/* Optional boot observations, never command or RF authority. Validity records
 * successful reads/selection; state records completed actions, including when
 * a later stage fails. Unknown fields are zero, not fabricated defaults. */
#define WIFI_BOOT_RESET_VALID (UINT32_C(1) << 0)
#define WIFI_BOOT_CUT_VALID (UINT32_C(1) << 1)
#define WIFI_BOOT_SECURITY_VALID (UINT32_C(1) << 2)
#define WIFI_BOOT_IMAGE_VALID (UINT32_C(1) << 3)
#define WIFI_BOOT_BOARD_VALID (UINT32_C(1) << 4)
#define WIFI_BOOT_VALID_MASK UINT32_C(0x1f)
#define WIFI_BOOT_ATTEMPTED (UINT32_C(1) << 0)
#define WIFI_BOOT_FIRMWARE_READY (UINT32_C(1) << 1)
#define WIFI_BOOT_PRE_DEINIT (UINT32_C(1) << 2)
#define WIFI_BOOT_MAC_OFF (UINT32_C(1) << 3)
#define WIFI_BOOT_PCI_STOP (UINT32_C(1) << 4)
#define WIFI_BOOT_STATE_MASK UINT32_C(0x1f)
#define WIFI_RESET_NONE UINT32_C(0)
#define WIFI_RESET_FLR UINT32_C(1)
#define WIFI_RESET_POWER UINT32_C(2)

struct wifi_boot_facts {
  uint32_t valid, state, reset_method, pci_device_capabilities;
  uint16_t pci_power_control;
  uint8_t live_cut, secure_boot;
  uint8_t firmware_version[4];
  uint8_t rfe, xtal, tx_nss, rx_nss, tx_antennas, rx_antennas;
  uint8_t board_country[2];
};

#define WIFI_INPUT_UNKNOWN UINT32_C(0)
#define WIFI_INPUT_PENDING UINT32_C(1)
#define WIFI_INPUT_ABSENT UINT32_C(2)
#define WIFI_INPUT_VALID UINT32_C(3)
#define WIFI_INPUT_DISABLED UINT32_C(4)
#define WIFI_INPUT_UNSUPPORTED UINT32_C(5)
#define WIFI_INPUT_MALFORMED UINT32_C(6)
#define WIFI_INPUT_FAILED UINT32_C(7)
#define WIFI_RADIO_INPUTS_NONE UINT32_C(0)
#define WIFI_RADIO_INPUTS_RTL8922AE UINT32_C(1)
#define WIFI_TABLE_POWER_DEFAULT (UINT32_C(1) << 0)

#define WIFI_RULE_NO_OFDM (UINT32_C(1) << 0)
#define WIFI_RULE_NO_CCK (UINT32_C(1) << 1)
#define WIFI_RULE_NO_INDOOR (UINT32_C(1) << 2)
#define WIFI_RULE_NO_OUTDOOR (UINT32_C(1) << 3)
#define WIFI_RULE_DFS (UINT32_C(1) << 4)
#define WIFI_RULE_PTP_ONLY (UINT32_C(1) << 5)
#define WIFI_RULE_PTMP_ONLY (UINT32_C(1) << 6)
#define WIFI_RULE_NO_IR (UINT32_C(1) << 7)
#define WIFI_RULE_NO_HT40 (UINT32_C(1) << 10)
#define WIFI_RULE_AUTO_BW (UINT32_C(1) << 11)
#define WIFI_RULE_FLAGS UINT32_C(0xcff)

struct wifi_channel_rule {
  int32_t max_eirp_mbm;
  uint32_t flags;
};

#define WIFI_TAS_STAGE_NONE UINT32_C(0)
#define WIFI_TAS_STAGE_QUERY_0 UINT32_C(1)
#define WIFI_TAS_STAGE_FUNCTION_5 UINT32_C(2)
#define WIFI_TAS_STAGE_ALIAS UINT32_C(3)
#define WIFI_TAS_REASON_NONE UINT32_C(0)
#define WIFI_TAS_REASON_LOOKUP UINT32_C(1)
#define WIFI_TAS_REASON_EVALUATION UINT32_C(2)
#define WIFI_TAS_REASON_ALLOCATION UINT32_C(3)
#define WIFI_TAS_REASON_TYPE UINT32_C(4)
#define WIFI_TAS_REASON_LENGTH UINT32_C(5)
#define WIFI_TAS_REASON_SUPPORT_MASK UINT32_C(6)
#define WIFI_TAS_REASON_SIGNATURE UINT32_C(7)
#define WIFI_TAS_REASON_REVISION UINT32_C(8)
#define WIFI_TAS_REASON_ENABLE UINT32_C(9)
#define WIFI_TAS_REASON_ALIAS UINT32_C(10)
#define WIFI_TAS_OBJECT_UNOBSERVED UINT32_C(0)
#define WIFI_TAS_OBJECT_INTEGER UINT32_C(1)
#define WIFI_TAS_OBJECT_BUFFER UINT32_C(2)
#define WIFI_TAS_OBJECT_PACKAGE UINT32_C(3)
#define WIFI_TAS_OBJECT_STRING UINT32_C(4)
#define WIFI_TAS_OBJECT_OTHER UINT32_C(5)
#define WIFI_TAS_OBJECT_VALID (UINT32_C(1) << 0)
#define WIFI_TAS_LENGTH_VALID (UINT32_C(1) << 1)
#define WIFI_TAS_SUPPORT_VALID (UINT32_C(1) << 2)
#define WIFI_TAS_ALIAS_CONFLICT (UINT32_C(1) << 3)
#define WIFI_TAS_FLAGS UINT32_C(0xf)

/* First concrete rejection, or final evaluated stage with reason NONE. The
 * support mask describes that alias's function-0 reply. No raw AML is exposed.
 * TAS remains disabled regardless of this diagnostic. */
struct wifi_tas_diagnostic {
  uint32_t stage, reason, object_type, flags;
  uint64_t length, supported;
};

#define WIFI_SAR_SOURCE_WRDS (UINT32_C(1) << 0)
#define WIFI_SAR_SOURCE_RWRD (UINT32_C(1) << 1)
#define WIFI_SAR_SOURCES (WIFI_SAR_SOURCE_WRDS | WIFI_SAR_SOURCE_RWRD)
#define WIFI_SAR_CID_HP UINT32_C(0x5048)
#define WIFI_SAR_CID_RT UINT32_C(0x5452)
#define WIFI_SAR_PROFILES 6u
/* Recognized platform formats: revision 1/2 has 5/11 subbands; HP uses four
 * antennas, RT two or four. WRDS has one profile, RWRD at most six. */
struct wifi_sar_source {
  uint32_t cid, revision, antennas, subbands, profiles;
};

/* Fixed 2.4 GHz SAR envelope in signed eighth-dBm, paths A then B. Every RWRD
 * profile and present WRDS constrain it, with the source-specific 2TX reduction.
 * VALID is a decoded input, not an applied board/regulatory/EIRP ceiling or
 * calibration qualification. Non-VALID results have no metadata or limits. */
struct wifi_sar_inputs {
  uint32_t state, sources;
  struct wifi_sar_source wrds, rwrd;
  int32_t limits_1tx_eighth_dbm[2], limits_2tx_eighth_dbm[2];
};

/* Optional input observations, not RF admission. VALID means a source was
 * decoded, not that its limits have been applied or RFK qualified. rule_channels
 * marks matching 20 MHz regdb rules within the host channel mask; other channel
 * entries are zero. Flags retain restrictions, including NO_IR/DFS/environment.
 * The table default flag identifies the explicit reference RFE-0 selection.
 * Raw platform tables and network identities are never returned here. */
struct wifi_radio_inputs {
  uint32_t kind, tables_state, tables_flags, rule_channels;
  uint32_t acpi_state, static_sar_state, dynamic_sar_state, sar_selector_state;
  uint32_t geographic_sar_state, antenna_gain_state, tas_state, reserved;
  struct wifi_channel_rule channel[WIFI_CHANNEL_24_COUNT];
  struct wifi_tas_diagnostic tas;
  struct wifi_sar_inputs sar;
};

#define WIFI_PHY_BBMCU_READY (UINT32_C(1) << 0)
#define WIFI_PHY_MAC_CONFIGURED (UINT32_C(1) << 1)
#define WIFI_PHY_CONFIGURED (UINT32_C(1) << 2)
#define WIFI_PHY_POWER_VERIFIED (UINT32_C(1) << 3)
#define WIFI_PHY_STOP_CONFIRMED (UINT32_C(1) << 4)
#define WIFI_PHY_STOP_UNCERTAIN (UINT32_C(1) << 5)
#define WIFI_PHY_RFK_REPORT_VALID (UINT32_C(1) << 6)
#define WIFI_PHY_RFKILL_VALID (UINT32_C(1) << 7)
#define WIFI_PHY_STATE_MASK UINT32_C(0xff)
#define WIFI_PHY_IDLE UINT32_C(0)
#define WIFI_PHY_FIRMWARE UINT32_C(1)
#define WIFI_PHY_TABLES UINT32_C(2)
#define WIFI_PHY_MAC UINT32_C(3)
#define WIFI_PHY_HARDWARE UINT32_C(4)
#define WIFI_PHY_CHANNEL UINT32_C(5)
#define WIFI_PHY_POWER UINT32_C(6)
#define WIFI_PHY_RFK UINT32_C(7)
#define WIFI_PHY_TSSI UINT32_C(8)
#define WIFI_PHY_STOPPING UINT32_C(9)
#define WIFI_PHY_COMPLETE UINT32_C(10)
#define WIFI_PHY_RFK_PRE_NOTIFY (UINT32_C(1) << 0)
#define WIFI_PHY_RFK_DACK (UINT32_C(1) << 1)
#define WIFI_PHY_RFK_RXDCK (UINT32_C(1) << 2)
#define WIFI_PHY_RFK_TSSI (UINT32_C(1) << 3)
#define WIFI_PHY_RFK_MASK UINT32_C(0xf)

/* Historical observations for the one preparation attempt, not live RF power
 * or scan readiness. POWER_VERIFIED means normal/RU/SAR and force/offset
 * readbacks matched the owned image before calibration. Path values use RF
 * quarter-dBm units, unlike the SAR input's eighth-dBm. COMPLETE makes
 * last_status meaningful; the preparation-only cut returns UNAVAILABLE with
 * no scan FILE even after successful calibration and confirmed stop. */
struct wifi_phy_facts {
  uint32_t state, phase, channel, region;
  int32_t sar_quarter_dbm[2];
  uint32_t power_verifications, rfkill_checks, rfk_completed, rfk_state, rfk_version;
  uint32_t thermal_default_paths, last_status, reserved;
};

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
  struct wifi_boot_facts boot;
  struct wifi_radio_inputs radio;
  struct wifi_phy_facts phy;
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
 * Upper-case hint excludes ZZ; no inferred/default host country.
 * WIFI_COUNTRY_WORLD selects wireless-regdb's world domain 00, the rules used
 * when no country is configured: no country hint, and its NO_IR channels
 * (12 and 13) admit TX only where the device profile marks them active.
 * RTL8922AE additionally requires a known regdb country and intersects the
 * channel mask with qualified hardware/platform limits. No calibration field. */
struct wifi_policy_request {
  struct message_header header;
  uint32_t channels;
  char country[2];
  uint8_t reserved[2];
};

#define WIFI_COUNTRY_WORLD "00"

static inline bool wifi_policy_country_valid(const char country[2])
{
  bool world = country[0] == '0' && country[1] == '0';
  bool upper = country[0] >= 'A' && country[0] <= 'Z' &&
      country[1] >= 'A' && country[1] <= 'Z';
  return world || (upper && !(country[0] == 'Z' && country[1] == 'Z'));
}

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
/* Passive, unauthenticated evidence only. VALID requires a complete RSN v1
 * parse, one RSN per frame, and consistent security observations for this BSS.
 * Missing VALID never authorizes credential entry. MFPC alone permits the
 * PMF-disabled PSK profile; MFPR and SPP_REQUIRED do not. */
#define WIFI_BSS_RSN_PRESENT (UINT32_C(1) << 0)
#define WIFI_BSS_RSN_VALID (UINT32_C(1) << 1)
#define WIFI_BSS_RSN_AKM_PSK (UINT32_C(1) << 2)
#define WIFI_BSS_RSN_AKM_SAE (UINT32_C(1) << 3)
#define WIFI_BSS_RSN_PAIRWISE_CCMP (UINT32_C(1) << 4)
#define WIFI_BSS_RSN_GROUP_CCMP (UINT32_C(1) << 5)
#define WIFI_BSS_RSN_MFPR (UINT32_C(1) << 6)
#define WIFI_BSS_RSN_MFPC (UINT32_C(1) << 7)
#define WIFI_BSS_RSN_SPP_REQUIRED (UINT32_C(1) << 8)
#define WIFI_BSS_RSN_FLAGS (WIFI_BSS_RSN_PRESENT | WIFI_BSS_RSN_VALID | \
    WIFI_BSS_RSN_AKM_PSK | WIFI_BSS_RSN_AKM_SAE | WIFI_BSS_RSN_PAIRWISE_CCMP | \
    WIFI_BSS_RSN_GROUP_CCMP | WIFI_BSS_RSN_MFPR | WIFI_BSS_RSN_MFPC | WIFI_BSS_RSN_SPP_REQUIRED)
#define WIFI_BSS_RSN_PSK_CCMP (WIFI_BSS_RSN_PRESENT | WIFI_BSS_RSN_VALID | \
    WIFI_BSS_RSN_AKM_PSK | WIFI_BSS_RSN_PAIRWISE_CCMP | WIFI_BSS_RSN_GROUP_CCMP)
struct wifi_bss_record {
  uint8_t bssid[6];
  uint8_t channel, ssid_bytes;
  int32_t signal_dbm;
  uint32_t capabilities;
  uint8_t ssid[32];
  uint32_t security_flags;
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
_Static_assert(sizeof(struct wifi_boot_facts) == 32, "Wi-Fi boot facts");
_Static_assert(sizeof(struct wifi_tas_diagnostic) == 32, "Wi-Fi TAS diagnostic");
_Static_assert(sizeof(struct wifi_sar_inputs) == 64, "Wi-Fi SAR inputs");
_Static_assert(sizeof(struct wifi_radio_inputs) == 256, "Wi-Fi radio inputs");
_Static_assert(sizeof(struct wifi_phy_facts) == 56, "Wi-Fi PHY facts");
_Static_assert(sizeof(struct wifi_status_reply) == 600, "Wi-Fi status reply");
_Static_assert(sizeof(struct wifi_target) == 48, "Wi-Fi target");
_Static_assert(sizeof(struct wifi_credential) == 72, "Wi-Fi credential");
_Static_assert(sizeof(struct wifi_credential_request) == 88, "Wi-Fi credential request");
_Static_assert(sizeof(struct wifi_connect_request) == 80, "Wi-Fi connect request");
_Static_assert(sizeof(struct wifi_auth_event) == 2088, "Wi-Fi auth event");
_Static_assert(sizeof(struct wifi_auth_key_request) == 64, "Wi-Fi auth key request");
_Static_assert(sizeof(struct wifi_link_status_reply) == 32, "Wi-Fi link status");

_Static_assert(sizeof(struct wifi_scan_request) == 32, "Wi-Fi scan request");
_Static_assert(sizeof(struct wifi_policy_request) == 24, "Wi-Fi policy request");
_Static_assert(sizeof(struct wifi_bss_record) == 52, "Wi-Fi BSS record");
_Static_assert(sizeof(struct wifi_scan_file_header) == 16, "Wi-Fi result header");
#endif
