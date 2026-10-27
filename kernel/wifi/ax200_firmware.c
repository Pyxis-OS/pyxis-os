#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/boot/options.h>
#include <kernel/log.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/wifi.h>
#include <kernel/wifi/ax200_transport.h>
#include "ax200_firmware.h"
#include "ax200_station.h"
#include "wifi_rsn.h"
#include "rtl8922ae.h"

#define AX200_PREPARATION_MS 10000u
#define AX200_COMMAND_MS 1000u
#define AX200_INIT_MS 2000u
#define AX200_ABORT_MS 2000u
#define AX200_IDLE_POLL_MS 1000u
#define AX200_SIGNAL_UNAVAILABLE (-256)
#define AX200_MCC_PENDING 8u

/* Shared publication belongs to the BSP with IF=0. The worker owns all other
 * firmware/parser state and never borrows a process table or user buffer. */
static struct {
  bool prepared, started, ready, failed, policy_installed;
  uint32_t allowed_channels, policy_channels;
  char country[2];
  const char *detail;
  struct wifi_scan_work *pending;
  struct wifi_connect_work *connection;
} driver;

struct mcc_notice {
  uint16_t country;
  uint8_t source;
};

static struct {
  bool init_complete, init_waiting, active, terminal, iteration, overflow, input_lost;
  uint32_t uid, sequence, channels;
  uint32_t rx_mpdu, rx_length_rejected;
  uint64_t generation;
  uint8_t terminal_status;
  const char *fault;
  size_t count;
  struct wifi_bss_record records[WIFI_SCAN_RECORDS];
  struct ax200_scan_command command;
  uint8_t reply[AX200_NVM_REPLY_BYTES];
  uint32_t nvm_channels[WIFI_CHANNEL_24_COUNT];
  bool lar, regulatory_valid, fallback_logged, mcc_burst;
  uint32_t nvm_passive_channels, regulatory_channels, transmit_channels, rx_antennas, tx_antennas;
  struct mcc_notice mcc[AX200_MCC_PENDING];
  size_t mcc_count;
  uint8_t address[6];
} firmware;

static void wifi_worker(void *argument);

static uint16_t read_le16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

static uint32_t read_le32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
      (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static void require_bsp(void)
{
  KASSERT(cpu_current() == cpu_bsp());
}

static void require_worker(void)
{
  uint64_t flags = cpu_save_interrupts();
  require_bsp();
  KASSERT((flags & RFLAGS_INTERRUPT_ENABLE) && kernel_task_is_current(wifi_worker, NULL));
  cpu_restore_interrupts(flags);
}

static void set_detail(const char *detail)
{
  uint64_t flags = cpu_save_interrupts();
  driver.detail = detail;
  cpu_restore_interrupts(flags);
}

static void fail_controller(const char *reason)
{
  require_worker();
  uint64_t flags = cpu_save_interrupts();
  driver.ready = false;
  driver.failed = true;
  driver.detail = reason;
  cpu_restore_interrupts(flags);
  ax200_transport_disable(reason);
}

static uint64_t command_deadline(uint64_t deadline)
{
  uint64_t bound = task_deadline_after_ms(AX200_COMMAND_MS);
  return bound < deadline ? bound : deadline;
}

static bool command(uint8_t group, uint8_t opcode, const void *payload, size_t bytes,
    void *reply, size_t capacity, size_t *received, uint64_t deadline)
{
  if (firmware.fault || task_deadline_expired(deadline)) {
    return false;
  }
  return ax200_command(group, opcode, payload, bytes, reply, capacity, received,
      command_deadline(deadline)) && !firmware.fault;
}

static bool fixed_command(uint8_t group, uint8_t opcode, const void *payload,
    size_t bytes, uint64_t deadline)
{
  return command(group, opcode, payload, bytes, NULL, 0, NULL, deadline);
}

static bool versions_supported(void)
{
  static const struct {
    uint8_t group, opcode, version;
    bool notification;
  } versions[] = {
    { AX200_NVM, AX200_NVM_COMPLETE, 1, false },
    { AX200_NVM, AX200_NVM_GET, 1, false },
    { AX200_NVM, AX200_NVM_GET, 4, true },
    { AX200_SYSTEM, AX200_SOC_CONFIG, 2, false },
    { AX200_LONG, AX200_TX_ANTENNA, 1, false },
    { AX200_LONG, AX200_BT_CONFIG, 6, false },
    { AX200_PHY, AX200_TEMPERATURE_THRESHOLDS, 1, false },
    { AX200_LONG, AX200_POWER_TABLE, 6, false },
    { AX200_LONG, AX200_MCC_UPDATE, 1, false },
    { AX200_LONG, AX200_MCC_UPDATE, 5, true },
    { AX200_LONG, AX200_SCAN_CONFIG, 5, false },
    { AX200_LONG, AX200_SCAN_REQUEST, 15, false },
    { AX200_LONG, AX200_SCAN_ABORT, 1, false },
    { AX200_LONG, AX200_BEACON_FILTER, 4, false },
    { AX200_LEGACY, AX200_RX_MPDU, 4, true },
  };
  for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
    if (ax200_command_version(versions[i].group, versions[i].opcode,
        versions[i].notification) != versions[i].version) {
      return false;
    }
  }
  return ax200_firmware_api(AX200_API_WIFI_MCC_UPDATE) &&
      ax200_firmware_api(AX200_API_WIDE_HEADER) &&
      ax200_firmware_api(AX200_API_BEACON_FILTER_V4) &&
      ax200_firmware_api(AX200_API_REGULATORY_NVM) &&
      ax200_firmware_api(AX200_API_REDUCED_SCAN_CONFIG) &&
      ax200_firmware_api(AX200_API_SCAN_CHANNEL_V2) &&
      ax200_firmware_capability(AX200_CAP_LAR) &&
      ax200_firmware_capability(AX200_CAP_UMAC_SCAN) &&
      ax200_firmware_capability(AX200_CAP_MULTIQUEUE_RX) &&
      ax200_firmware_capability(AX200_CAP_THERMAL_KILL) &&
      ax200_firmware_capability(AX200_CAP_MCC_11AX) &&
      !ax200_firmware_capability(AX200_CAP_DQA);
}

static bool wait_init(uint64_t deadline)
{
  while (!task_deadline_expired(deadline)) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll() || firmware.fault) {
      return false;
    }
    if (firmware.init_complete) {
      return true;
    }
    ax200_transport_wait(event, deadline);
  }
  return false;
}

static bool channel_permission(uint32_t flags)
{
  return (flags & AX200_CHANNEL_VALID) && !(flags & ~AX200_CHANNEL_KNOWN);
}

static bool mcc_country_valid(uint16_t country)
{
  unsigned first = country >> 8, second = country & UINT8_MAX;
  return !country || country == UINT16_C(0x3030) ||
      (first >= 'A' && first <= 'Z' && second >= 'A' && second <= 'Z');
}

static bool mcc_profile_source(uint8_t source)
{
  /* Profile provenance is not an echo of the requested source. Reserved,
   * uninitialized and test-mode profiles do not qualify station TX. */
  switch (source) {
    case AX200_MCC_OLD_FIRMWARE:
    case AX200_MCC_MANAGEMENT_ENGINE:
    case AX200_MCC_BIOS:
    case AX200_MCC_CELLULAR_HOST:
    case AX200_MCC_CELLULAR_DEVICE:
    case AX200_MCC_WIFI:
    case AX200_MCC_DEFAULT:
    case AX200_MCC_API:
    case AX200_MCC_GET_CURRENT:
      return true;
    default:
      return false;
  }
}

static void regulatory_fallback(const char *reason)
{
  firmware.regulatory_valid = false;
  firmware.transmit_channels = 0;
  uint32_t supported = firmware.lar ? WIFI_SCAN_CHANNELS : firmware.nvm_passive_channels;
  firmware.regulatory_channels = supported & WIFI_SCAN_FALLBACK_CHANNELS;
  uint64_t flags = cpu_save_interrupts();
  driver.allowed_channels = firmware.mcc_count ? 0 : firmware.regulatory_channels;
  cpu_restore_interrupts(flags);
  if (!firmware.fallback_logged) {
    kwarn("wifi: MCC profile unavailable (%s); passive fallback channels 1-11 only; TX denied\n", reason);
    firmware.fallback_logged = true;
  }
}

static void apply_regulatory(size_t received)
{
  firmware.regulatory_valid = false;
  firmware.transmit_channels = 0;
  if (received < 20) {
    regulatory_fallback("missing or truncated response");
    return;
  }
  uint32_t status = read_le32(firmware.reply);
  uint32_t count = read_le32(firmware.reply + 16);
  uint16_t reported = read_le16(firmware.reply + 4);
  uint16_t capabilities = read_le16(firmware.reply + 6);
  uint8_t source = firmware.reply[12];
  ktrace("wifi: MCC v4 bytes=%zu status=%u mcc=%x cap=%x n_channels=%u source_id=%u\n",
      received, status, reported, capabilities, count, source);
  if (status > AX200_MCC_SAME_PROFILE || count < WIFI_CHANNEL_24_COUNT ||
      count > AX200_MCC_CHANNELS || received != 20 + (size_t)count * 4 ||
      !mcc_country_valid(reported) || !mcc_profile_source(source)) {
    regulatory_fallback("refused or malformed response");
    return;
  }
  uint32_t passive = 0, active = 0;
  /* The first fourteen entries are 2.4 GHz in the pinned CC77 channel map.
   * Under LAR, the live profile replaces NVM's regulatory defaults, as in
   * iwl_init_channel_map/iwl_parse_nvm_mcc_info; no 5 GHz entry is admitted. */
  for (unsigned i = 0; i < WIFI_CHANNEL_24_COUNT; ++i) {
    uint32_t flags = read_le32(firmware.reply + 20 + 4 * i);
    if (!channel_permission(flags)) {
      continue;
    }
    passive |= UINT32_C(1) << i;
    if ((flags & AX200_CHANNEL_ACTIVE) && !(flags & AX200_CHANNEL_UNQUALIFIED)) {
      active |= UINT32_C(1) << i;
    }
  }
  if (!firmware.lar) {
    passive &= firmware.nvm_passive_channels;
    for (unsigned i = 0; i < WIFI_CHANNEL_24_COUNT; ++i) {
      uint32_t flags = firmware.nvm_channels[i];
      if (!channel_permission(flags) || !(flags & AX200_CHANNEL_ACTIVE) ||
          (flags & AX200_CHANNEL_UNQUALIFIED)) {
        active &= ~(UINT32_C(1) << i);
      }
    }
  }
  firmware.regulatory_channels = passive;
  /* Channel 14 is passive-only: this slice has no CCK-only station policy. */
  firmware.transmit_channels = active & passive & ~(UINT32_C(1) << 13);
  firmware.regulatory_valid = true;
  firmware.fallback_logged = false;
  /* The world domain is no country hint, so there is nothing to compare. */
  if (driver.policy_installed && !(driver.country[0] == '0' && driver.country[1] == '0')) {
    uint16_t configured = (uint8_t)driver.country[0] << 8 | (uint8_t)driver.country[1];
    if (reported != configured) {
      ktrace("wifi: LAR configured country=%c%c firmware country=%c%c; firmware profile authoritative\n",
          driver.country[0], driver.country[1], reported ? reported >> 8 : '0',
          reported ? reported & UINT8_MAX : '0');
    }
  }
}

/* MCC_CHUB is a hint, not a map. Query its firmware profile as in
 * iwl_mvm_rx_chub_update_mcc; configured country is never an MCC setter. */
static bool query_regulatory(uint16_t country, uint8_t source, uint64_t deadline)
{
  uint8_t request[28] = { country & UINT8_MAX, country >> 8, source };
  size_t received = 0;
  if (!command(AX200_LONG, AX200_MCC_UPDATE, request, sizeof(request), firmware.reply,
      AX200_MCC_REPLY_BYTES, &received, deadline)) {
    regulatory_fallback("command unavailable");
    return false;
  }
  apply_regulatory(received);
  uint64_t flags = cpu_save_interrupts();
  driver.allowed_channels = firmware.mcc_count ? 0 : firmware.regulatory_channels;
  cpu_restore_interrupts(flags);
  return true;
}

static bool refresh_regulatory(uint64_t deadline)
{
  /* Called only outside RX delivery and after the command slot is free. A
   * notification received by the query queues another owned, bounded hint. */
  for (unsigned processed = 0; firmware.mcc_count && processed < AX200_MCC_PENDING; ++processed) {
    if (task_deadline_expired(deadline)) {
      break;
    }
    struct mcc_notice notice = firmware.mcc[0];
    --firmware.mcc_count;
    memmove(firmware.mcc, firmware.mcc + 1, firmware.mcc_count * sizeof(*firmware.mcc));
    if (!query_regulatory(notice.country, notice.source, deadline)) {
      struct wifi_status_reply status;
      uint64_t flags = cpu_save_interrupts();
      ax200_transport_status(&status);
      cpu_restore_interrupts(flags);
      /* A deadline can expire before command admission. Submitted uncertainty
       * always marks the transport failed; an unsubmitted hint stays pending. */
      if (!firmware.fault && !(status.flags & WIFI_FAILED) && task_deadline_expired(deadline)) {
        if (firmware.mcc_count < AX200_MCC_PENDING) {
          firmware.mcc[firmware.mcc_count++] = notice;
        } else {
          firmware.mcc_burst = true;
        }
        break;
      }
      return false;
    }
  }
  uint64_t flags = cpu_save_interrupts();
  driver.allowed_channels = firmware.mcc_count ? 0 : firmware.regulatory_channels;
  cpu_restore_interrupts(flags);
  return true;
}

static bool prepare_firmware(void)
{
  uint64_t deadline = task_deadline_after_ms(AX200_PREPARATION_MS);
  set_detail("loading pinned AX200 firmware");
  if (!ax200_transport_boot(deadline) || !versions_supported()) {
    return false;
  }
  firmware.init_waiting = true;
  uint32_t init_flags = AX200_INIT_NVM;
  uint32_t zero = 0;
  if (!fixed_command(AX200_SYSTEM, AX200_INIT_EXTENDED, &init_flags, sizeof(init_flags), deadline) ||
      !fixed_command(AX200_NVM, AX200_NVM_COMPLETE, &zero, sizeof(zero), deadline)) {
    return false;
  }
  uint64_t init_deadline = task_deadline_after_ms(AX200_INIT_MS);
  if (init_deadline > deadline) {
    init_deadline = deadline;
  }
  if (!wait_init(init_deadline)) {
    return false;
  }
  firmware.init_waiting = false;
  set_detail("reading live AX200 NVM restrictions");
  size_t received = 0;
  if (!command(AX200_NVM, AX200_NVM_GET, &zero, sizeof(zero), firmware.reply,
      sizeof(firmware.reply), &received, deadline) || received != AX200_NVM_REPLY_BYTES) {
    return false;
  }
  uint32_t general = read_le32(firmware.reply);
  uint32_t sku = read_le32(firmware.reply + 8);
  uint32_t tx = read_le32(firmware.reply + 12);
  uint32_t rx = read_le32(firmware.reply + 16);
  firmware.lar = read_le32(firmware.reply + 20) != 0;
  uint32_t channels = read_le32(firmware.reply + 24);
  if ((general & AX200_NVM_EMPTY_OTP) || !(sku & AX200_NVM_BAND_24) ||
      channels < WIFI_CHANNEL_24_COUNT || channels > AX200_NVM_CHANNELS) {
    return false;
  }
  tx &= (ax200_firmware_phy() >> AX200_FW_TX_CHAIN_SHIFT) & AX200_FW_CHAIN_MASK;
  rx &= (ax200_firmware_phy() >> AX200_FW_RX_CHAIN_SHIFT) & AX200_FW_CHAIN_MASK;
  if (!tx || !rx || (tx & ~AX200_VALID_ANTENNAS) || (rx & ~AX200_VALID_ANTENNAS)) {
    return false;
  }
  firmware.rx_antennas = rx;
  firmware.tx_antennas = tx;
  uint32_t admitted = 0;
  for (unsigned i = 0; i < WIFI_CHANNEL_24_COUNT; ++i) {
    uint32_t flags = read_le32(firmware.reply + 28 + 4 * i);
    firmware.nvm_channels[i] = flags;
    if (channel_permission(flags)) {
      admitted |= UINT32_C(1) << i;
    }
  }
  if (!ax200_transport_address(firmware.address)) {
    return false;
  }
  if (!fixed_command(AX200_LONG, AX200_TX_ANTENNA, &tx, sizeof(tx), deadline)) {
    return false;
  }
  uint32_t coexistence[2] = { AX200_BT_WIFI, 0 };
  uint32_t soc[2] = { AX200_SOC_DISCRETE, 0 };
  uint32_t thermal[5] = { 0 };
  uint32_t power = 0;
  if (!fixed_command(AX200_LONG, AX200_BT_CONFIG, coexistence, sizeof(coexistence), deadline) ||
      !fixed_command(AX200_SYSTEM, AX200_SOC_CONFIG, soc, sizeof(soc), deadline)) {
    return false;
  }
  if (ax200_transport_ltr_enabled()) {
    /* The pinned reference uses this 32-byte layout for advertised v3. */
    uint32_t ltr[8] = { AX200_LTR_ENABLE };
    if (ax200_command_version(AX200_LONG, AX200_LTR_CONFIG, false) != 3 ||
        !fixed_command(AX200_LONG, AX200_LTR_CONFIG, ltr, sizeof(ltr), deadline)) {
      return false;
    }
  }
  if (!fixed_command(AX200_PHY, AX200_TEMPERATURE_THRESHOLDS, thermal, sizeof(thermal), deadline) ||
      !fixed_command(AX200_LONG, AX200_POWER_TABLE, &power, sizeof(power), deadline)) {
    return false;
  }
  firmware.nvm_passive_channels = admitted;
  if (firmware.lar) {
    if (!query_regulatory(UINT16_C(0x5a5a), AX200_MCC_GET_CURRENT, deadline)) {
      return false;
    }
  } else {
    regulatory_fallback("live LAR profile unavailable");
  }
  uint32_t scan_config[3] = { 0, tx, rx };
  uint32_t beacon_filter[15] = { 0 };
  if (!fixed_command(AX200_LONG, AX200_SCAN_CONFIG, scan_config, sizeof(scan_config), deadline) ||
      !fixed_command(AX200_LONG, AX200_BEACON_FILTER, beacon_filter, sizeof(beacon_filter), deadline) ||
      !refresh_regulatory(deadline)) {
    return false;
  }
  uint64_t flags = cpu_save_interrupts();
  driver.allowed_channels = firmware.regulatory_channels;
  driver.ready = true;
  driver.detail = "firmware-selected 2.4 GHz passive channels ready; no scan requested";
  cpu_restore_interrupts(flags);
  return true;
}

static void build_scan(uint32_t uid, uint32_t channels)
{
  struct ax200_scan_command *request = &firmware.command;
  memset(request, 0, sizeof(*request));
  request->uid = uid;
  request->priority = AX200_SCAN_PRIORITY;
  request->general.flags = AX200_SCAN_PASS_ALL | AX200_SCAN_ITERATION_NOTIFY |
      AX200_SCAN_ADAPTIVE_DWELL | AX200_SCAN_FORCE_PASSIVE;
  request->general.active_dwell[0] = request->general.active_dwell[1] = AX200_SCAN_ACTIVE_DWELL;
  request->general.passive_dwell[0] = request->general.passive_dwell[1] = AX200_SCAN_PASSIVE_DWELL;
  request->general.adaptive_24 = 2;
  request->general.adaptive_5 = 8;
  request->general.adaptive_social = 10;
  request->general.adaptive_budget = 300;
  request->general.priority = AX200_SCAN_PRIORITY;
  request->channels.flags = AX200_SCAN_CHANNEL_ORDER;
  request->channels.ap_override[0] = 10;
  request->channels.ap_override[1] = 2;
  for (unsigned i = 0; i < WIFI_CHANNEL_24_COUNT; ++i) {
    if (!(channels & (UINT32_C(1) << i))) {
      continue;
    }
    struct ax200_scan_channel *channel = &request->channels.channels[request->channels.count++];
    channel->channel = i + 1;
    channel->band = AX200_BAND_24;
    channel->iterations = 1;
  }
  request->periodic.schedule[0].iterations = 1;
  /* FORCE_PASSIVE makes this source-shaped probe template inert. It is never
   * submitted as a TX command, and all directed identifiers remain zero. */
  struct ax200_probe_template *probe = &request->probe.frame;
  uint8_t *frame = probe->bytes;
  frame[0] = AX200_FRAME_PROBE_REQUEST;
  memset(frame + 4, 0xff, 6);
  memcpy(frame + 10, firmware.address, 6);
  memset(frame + 16, 0xff, 6);
  probe->mac.length = 26;
  static const uint8_t rates24[] = {
    1, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
    50, 4, 0x30, 0x48, 0x60, 0x6c,
  };
  probe->band[0].offset = 26;
  memcpy(frame + 26, rates24, sizeof(rates24));
  size_t offset = 26 + sizeof(rates24);
  if (ax200_firmware_capability(AX200_CAP_DS_IE)) {
    frame[offset++] = AX200_IE_DS;
    frame[offset++] = 1;
    frame[offset++] = 0;
  }
  probe->band[0].length = offset - 26;
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

static void collect_beacon(const uint8_t *payload, size_t bytes)
{
  if (!firmware.active || firmware.terminal || firmware.overflow) {
    return;
  }
  ++firmware.rx_mpdu;
  if (bytes < AX200_RX_DESCRIPTOR_BYTES) {
    ++firmware.rx_length_rejected;
    return;
  }
  uint32_t status = read_le32(payload + offsetof(struct ax200_rx_descriptor_v1, status));
  uint16_t frame_bytes = read_le16(payload);
  if (!(status & AX200_RX_OVERRUN_OK)) {
    firmware.input_lost = true;
    return;
  }
  if (frame_bytes > bytes - AX200_RX_DESCRIPTOR_BYTES) {
    ++firmware.rx_length_rejected;
    return;
  }
  if ((status & (AX200_RX_CRC_OK | AX200_RX_OVERRUN_OK)) !=
      (AX200_RX_CRC_OK | AX200_RX_OVERRUN_OK) ||
      (status & (AX200_RX_SECURITY_MASK | AX200_RX_RFKILL)) ||
      (payload[3] & AX200_RX_AMSDU)) {
    return;
  }
  uint8_t channel = payload[offsetof(struct ax200_rx_descriptor_v1, channel)];
  if (!channel || channel > WIFI_CHANNEL_24_COUNT || !(firmware.channels & (UINT32_C(1) << (channel - 1)))) {
    return;
  }
  const uint8_t *frame = payload + AX200_RX_DESCRIPTOR_BYTES;
  size_t header = AX200_FRAME_HEADER_BYTES + ((payload[3] & AX200_RX_PAD) ? 2 : 0);
  if (frame_bytes < header + AX200_FRAME_BEACON_BYTES ||
      (frame[0] != AX200_FRAME_BEACON && frame[0] != AX200_FRAME_PROBE_REPLY) ||
      (frame[1] & (AX200_FRAME_DIRECTION_MASK | AX200_FRAME_PROTECTED)) || (frame[16] & 1)) {
    return;
  }
  /* Target confirmation never spends result-table capacity on other BSSes. */
  if (driver.connection &&
      (channel != driver.connection->target.channel ||
       memcmp(frame + 16, driver.connection->target.bssid, 6))) {
    return;
  }
  static const uint8_t zero_bssid[6];
  if (!memcmp(frame + 16, zero_bssid, sizeof(zero_bssid))) {
    return;
  }
  const uint8_t *body = frame + header;
  const uint8_t *ssid = NULL;
  size_t ssid_bytes = 0;
  bool rates = false, duplicate_rsn = false;
  uint32_t security_flags = 0;
  size_t offset = header + AX200_FRAME_BEACON_BYTES;
  while (offset < frame_bytes) {
    if (frame_bytes - offset < 2) {
      return;
    }
    uint8_t id = frame[offset], length = frame[offset + 1];
    offset += 2;
    if (length > frame_bytes - offset) {
      return;
    }
    const uint8_t *value = frame + offset;
    if (id == AX200_IE_SSID) {
      if (length > 32 || (ssid && (length != ssid_bytes || memcmp(ssid, value, length)))) {
        return;
      }
      ssid = value;
      ssid_bytes = length;
    } else if (id == AX_RSN_ELEMENT_ID) {
      duplicate_rsn |= (security_flags & WIFI_BSS_RSN_PRESENT) != 0;
      security_flags |= wifi_rsn_security(value - 2, (size_t)length + 2);
    } else if (id == AX200_IE_RATES) {
      if (!length || length > 8) {
        return;
      }
      rates = true;
    } else if (id == AX200_IE_DS) {
      if (length != 1 || value[0] != channel) {
        return;
      }
    } else if (id == AX200_IE_HT_OPERATION) {
      if (length != 22 || value[0] != channel) {
        return;
      }
    }
    offset += length;
  }
  if (!ssid || !rates) {
    return;
  }
  if (duplicate_rsn) {
    security_flags &= ~WIFI_BSS_RSN_VALID;
  }
  uint8_t energy_a = payload[offsetof(struct ax200_rx_descriptor_v1, energy_a)];
  uint8_t energy_b = payload[offsetof(struct ax200_rx_descriptor_v1, energy_b)];
  int signal_a = energy_a ? -(int)energy_a : AX200_SIGNAL_UNAVAILABLE;
  int signal_b = energy_b ? -(int)energy_b : AX200_SIGNAL_UNAVAILABLE;
  int signal = signal_a > signal_b ? signal_a : signal_b;
  struct wifi_bss_record *record = NULL;
  for (size_t i = 0; i < firmware.count; ++i) {
    if (firmware.records[i].channel == channel &&
        !memcmp(firmware.records[i].bssid, frame + 16, 6)) {
      record = &firmware.records[i];
      break;
    }
  }
  if (!record) {
    if (firmware.count == WIFI_SCAN_RECORDS) {
      firmware.overflow = true;
      return;
    }
    record = &firmware.records[firmware.count++];
    memcpy(record->bssid, frame + 16, 6);
    record->channel = channel;
    record->signal_dbm = signal;
    record->capabilities = read_le16(body + 10);
    record->security_flags = security_flags;
    record->ssid_bytes = ssid_bytes;
    memcpy(record->ssid, ssid, ssid_bytes);
  } else {
    /* Conflicting security observations remain unusable for this scan. */
    if (record->security_flags != security_flags) {
      record->security_flags = (record->security_flags | security_flags) & ~WIFI_BSS_RSN_VALID;
    }
    /* Repeated observations do not consume another record. Keep the strongest
     * measurement and the first disclosed name; a hidden name may be filled. */
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

void ax200_firmware_notify(uint8_t group, uint8_t opcode, const uint8_t *payload, size_t bytes)
{
  require_worker();
  ax200_station_notify(group, opcode, payload, bytes);
  if ((group == AX200_PHY && opcode == AX200_THERMAL_KILL) ||
      ((group == AX200_LEGACY || group == AX200_LONG) && opcode == 0x02) ||
      (group == AX200_SYSTEM && opcode == 0xff)) {
    firmware.fault = "firmware fault or thermal kill; reboot required";
    return;
  }
  if (group != AX200_LEGACY && group != AX200_LONG) {
    return;
  }
  if (opcode == AX200_INIT_COMPLETE && firmware.init_waiting) {
    /* Like iwx, this is a completion event, not a zero-length wire schema.
     * The transport has already bounded the notification's declared extent. */
    firmware.init_complete = true;
  } else if (opcode == AX200_MCC_CHANGED) {
    if (bytes != 4 || payload[3] || !mcc_country_valid(read_le16(payload)) ||
        payload[2] > 9 || payload[2] == 6 || payload[2] == 8) {
      firmware.fault = "invalid firmware regulatory notification; reboot required";
      return;
    }
    struct mcc_notice notice = {read_le16(payload), payload[2]};
    bool queued = false;
    for (size_t i = 0; i < firmware.mcc_count; ++i) {
      if (firmware.mcc[i].country == notice.country && firmware.mcc[i].source == notice.source) {
        queued = true;
        break;
      }
    }
    if (!queued) {
      if (firmware.mcc_count == AX200_MCC_PENDING) {
        /* Hints are not maps. Keep the newest hint, bound the work and retire
         * an active link cleanly rather than faulting the entire controller. */
        firmware.mcc_burst = true;
        firmware.mcc[AX200_MCC_PENDING - 1] = notice;
      } else {
        firmware.mcc[firmware.mcc_count++] = notice;
      }
    }
    /* No new admission while the firmware's new restrictions are unknown. */
    uint64_t flags = cpu_save_interrupts();
    driver.allowed_channels = 0;
    firmware.transmit_channels = 0;
    cpu_restore_interrupts(flags);
  } else if (opcode == AX200_RX_MPDU) {
    collect_beacon(payload, bytes);
  } else if ((opcode == AX200_SCAN_COMPLETE || opcode == AX200_SCAN_ITERATION) && firmware.active) {
    ktrace("wifi: scan notification opcode=%x bytes=%zu uid=%x expected=%x count-or-schedule=%u status=%u byte7=%u\n",
        opcode, bytes, bytes >= 4 ? read_le32(payload) : 0, firmware.uid,
        bytes >= 8 ? payload[4] : 0,
        bytes >= 8 ? payload[opcode == AX200_SCAN_COMPLETE ? 6 : 5] : 0,
        bytes >= 8 ? payload[7] : 0);
    if (bytes < 16 || read_le32(payload) != firmware.uid) {
      firmware.fault = "unmatched scan completion; reboot required";
      return;
    }
    if (opcode == AX200_SCAN_COMPLETE) {
      if (bytes != 16 || payload[4] != 0 || payload[5] > 1 ||
          (payload[6] != AX200_SCAN_DONE && payload[6] != AX200_SCAN_ABORTED) ||
          (firmware.terminal && firmware.terminal_status != payload[6])) {
        firmware.fault = "invalid terminal scan completion; reboot required";
        return;
      }
      firmware.terminal_status = payload[6];
      firmware.terminal = true;
    } else {
      uint8_t count = payload[4];
      if (count > __builtin_popcount(firmware.channels)) {
        firmware.fault = "invalid scan iteration count; reboot required";
        return;
      }
      if (count && (!payload[7] || payload[7] > WIFI_CHANNEL_24_COUNT ||
          !(firmware.channels & (UINT32_C(1) << (payload[7] - 1))))) {
        firmware.fault = "invalid last scan iteration channel; reboot required";
        return;
      }
      /* iwx v1 and iwlwifi v2 share this 16-byte prefix. Like Linux's
       * iteration handler, consume no optional per-channel tail: its presence
       * is not a completion/drain proof. RX transport bounds the whole packet.
       * Only matching terminal0f permits reuse; iteration status stays opaque. */
      firmware.iteration = true;
    }
  }
}

static void finish_scan(struct wifi_scan_work *work, enum call_status status)
{
  ktrace("wifi: scan receive MPDU=%u length-rejected=%u records=%zu outcome=%u\n",
      firmware.rx_mpdu, firmware.rx_length_rejected, firmware.count, (unsigned)status);
  uint64_t flags = cpu_save_interrupts();
  KASSERT(driver.pending == work && work->generation == firmware.generation);
  driver.pending = NULL;
  firmware.active = false;
  wifi_scan_finished(work, status, firmware.channels,
      status == CALL_OK ? firmware.records : NULL, status == CALL_OK ? firmware.count : 0);
  cpu_restore_interrupts(flags);
  memzero_explicit(firmware.records, sizeof(firmware.records));
  firmware.count = 0;
}

static void run_scan(struct wifi_scan_work *work)
{
  firmware.generation = work->generation;
  firmware.channels = work->channels;
  firmware.terminal = false;
  firmware.iteration = false;
  firmware.overflow = false;
  firmware.input_lost = false;
  firmware.count = 0;
  firmware.rx_mpdu = firmware.rx_length_rejected = 0;
  uint64_t flags = cpu_save_interrupts();
  enum call_status stopped = wifi_scan_stop_reason(work);
  uint32_t admitted = driver.allowed_channels;
  if (driver.policy_installed) {
    admitted &= driver.policy_channels;
  }
  cpu_restore_interrupts(flags);
  if (stopped != CALL_OK) {
    finish_scan(work, stopped);
    return;
  }
  if ((firmware.channels & ~admitted) || ax200_transport_rfkill()) {
    finish_scan(work, CALL_UNAVAILABLE);
    return;
  }
  if (task_deadline_expired(work->deadline_ns)) {
    finish_scan(work, CALL_TIMED_OUT);
    return;
  }
  if (firmware.sequence == UINT32_C(0xffffff)) {
    fail_controller("scan UID capacity exhausted; reboot required");
    finish_scan(work, CALL_UNAVAILABLE);
    return;
  }
  firmware.uid = ++firmware.sequence << 8;
  firmware.active = true;
  build_scan(firmware.uid, firmware.channels);
  flags = cpu_save_interrupts();
  bool begin = wifi_scan_begin(work);
  stopped = wifi_scan_stop_reason(work);
  cpu_restore_interrupts(flags);
  if (!begin) {
    finish_scan(work, stopped == CALL_OK ? CALL_DENIED : stopped);
    return;
  }
  set_detail("explicit passive scan in progress");
  if (!fixed_command(AX200_LONG, AX200_SCAN_REQUEST, &firmware.command,
      sizeof(firmware.command), work->deadline_ns)) {
    fail_controller(firmware.fault ? firmware.fault : "scan submission uncertain; reboot required");
    finish_scan(work, CALL_OUTCOME_UNKNOWN);
    return;
  }
  enum call_status result = CALL_OK;
  bool abort_sent = false;
  bool cleanup_started = false;
  uint64_t drain_deadline = work->deadline_ns;
  for (;;) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll() || firmware.fault) {
      fail_controller(firmware.fault ? firmware.fault : "scan transport failed; reboot required");
      finish_scan(work, CALL_OUTCOME_UNKNOWN);
      return;
    }
    flags = cpu_save_interrupts();
    stopped = wifi_scan_stop_reason(work);
    cpu_restore_interrupts(flags);
    if (firmware.overflow) {
      result = CALL_FILE_TOO_LARGE;
    } else if (firmware.input_lost && result == CALL_OK) {
      result = CALL_INPUT_LOST;
    } else if (stopped != CALL_OK && result == CALL_OK) {
      result = stopped;
    } else if (ax200_transport_rfkill() && result == CALL_OK) {
      result = CALL_UNAVAILABLE;
    } else if (task_deadline_expired(work->deadline_ns) && result == CALL_OK) {
      result = CALL_TIMED_OUT;
    }
    if (result == CALL_OK && (firmware.mcc_count || firmware.mcc_burst)) {
      if (!refresh_regulatory(work->deadline_ns)) {
        fail_controller(firmware.fault ? firmware.fault : "scan regulatory refresh uncertain; reboot required");
        finish_scan(work, CALL_OUTCOME_UNKNOWN);
        return;
      }
      flags = cpu_save_interrupts();
      stopped = wifi_scan_stop_reason(work);
      cpu_restore_interrupts(flags);
      if (stopped != CALL_OK) {
        result = stopped;
      } else if (firmware.mcc_count || firmware.mcc_burst ||
          (firmware.channels & ~firmware.regulatory_channels)) {
        result = CALL_UNAVAILABLE;
      }
    }
    if (result != CALL_OK && !cleanup_started) {
      cleanup_started = true;
      drain_deadline = task_deadline_after_ms(AX200_ABORT_MS);
    }
    if (firmware.terminal && ax200_transport_drained()) {
      if (result == CALL_OK && firmware.terminal_status != AX200_SCAN_DONE) {
        result = CALL_IO;
      }
      set_detail("passive scan terminal completion drained");
      finish_scan(work, result);
      return;
    }
    if (result != CALL_OK && !abort_sent && !firmware.terminal) {
      uint32_t abort[2] = { firmware.uid, 0 };
      abort_sent = true;
      if (!fixed_command(AX200_LONG, AX200_SCAN_ABORT, abort, sizeof(abort), drain_deadline)) {
        fail_controller("scan abort uncertain; reboot required");
        finish_scan(work, CALL_OUTCOME_UNKNOWN);
        return;
      }
      continue;
    }
    if (task_deadline_expired(drain_deadline)) {
      fail_controller("scan terminal completion uncertain; reboot required");
      finish_scan(work, CALL_OUTCOME_UNKNOWN);
      return;
    }
    ax200_transport_wait(event, drain_deadline);
  }
}

static void wifi_worker(void *argument)
{
  (void)argument;
  require_worker();
  if (!prepare_firmware()) {
    fail_controller(firmware.fault ? firmware.fault : "firmware preparation failed; reboot required");
    return;
  }
  for (;;) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll() || firmware.fault ||
        !refresh_regulatory(task_deadline_after_ms(AX200_COMMAND_MS))) {
      fail_controller(firmware.fault ? firmware.fault : "firmware regulatory/transport refresh failed; reboot required");
    }
    if (!firmware.mcc_count) {
      firmware.mcc_burst = false;
    }
    uint64_t flags = cpu_save_interrupts();
    struct wifi_scan_work *work = driver.pending;
    struct wifi_connect_work *connection = driver.connection;
    bool failed = driver.failed;
    cpu_restore_interrupts(flags);
    if (connection) {
      ax200_station_run(connection);
      continue;
    }
    if (work) {
      if (failed) {
        firmware.generation = work->generation;
        firmware.channels = work->channels;
        finish_scan(work, CALL_OUTCOME_UNKNOWN);
      } else {
        run_scan(work);
      }
      continue;
    }
    if (failed) {
      return;
    }
    ax200_transport_wait(event, task_deadline_after_ms(AX200_IDLE_POLL_MS));
  }
}

void wifi_prepare(const struct boot_info *boot)
{
  require_bsp();
  if (boot_options_get()->wifi_rtl8922ae) {
    rtl8922ae_prepare(boot);
    return;
  }
  driver.prepared = ax200_transport_prepare(boot);
}

void wifi_start(void)
{
  require_bsp();
  if (boot_options_get()->wifi_rtl8922ae) {
    rtl8922ae_start();
    return;
  }
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE) && !driver.started);
  driver.started = true;
  if (!driver.prepared) {
    return;
  }
  if (kernel_task_create(wifi_worker, NULL) != MM_OK) {
    driver.failed = true;
    driver.detail = "Wi-Fi worker unavailable; firmware not activated";
  }
}

void wifi_driver_status(struct wifi_status_full_reply *status)
{
  require_bsp();
  if (boot_options_get()->wifi_rtl8922ae) {
    rtl8922ae_status(status);
    return;
  }
  *status = (struct wifi_status_full_reply){0};
  struct wifi_status_reply *base = &status->base;
  ax200_transport_status(base);
  struct wifi_network_statistics data;
  wifi_driver_network_statistics(&data);
  base->tx_queued = data.tx_queued;
  base->tx_completed = data.tx_completed;
  base->tx_failed = data.tx_failed;
  base->tx_dropped = data.tx_dropped;
  base->rx_queued = data.rx_queued;
  base->rx_dropped = data.rx_dropped;
  base->data_tx_pending = data.tx_pending;
  base->data_tx_high_water = data.tx_high_water;
  base->data_rx_high_water = data.rx_high_water;
  base->data_ring_entries = driver.prepared ? data.ring_entries : 0;
  if (driver.ready) {
    base->flags |= WIFI_READY;
  }
  if (driver.failed) {
    base->flags |= WIFI_FAILED;
    base->flags &= ~WIFI_READY;
  }
  base->allowed_channels = driver.allowed_channels;
  base->policy_channels = driver.policy_installed ? driver.policy_channels : WIFI_SCAN_CHANNELS;
  if (driver.policy_installed) {
    base->flags |= WIFI_POLICY_INSTALLED;
    memcpy(base->country, driver.country, sizeof(driver.country));
  }
  if (driver.detail) {
    memset(base->detail, 0, sizeof(base->detail));
    size_t i = 0;
    while (i + 1 < sizeof(base->detail) && driver.detail[i]) {
      base->detail[i] = driver.detail[i];
      ++i;
    }
  }
}

enum call_status wifi_driver_policy(const struct wifi_policy_request *policy)
{
  require_bsp();
  if (boot_options_get()->wifi_rtl8922ae) {
    return rtl8922ae_policy(policy);
  }
  if (!wifi_policy_country_valid(policy->country) ||
      policy->reserved[0] || policy->reserved[1] || !policy->channels ||
      (policy->channels & ~WIFI_SCAN_CHANNELS)) {
    return CALL_BAD_REQUEST;
  }
  if (!driver.prepared || driver.failed) {
    return CALL_UNAVAILABLE;
  }
  if (driver.policy_installed) {
    return CALL_ALREADY_EXISTS;
  }
  if (driver.pending || driver.connection) {
    return CALL_BUSY;
  }
  driver.policy_channels = policy->channels;
  memcpy(driver.country, policy->country, sizeof(driver.country));
  driver.policy_installed = true;
  return CALL_OK;
}

enum call_status wifi_driver_scan(struct wifi_scan_work *work)
{
  if (boot_options_get()->wifi_rtl8922ae) return rtl8922ae_scan(work);
  require_bsp();
  if (!driver.ready || driver.failed || ax200_transport_rfkill()) {
    return CALL_UNAVAILABLE;
  }
  if (driver.pending || driver.connection) {
    return CALL_BUSY;
  }
  uint32_t channels = driver.allowed_channels;
  if (driver.policy_installed) {
    channels &= driver.policy_channels;
  }
  if (!channels) {
    return CALL_UNAVAILABLE;
  }
  if (work->channels & ~channels) {
    return CALL_BAD_REQUEST;
  }
  if (!work->channels) {
    work->channels = channels;
  }
  uint64_t now = arch_monotonic_ns();
  uint64_t maximum = now > UINT64_MAX - WIFI_SCAN_MAX_NS ? UINT64_MAX : now + WIFI_SCAN_MAX_NS;
  if (!work->deadline_ns || work->deadline_ns > maximum) {
    work->deadline_ns = maximum;
  }
  if (work->deadline_ns <= now) {
    return CALL_TIMED_OUT;
  }
  driver.pending = work;
  ax200_transport_notify();
  return CALL_OK;
}

void wifi_driver_cancel(struct wifi_scan_work *work)
{
  if (boot_options_get()->wifi_rtl8922ae) {
    rtl8922ae_cancel(work);
    return;
  }
  atomic_store_explicit(&work->cancel, true, memory_order_release);
  ax200_transport_notify();
}

bool ax200_radio_command(uint8_t group, uint8_t opcode, const void *payload,
    size_t bytes, void *reply, size_t capacity, size_t *received, uint64_t deadline)
{
  return command(group, opcode, payload, bytes, reply, capacity, received, deadline);
}

uint32_t ax200_radio_rx_antennas(void)
{
  return firmware.rx_antennas;
}

bool ax200_radio_faulted(void)
{
  return firmware.fault || driver.failed;
}

void ax200_radio_quarantine(const char *reason)
{
  fail_controller(reason);
  ax200_transport_stop(task_deadline_after_ms(200));
}

void ax200_radio_connection_clear(struct wifi_connect_work *work)
{
  KASSERT(!cpu_interrupts_enabled() && driver.connection == work);
  driver.connection = NULL;
}

bool ax200_radio_transmit_policy(unsigned channel, uint64_t deadline, const char **failure)
{
  *failure = "boot channel policy unavailable; terminal drained";
  if (!driver.policy_installed || channel < 1 || channel >= WIFI_CHANNEL_24_COUNT ||
      !(driver.policy_channels & (1u << (channel - 1))) || firmware.mcc_count) {
    return false;
  }
  /* ZZ/GET_CURRENT reads LAR without installing the configured country. */
  if (!query_regulatory(UINT16_C(0x5a5a), AX200_MCC_GET_CURRENT, deadline)) {
    *failure = "firmware country MCC command failed; terminal drained";
    return false;
  }
  *failure = "valid firmware regulatory TX permission unavailable; terminal drained";
  if (!firmware.regulatory_valid) {
    return false;
  }
  ktrace("wifi: MCC selected channel=%u firmware TX-mask=%x NVM_flags=%x\n",
      channel, firmware.transmit_channels, firmware.nvm_channels[channel - 1]);
  *failure = "firmware or boot channel denies TX; terminal drained";
  return !firmware.mcc_count &&
      (firmware.transmit_channels & driver.policy_channels & (UINT32_C(1) << (channel - 1)));
}

bool ax200_radio_target_scan(struct wifi_connect_work *work, uint64_t scan_deadline)
{
  if (firmware.sequence == UINT32_C(0xffffff)) {
    return false;
  }
  while (!ax200_transport_drained()) {
    if (task_deadline_expired(scan_deadline) || !ax200_transport_poll() || firmware.fault) {
      return false;
    }
    kernel_task_yield_if_runnable();
  }
  uint64_t flags = cpu_save_interrupts();
  uint32_t target = UINT32_C(1) << (work->target.channel - 1);
  bool admitted = !firmware.mcc_count &&
      (target & driver.allowed_channels & driver.policy_channels);
  cpu_restore_interrupts(flags);
  if (!admitted) {
    return false;
  }
  ax200_station_observation_begin();
  firmware.channels = target;
  firmware.uid = ++firmware.sequence << 8;
  firmware.terminal = firmware.iteration = firmware.overflow = firmware.input_lost = false;
  firmware.count = 0;
  firmware.active = true;
  build_scan(firmware.uid, firmware.channels);
  bool submitted = fixed_command(AX200_LONG, AX200_SCAN_REQUEST, &firmware.command,
      sizeof(firmware.command), scan_deadline);
  bool abort_sent = false, cancelled = false;
  uint64_t deadline = scan_deadline;
  if (!submitted) {
    firmware.active = false;
    fail_controller("connection passive scan submission uncertain; reboot required");
    return false;
  }
  for (;;) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll() || firmware.fault) {
      break;
    }
    uint64_t stop_flags = cpu_save_interrupts();
    enum call_status stopped = wifi_connect_stop_reason(work);
    cpu_restore_interrupts(stop_flags);
    if (!cancelled && (stopped != CALL_OK || ax200_transport_rfkill() ||
        firmware.mcc_count || firmware.overflow || firmware.input_lost ||
        task_deadline_expired(scan_deadline))) {
      cancelled = true;
      deadline = task_deadline_after_ms(AX200_ABORT_MS);
    }
    if (firmware.terminal && ax200_transport_drained()) {
      firmware.active = false;
      memzero_explicit(firmware.records, sizeof(firmware.records));
      firmware.count = 0;
      return !cancelled && firmware.terminal_status == AX200_SCAN_DONE;
    }
    if (cancelled && !abort_sent && !firmware.terminal) {
      uint32_t abort[2] = {firmware.uid, 0};
      abort_sent = true;
      if (!fixed_command(AX200_LONG, AX200_SCAN_ABORT, abort, sizeof(abort), deadline)) {
        break;
      }
    }
    if (task_deadline_expired(deadline)) {
      break;
    }
    ax200_transport_wait(event, deadline);
  }
  firmware.active = false;
  fail_controller("connection passive scan drain uncertain; reboot required");
  return false;
}

enum call_status wifi_driver_connect(struct wifi_connect_work *work)
{
  require_bsp();
  KASSERT(!cpu_interrupts_enabled());
  if (!driver.ready || driver.failed || !driver.policy_installed || ax200_transport_rfkill()) {
    return CALL_UNAVAILABLE;
  }
  if (driver.pending || driver.connection) {
    return CALL_BUSY;
  }
  const struct wifi_target *target = &work->target;
  static const uint8_t zero[6];
  if (!work->generation ||
      (target->security != WIFI_SECURITY_OPEN && target->security != WIFI_SECURITY_WPA2_PSK) ||
      (target->security == WIFI_SECURITY_WPA2_PSK && !work->auth) || target->reserved ||
      !target->ssid_bytes || target->ssid_bytes > 32 || hidden_ssid(target->ssid, target->ssid_bytes) ||
      (target->bssid[0] & 1) || !memcmp(target->bssid, zero, sizeof(zero)) ||
      target->channel < 1 || target->channel >= WIFI_CHANNEL_24_COUNT ||
      !((driver.policy_channels & driver.allowed_channels) & (1u << (target->channel - 1)))) {
    return CALL_BAD_REQUEST;
  }
  uint64_t interface_id = ax200_station_interface_id();
  if (!interface_id) {
    return CALL_UNAVAILABLE;
  }
  work->interface_id = interface_id;
  work->controller_id = ax200_transport_controller_id();
  driver.connection = work;
  ax200_transport_notify();
  return CALL_OK;
}

void wifi_driver_disconnect(struct wifi_connect_work *work)
{
  atomic_store_explicit(&work->cancel, true, memory_order_release);
  ax200_transport_notify();
}

uint32_t ax200_radio_tx_antennas(void)
{
  return firmware.tx_antennas;
}

enum call_status ax200_radio_refresh(uint64_t deadline)
{
  require_worker();
  if (!refresh_regulatory(deadline)) {
    return CALL_OUTCOME_UNKNOWN;
  }
  bool unavailable = firmware.mcc_burst || firmware.mcc_count;
  firmware.mcc_burst = false;
  return unavailable ? CALL_UNAVAILABLE : CALL_OK;
}

bool ax200_radio_tx_allowed(unsigned channel)
{
  return !ax200_radio_faulted() && !ax200_transport_rfkill() && !firmware.mcc_count &&
      firmware.regulatory_valid && channel >= 1 && channel < WIFI_CHANNEL_24_COUNT &&
      (firmware.transmit_channels & driver.policy_channels & (1u << (channel - 1)));
}

void ax200_radio_detail(const char *detail)
{
  set_detail(detail);
}

void wifi_interrupt(void)
{
  if (boot_options_get()->wifi_rtl8922ae) {
    rtl8922ae_interrupt();
  } else {
    ax200_transport_interrupt();
  }
}
