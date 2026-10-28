/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2007-2009 Damien Bergamini
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Native bounded station path adapted from pinned iwx/net80211 and the
 * BSD-3-Clause alternative of Linux v6.12 iwlwifi RF-policy references.
 * Intel reference notices are retained in kernel/wifi/NOTICE. */
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/acpi/wifi.h>
#include <kernel/memory.h>
#include <kernel/log.h>
#include <kernel/panic.h>
#include <kernel/object/wifi_auth.h>
#include <kernel/task.h>
#include <kernel/wifi/ax200_transport.h>
#include "ax200_firmware.h"
#include "ax200_station.h"
#include "ax200_station_wire.h"
#include "ax200_security.h"
#include "wifi_rsn.h"

#define AX_LINK_QUEUE 32u
#define AX_LINK_STEP_MS 20u
#define AX_LINK_DRAIN_MS 2000u
#define AX_LINK_BEACON_LOSS_NS UINT64_C(3000000000)
#define AX_LINK_TX_STALL_NS UINT64_C(3000000000)
#define AX_LINK_RESPONSE_MS 1000u
#define AX_LINK_EXCHANGES 3u
#define AX_RX_SEQUENCE_CACHE 64u
#define AX_LINK_RECOVERY_MIN_MS 1000u
#define AX_LINK_RECOVERY_MAX_MS 30000u
#define AX_LINK_HANDSHAKE_MS 30000u
#define AX_LINK_AUTH_REPLACEMENT_MS (2u * AX_LINK_RECOVERY_MAX_MS)
#define AX_EAPOL_TYPE UINT16_C(0x888e)
#define AX_RSN_IE_BYTES 22u
#define AX_MANAGEMENT_BYTES 128u

struct ax_ethernet_frame { uint16_t bytes; uint8_t data[WIFI_ETHERNET_BYTES]; };
struct ax_rx_sequence_cache {
  uint16_t sequence[AX_RX_SEQUENCE_CACHE];
  unsigned count, next;
};
static struct {
  struct wifi_network_snapshot snapshot;
  uint64_t interface_id;
  struct wifi_network_statistics statistics;
  unsigned tx_read, tx_count, rx_read, rx_count;
  struct ax_ethernet_frame tx[AX_LINK_QUEUE], rx[AX_LINK_QUEUE];
} network;

enum station_phase {
  STATION_IDLE, STATION_OBSERVE, STATION_AUTH, STATION_ASSOC, STATION_HANDSHAKE, STATION_DATA, STATION_DRAIN,
};
static struct {
  struct wifi_connect_work *work;
  const char *failure;
  enum station_phase phase;
  bool observed, rejected, response, failed, phy, mac, binding, peer, queue, session, session_ended;
  bool mac_update_pending;
  bool secure, associated, authenticated, port_authorized, auth_started, setup_finished;
  bool ordinary_loss, security_failure;
  uint64_t association_generation, attempt_deadline, rekey_deadline;
  enum call_status auth_status;
  struct ax200_security security;
  uint8_t ap_rsn[WIFI_AUTH_RSN_BYTES], association_rsn[AX_RSN_IE_BYTES];
  size_t ap_rsn_bytes;
  uint8_t address[6], rates[12], rate_count, dtim_count, dtim_period, erp;
  uint16_t legacy_rates, basic_rates, capabilities, beacon_interval, aid, sequence;
  struct ax_rx_sequence_cache rx_sequence[2];
  uint32_t beacon_arrival;
  uint64_t beacon_tsf, last_beacon, last_tx_progress;
  uint64_t last_tx_completed;
  uint8_t received_frame[AX_STATION_FRAME_BYTES]; /* Sole radio worker RX scratch. */
} station;
static uint64_t next_association_generation;

static uint16_t get16(const uint8_t *p)
{
  return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static uint32_t get32(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put16(uint8_t *p, uint16_t value)
{
  p[0] = value;
  p[1] = value >> 8;
}

static void require_shared(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
}

static bool next_incarnation(void)
{
  require_shared();
  if (network.snapshot.generation == UINT64_MAX) {
    network.snapshot.ready = false;
    network.snapshot.prepared = false;
    return false;
  }
  ++network.snapshot.generation;
  return true;
}

void wifi_driver_network_statistics(struct wifi_network_statistics *statistics)
{
  require_shared();
  *statistics = network.statistics;
  statistics->tx_pending = ax200_transport_tx_pending() + network.tx_count;
  statistics->tx_completed = ax200_transport_tx_completed();
  statistics->tx_failed = ax200_transport_tx_failures();
  statistics->tx_high_water = ax200_transport_tx_high_water();
  statistics->frame_bytes = WIFI_ETHERNET_BYTES;
  statistics->ring_entries = 256;
}

void wifi_driver_network_snapshot(struct wifi_network_snapshot *snapshot)
{
  require_shared();
  struct wifi_status_reply status;
  ax200_transport_status(&status);
  *snapshot = network.snapshot;
  snapshot->present = !!(status.flags & WIFI_PRESENT);
  snapshot->prepared = !!(status.flags & WIFI_PREPARED) && !(status.flags & WIFI_FAILED) &&
      network.snapshot.generation != UINT64_MAX;
  snapshot->controller_id = status.controller_id;
  snapshot->mac_valid = ax200_transport_address(snapshot->mac);
  snapshot->ready = snapshot->ready && snapshot->prepared && !(status.flags & WIFI_RFKILL) &&
      station.work && !atomic_load_explicit(&station.work->cancel, memory_order_acquire);
}

bool wifi_driver_network_bind(uint64_t interface_id)
{
  require_shared();
  if (!interface_id || (network.interface_id && network.interface_id != interface_id)) {
    return false;
  }
  network.interface_id = interface_id;
  ax200_transport_notify();
  return true;
}

uint64_t ax200_station_interface_id(void)
{
  require_shared();
  return network.snapshot.generation == UINT64_MAX ? 0 : network.interface_id;
}

enum net_result wifi_driver_transmit(uint64_t generation, const void *frame, size_t bytes)
{
  require_shared();
  if (!network.snapshot.ready || generation != network.snapshot.generation || !station.work ||
      atomic_load_explicit(&station.work->cancel, memory_order_acquire) ||
      !ax200_radio_tx_allowed(station.work->target.channel)) {
    return NET_UNAVAILABLE;
  }
  /* Keep successful rekey a bounded pause, not a carrier/L3 failure. */
  if (!station.port_authorized) {
    return NET_QUEUE_FULL;
  }
  if (!frame || bytes < 14 || bytes > WIFI_ETHERNET_BYTES ||
      memcmp((const uint8_t *)frame + 6, network.snapshot.mac, 6) ||
      (((const uint8_t *)frame)[12] << 8 | ((const uint8_t *)frame)[13]) < 1536) {
    return NET_INVALID;
  }
  if (network.tx_count == AX_LINK_QUEUE) {
    return NET_QUEUE_FULL;
  }
  struct ax_ethernet_frame *owned = &network.tx[(network.tx_read + network.tx_count) % AX_LINK_QUEUE];
  owned->bytes = bytes;
  memcpy(owned->data, frame, bytes);
  ++network.tx_count;
  ++network.statistics.tx_queued;
  ax200_transport_notify();
  return NET_OK;
}

bool wifi_driver_receive(uint64_t generation, void *frame, size_t capacity, size_t *bytes)
{
  require_shared();
  *bytes = 0;
  if (!network.snapshot.ready || !station.port_authorized || generation != network.snapshot.generation || !network.rx_count) {
    return false;
  }
  struct ax_ethernet_frame *owned = &network.rx[network.rx_read];
  if (!frame || capacity < owned->bytes) {
    return false;
  }
  *bytes = owned->bytes;
  memcpy(frame, owned->data, owned->bytes);
  network.rx_read = (network.rx_read + 1) % AX_LINK_QUEUE;
  --network.rx_count;
  return true;
}

static enum call_status stop_reason(void)
{
  uint64_t flags = cpu_save_interrupts();
  enum call_status result = wifi_connect_stop_reason(station.work);
  if (result == CALL_OK && station.secure) {
    result = wifi_auth_stop_reason(station.work->auth, station.auth_started ? station.association_generation : 0);
  }
  cpu_restore_interrupts(flags);
  return result;
}

static bool fixed(uint8_t group, uint8_t opcode, const void *data, size_t bytes, uint64_t deadline)
{
  return ax200_radio_command(group, opcode, data, bytes, NULL, 0, NULL, deadline);
}

static bool command_status(uint8_t opcode, const void *data, size_t bytes,
    uint32_t expected, uint64_t deadline)
{
  uint32_t reply;
  size_t received;
  if (!ax200_radio_command(AX200_LONG, opcode, data, bytes, &reply, sizeof(reply), &received,
      deadline) || received != sizeof(reply) || reply != expected) {
    ax200_radio_quarantine("station command status uncertain; DMA retained until reboot");
    return false;
  }
  return true;
}

static bool policy_state_known(enum acpi_wifi_state state)
{
  return state == ACPI_WIFI_ABSENT || state == ACPI_WIFI_DISABLED || state == ACPI_WIFI_VALID;
}

static const char *platform_policy(uint64_t deadline)
{
  struct acpi_wifi_policy policy;
  uint64_t flags = cpu_save_interrupts();
  enum acpi_wifi_state captured = acpi_wifi_snapshot(&policy);
  cpu_restore_interrupts(flags);
  struct pci_address address;
  if (captured != ACPI_WIFI_VALID) {
    switch (captured) {
      case ACPI_WIFI_PENDING: return "ACPI policy capture pending; terminal drained";
      case ACPI_WIFI_ABSENT: return "ACPI PCI companion absent; terminal drained";
      case ACPI_WIFI_DISABLED: return "ACPI PCI companion disabled; terminal drained";
      case ACPI_WIFI_UNSUPPORTED: return "ACPI companion or locked policy unsupported; terminal drained";
      case ACPI_WIFI_MALFORMED: return "ACPI companion identity malformed; terminal drained";
      default: return "ACPI PCI companion discovery failed; terminal drained";
    }
  }
  if (!ax200_transport_pci_address(&address) ||
      memcmp(&address, &policy.address, sizeof(address))) {
    return "ACPI PCI companion identity mismatch; terminal drained";
  }
  if (!policy_state_known(policy.glai.state) ||
      (policy.glai.state == ACPI_WIFI_VALID && policy.glai.value)) {
    return "ACPI GLAI policy locked or unavailable; terminal drained";
  }
  if (!policy_state_known(policy.sar.state)) {
    return "ACPI WRDS SAR policy unavailable; terminal drained";
  }
  if (!policy_state_known(policy.ewrd.state)) {
    return "ACPI EWRD SAR policy unavailable; terminal drained";
  }
  if (!policy_state_known(policy.geo.state)) {
    return "ACPI WGDS geographic policy unavailable; terminal drained";
  }
  if (!policy_state_known(policy.ppag.state)) {
    return "ACPI PPAG policy unavailable; terminal drained";
  }
  if (!policy_state_known(policy.dsm.state)) {
    return "ACPI DSM policy unavailable; terminal drained";
  }
  /* PPAG applicability requires a known approved platform identity. The native
   * snapshot currently supplies no SMBIOS proof; enabled PPAG cannot be guessed. */
  if (policy.ppag.state == ACPI_WIFI_VALID && policy.ppag.flags) {
    return "ACPI enabled PPAG applicability unproved; terminal drained";
  }
  uint32_t lari[6] = {0};
  if (policy.dsm.state == ACPI_WIFI_VALID) {
    static const uint8_t functions[] = {1, 2, 3, 4, 6, 7, 8, 9};
    for (size_t f = 0; f < sizeof(functions); ++f) {
      unsigned i = functions[f];
      if ((policy.dsm.supported & (1u << i)) && !policy_state_known(policy.dsm.function[i].state)) {
        return "ACPI supported DSM function unavailable; terminal drained";
      }
    }
    if (policy.dsm.function[1].state == ACPI_WIFI_VALID) {
      unsigned value = policy.dsm.function[1].value;
      if (value > 2) {
        return "ACPI DSM regulatory value unsupported; terminal drained";
      }
      lari[0] |= value == 1 ? 2 : value == 2 ? 4 : 0;
    }
    lari[2] = policy.dsm.function[6].state == ACPI_WIFI_VALID ? policy.dsm.function[6].value : 0;
    lari[3] = policy.dsm.function[7].state == ACPI_WIFI_VALID ? policy.dsm.function[7].value & 0xf : 0;
    lari[4] = policy.dsm.function[8].state == ACPI_WIFI_VALID ? policy.dsm.function[8].value & ~UINT32_C(16) : 0;
    lari[1] = policy.dsm.function[3].state == ACPI_WIFI_VALID ? policy.dsm.function[3].value : 0;
    lari[5] = policy.dsm.function[9].state == ACPI_WIFI_VALID ? policy.dsm.function[9].value : 0;
    bool changed = false;
    for (unsigned i = 0; i < 6; ++i) {
      changed |= lari[i] != 0;
    }
    if (changed && !fixed(AX200_NVM, AX_STATION_LARI, lari, sizeof(lari), deadline)) {
      return "firmware LARI policy command failed; terminal drained";
    }
  }
  if (policy.sar.state != ACPI_WIFI_VALID) {
    return policy.geo.state == ACPI_WIFI_VALID ?
        "WGDS geographic policy lacks basic WRDS SAR; terminal drained" : NULL;
  }
  if (policy.sar.chains < 2 || policy.sar.bands < 5 || policy.sar.revision > 2 ||
      policy.sar.flags >> 1 || !ax200_firmware_api(AX_STATION_API_TX_POWER)) {
    return "WRDS SAR format or firmware API unsupported; terminal drained";
  }
  uint8_t power[44] = {0};
  power[0] = 2; /* SET_CHAINS; preserve the raw eighth-dBm platform limits. */
  for (unsigned chain = 0; chain < 2; ++chain) {
    for (unsigned band = 0; band < 5; ++band) {
      put16(power + 16 + (chain * 5 + band) * 2, policy.sar.limits[chain][band]);
    }
  }
  if (!fixed(AX200_LONG, AX_STATION_TX_POWER, power, sizeof(power), deadline)) {
    return "firmware SAR power command failed; terminal drained";
  }
  if (policy.geo.state == ACPI_WIFI_VALID) {
    if (policy.geo.profiles > 8 || policy.geo.profiles < 3 || policy.geo.bands < 2 ||
        policy.geo.revision > 3) {
      return "WGDS geographic format unsupported; terminal drained";
    }
    uint8_t geo[72] = {0};
    for (unsigned profile = 0; profile < policy.geo.profiles; ++profile) {
      unsigned source = profile;
      for (unsigned band = 0; band < 2; ++band) {
        const struct acpi_wifi_geo_band *entry = &policy.geo.table[source][band];
        uint8_t *out = geo + 4 + (profile * 2 + band) * 4;
        put16(out, entry->max);
        out[2] = entry->offset_a;
        out[3] = entry->offset_b;
      }
    }
    geo[68] = policy.geo.revision == 1;
    if (!fixed(AX200_PHY, AX_STATION_GEO_POWER, geo, sizeof(geo), deadline)) {
      return "firmware geographic power command failed; terminal drained";
    }
  }
  return NULL;
}

static const uint8_t legacy_rates[12] = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};
struct station_elements {
  const uint8_t *ssid;
  const uint8_t *rsn;
  size_t rsn_bytes;
  const char *refusal;
  size_t offset;
  unsigned element_id, element_bytes, value;
  uint8_t ssid_bytes, dtim_count, dtim_period, erp;
  uint16_t rates, basic;
  bool ssid_seen, rates_seen, extended_seen, tim_seen, erp_seen;
};

static bool rsn_supported(const uint8_t *ie, size_t bytes)
{
  uint32_t flags = wifi_rsn_security(ie, bytes);
  return (flags & WIFI_BSS_RSN_PSK_CCMP) == WIFI_BSS_RSN_PSK_CCMP &&
      !(flags & (WIFI_BSS_RSN_MFPR | WIFI_BSS_RSN_SPP_REQUIRED));
}

static bool add_rates(struct station_elements *elements, const uint8_t *rates, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    unsigned index = 0;
    while (index < 12 && legacy_rates[index] != (rates[i] & 0x7f)) {
      ++index;
    }
    if (index == 12) {
      if (rates[i] & 0x80) {
        elements->refusal = "unsupported-basic-rate";
        elements->value = rates[i];
        return false;
      }
      continue;
    }
    if (elements->rates & (1u << index)) {
      elements->refusal = "duplicate-rate";
      elements->value = rates[i];
      return false;
    }
    elements->rates |= 1u << index;
    if (rates[i] & 0x80) {
      elements->basic |= 1u << index;
    }
  }
  return true;
}

static bool parse_elements(const uint8_t *data, size_t bytes, struct station_elements *elements)
{
  memset(elements, 0, sizeof(*elements));
  for (size_t offset = 0; offset < bytes;) {
    elements->offset = offset;
    elements->element_id = 256;
    elements->element_bytes = 0;
    elements->value = 0;
    elements->refusal = "element-header";
    if (bytes - offset < 2) {
      return false;
    }
    uint8_t id = data[offset], length = data[offset + 1];
    elements->element_id = id;
    elements->element_bytes = length;
    elements->refusal = "element-extent";
    offset += 2;
    if (length > bytes - offset) {
      return false;
    }
    const uint8_t *value = data + offset;
    switch (id) {
    case AX_STATION_IE_SSID:
      elements->refusal = elements->ssid_seen ? "duplicate-ssid" : "ssid-length";
      if (elements->ssid_seen || length > 32) {
        return false;
      }
      elements->ssid_seen = true;
      elements->ssid = value;
      elements->ssid_bytes = length;
      break;
    case AX_STATION_IE_RATES:
      elements->refusal = elements->rates_seen ? "duplicate-rates-element" : "rates-length";
      if (elements->rates_seen || !length || length > 8 || !add_rates(elements, value, length)) {
        return false;
      }
      elements->rates_seen = true;
      break;
    case AX_STATION_IE_EXTENDED_RATES:
      elements->refusal = elements->extended_seen ? "duplicate-extended-rates" : "extended-rates-length";
      if (elements->extended_seen || !length || !add_rates(elements, value, length)) {
        return false;
      }
      elements->extended_seen = true;
      break;
    case AX_STATION_IE_CHANNEL:
      elements->refusal = length != 1 ? "channel-length" : "channel-mismatch";
      elements->value = length ? value[0] : 0;
      if (length != 1 || value[0] != station.work->target.channel) {
        return false;
      }
      break;
    case AX_STATION_IE_TIM:
      elements->refusal = elements->tim_seen ? "duplicate-tim" : length < 4 ? "tim-length" :
          !value[1] ? "zero-dtim-period" : "dtim-count-outside-period";
      elements->value = length >= 2 ? (unsigned)value[0] << 8 | value[1] : 0;
      if (elements->tim_seen || length < 4 || !value[1] || value[0] >= value[1]) {
        return false;
      }
      elements->tim_seen = true;
      elements->dtim_count = value[0];
      elements->dtim_period = value[1];
      break;
    case AX_STATION_IE_RSN:
      elements->refusal = "unsupported-rsn";
      if (!station.secure || elements->rsn || !rsn_supported(value - 2, (size_t)length + 2)) {
        return false;
      }
      elements->rsn = value - 2;
      elements->rsn_bytes = (size_t)length + 2;
      break;
    case AX_STATION_IE_RSNXE:
      /* SAE-only capabilities do not apply to the selected WPA2-PSK AKM. */
      if (station.secure && length) {
        break;
      }
      return false;
    case AX_STATION_IE_CHANNEL_SWITCH: /* A channel switch is outside the one-channel profile. */
      elements->refusal = "security-or-channel-switch-element";
      return false;
    case AX_STATION_IE_ERP:
      elements->refusal = elements->erp_seen ? "duplicate-erp" :
          length != 1 ? "erp-length" : "erp-reserved-flags";
      elements->value = length ? value[0] : 0;
      if (elements->erp_seen || length != 1 || (value[0] & ~7u)) {
        return false;
      }
      elements->erp_seen = true;
      elements->erp = value[0];
      break;
    case AX_STATION_IE_HT_OPERATION:
      elements->refusal = length != 22 ? "ht-operation-length" :
          value[0] != station.work->target.channel ? "ht-channel-mismatch" : "ht-secondary-reserved";
      elements->value = length >= 2 ? (unsigned)value[0] << 8 | value[1] : 0;
      /* AP HT20/40 operation does not negotiate HT for our legacy20 station. */
      if (length != 22 || value[0] != station.work->target.channel ||
          (value[1] & AX_STATION_HT_SECONDARY_MASK) == AX_STATION_HT_SECONDARY_RESERVED) {
        return false;
      }
      break;
    case AX_STATION_IE_VENDOR:
      elements->refusal = "vendor-security-element";
      if ((length >= 4 && value[0] == 0x00 && value[1] == 0x50 && value[2] == 0xf2 && value[3] == 1) ||
          (length >= 4 && value[0] == 0x50 && value[1] == 0x6f && value[2] == 0x9a && value[3] == 0x1c)) {
        return false;
      }
      break;
    default:
      break;
    }
    offset += length;
  }
  elements->refusal = !elements->rates_seen ? "missing-supported-rates" :
      !elements->rates ? "no-supported-legacy-rate" : !elements->basic ? "no-basic-legacy-rate" : NULL;
  elements->element_id = 256;
  elements->element_bytes = 0;
  elements->offset = bytes;
  elements->value = 0;
  return elements->rates_seen && elements->rates && elements->basic;
}

static void close_admission(const char *reason)
{
  if (!station.failure) {
    station.failure = reason;
  }
  station.failed = true;
  uint64_t flags = cpu_save_interrupts();
  station.port_authorized = false;
  network.snapshot.ready = false;
  net_worker_notify();
  cpu_restore_interrupts(flags);
}

static void observe_beacon(const uint8_t *frame, size_t bytes, uint32_t arrival)
{
  if (bytes < 36) {
    return;
  }
  if (memcmp(frame + 16, station.work->target.bssid, 6)) {
    return;
  }
  struct station_elements elements;
  bool valid = parse_elements(frame + 36, bytes - 36, &elements);
  uint16_t capabilities = get16(frame + 34), interval = get16(frame + 32);
  if (!valid || !elements.ssid_seen || !elements.tim_seen ||
      elements.ssid_bytes != station.work->target.ssid_bytes ||
      memcmp(elements.ssid, station.work->target.ssid, elements.ssid_bytes) ||
      memcmp(frame + 10, station.work->target.bssid, 6) ||
      !(capabilities & AX_STATION_CAP_ESS) ||
      (capabilities & 2) || !!(capabilities & AX_STATION_CAP_PRIVACY) != station.secure ||
      (station.secure && !elements.rsn) || !interval || interval > 1000) {
    if (!station.rejected) {
      bool ssid_matches = elements.ssid_seen &&
          elements.ssid_bytes == station.work->target.ssid_bytes &&
          !memcmp(elements.ssid, station.work->target.ssid, elements.ssid_bytes);
      bool transmitter_matches = !memcmp(frame + 10, station.work->target.bssid, 6);
      const char *predicate = !valid ? elements.refusal :
          !elements.ssid_seen ? "missing-ssid" : !elements.tim_seen ? "missing-tim" :
          !ssid_matches ? "ssid-mismatch" : !transmitter_matches ? "transmitter-mismatch" :
          !(capabilities & AX_STATION_CAP_ESS) ? "not-ess" :
          capabilities & (AX_STATION_CAP_PRIVACY | 2) ? "privacy-or-ibss" : "beacon-interval";
      kdebug("wifi: beacon reject predicate=%s bytes=%zu cap=%u interval=%u SSID=%u TIM=%u identity=%u/%u rates=%u basic=%u\n",
          predicate, bytes, capabilities, interval, elements.ssid_seen, elements.tim_seen,
          ssid_matches, transmitter_matches, elements.rates, elements.basic);
      if (!valid) {
        kdebug("wifi: beacon element id=%u length=%u offset=%zu value=%u\n",
            elements.element_id, elements.element_bytes, elements.offset, elements.value);
      }
    }
    station.rejected = true;
    close_admission("invalid target beacon profile");
    return;
  }
  if (station.observed && (station.legacy_rates != elements.rates ||
      station.basic_rates != elements.basic || station.beacon_interval != interval ||
      (station.secure && (station.ap_rsn_bytes != elements.rsn_bytes ||
      memcmp(station.ap_rsn, elements.rsn, station.ap_rsn_bytes))))) {
    station.rejected = true;
    close_admission("target beacon profile changed");
    return;
  }
  if (station.mac && (station.erp != elements.erp ||
      ((station.capabilities ^ capabilities) &
      (AX_STATION_CAP_SHORT_PREAMBLE | AX_STATION_CAP_SHORT_SLOT)))) {
    /* Firmware commands run outside the RX loan, with the latest beacon state. */
    station.mac_update_pending = true;
  }
  station.observed = true;
  if (station.secure) {
    station.ap_rsn_bytes = elements.rsn_bytes;
    memcpy(station.ap_rsn, elements.rsn, elements.rsn_bytes);
  }
  station.capabilities = capabilities;
  station.beacon_interval = interval;
  station.dtim_count = elements.dtim_count;
  station.dtim_period = elements.dtim_period;
  station.erp = elements.erp;
  station.legacy_rates = elements.rates;
  station.basic_rates = elements.basic;
  station.rate_count = 0;
  for (unsigned i = 0; i < 12; ++i) {
    if (elements.rates & (1u << i)) {
      station.rates[station.rate_count++] = legacy_rates[i] | ((elements.basic & (1u << i)) ? 0x80 : 0);
    }
  }
  memcpy(&station.beacon_tsf, frame + 24, sizeof(station.beacon_tsf));
  station.beacon_arrival = arrival;
  station.last_beacon = arch_monotonic_ns();
}

static bool received_retry(uint16_t sequence, bool group, bool retry)
{
  if (!retry) {
    return false;
  }
  struct ax_rx_sequence_cache *cache = &station.rx_sequence[group];
  for (unsigned i = 0; i < cache->count; ++i) {
    if (cache->sequence[i] == sequence) {
      return true;
    }
  }
  return false;
}

static void remember_sequence(uint16_t sequence, bool group)
{
  struct ax_rx_sequence_cache *cache = &station.rx_sequence[group];
  cache->sequence[cache->next] = sequence;
  cache->next = (cache->next + 1) % AX_RX_SEQUENCE_CACHE;
  if (cache->count < AX_RX_SEQUENCE_CACHE) {
    ++cache->count;
  }
}

static void receive_drop(void)
{
  uint64_t flags = cpu_save_interrupts();
  ++network.statistics.rx_dropped;
  cpu_restore_interrupts(flags);
}

static void receive_data(const uint8_t *frame, size_t bytes, uint32_t status,
    bool padded, uint8_t mac_flags1)
{
  static const uint8_t snap[6] = {0xaa, 0xaa, 3, 0, 0, 0};
  if (station.phase < STATION_ASSOC || station.phase >= STATION_DRAIN ||
      frame[0] != AX_STATION_DATA || (frame[1] & 3) != 2 || (frame[1] & AX_STATION_MORE_FRAG) ||
      (get16(frame + 22) & 15) || bytes < 32 ||
      memcmp(frame + 10, station.work->target.bssid, 6) || (frame[16] & 1) ||
      (!(frame[4] & 1) && memcmp(frame + 4, station.address, 6))) {
    return;
  }
  uint16_t sequence = get16(frame + 22) >> 4;
  bool group = frame[4] & 1, encrypted = frame[1] & AX200_FRAME_PROTECTED;
  bool deduplicate = encrypted || !station.secure;
  if (deduplicate && received_retry(sequence, group, frame[1] & AX_STATION_RETRY)) {
    return;
  }
  size_t offset = 24 + (padded ? 2 : 0), length = bytes - offset;
  struct ax200_security_rx protected = {0};
  if (encrypted) {
    if (!station.secure) {
      return;
    }
    enum ax200_security_rx_result checked = ax200_security_receive(&station.security,
        frame, bytes, status, padded, mac_flags1, &protected);
    if (checked != AX200_SECURITY_RX_OK) {
      /* Failed protected-frame checks are counted drops, not a reason to
       * retire an otherwise usable key or connection. */
      receive_drop();
      return;
    }
    offset = protected.payload_offset;
    length = protected.payload_bytes;
  } else if (status & AX200_RX_SECURITY_MASK) {
    return;
  }
  if (length < 8 || length > WIFI_ETHERNET_BYTES - 6 || memcmp(frame + offset, snap, sizeof(snap))) {
    return;
  }
  const uint8_t *body = frame + offset;
  unsigned ether_type = (unsigned)body[6] << 8 | body[7];
  if (ether_type < 1536 || (station.secure && !encrypted && ether_type != AX_EAPOL_TYPE)) {
    return;
  }
  if (encrypted && !ax200_security_accept(&station.security, &protected)) {
    receive_drop();
    return;
  }
  /* A checked frame remains consumed even if the downstream queue is full or
   * the controlled port is paused. An AP retry must not be accepted twice. */
  if (deduplicate) {
    remember_sequence(sequence, group);
  }
  if (ether_type == AX_EAPOL_TYPE && station.secure) {
    if (!station.auth_started || group || memcmp(frame + 16, station.work->target.bssid, 6) || length < 12) {
      return;
    }
    size_t eapol_bytes = 4u + ((unsigned)body[10] << 8 | body[11]);
    if (eapol_bytes > length - 8 || eapol_bytes > WIFI_AUTH_EAPOL_BYTES) {
      return;
    }
    uint64_t flags = cpu_save_interrupts();
    enum call_status result = wifi_auth_receive(station.work->auth, station.association_generation,
        body + 8, eapol_bytes, encrypted ? WIFI_AUTH_EVENT_ENCRYPTED : 0);
    cpu_restore_interrupts(flags);
    if (result == CALL_WOULD_BLOCK) {
      receive_drop();
    } else if (result != CALL_OK) {
      station.auth_status = result;
      close_admission("authentication event delivery failed");
    }
    return;
  }
  uint64_t flags = cpu_save_interrupts();
  if (network.snapshot.ready && station.port_authorized && network.rx_count < AX_LINK_QUEUE) {
    struct ax_ethernet_frame *owned = &network.rx[(network.rx_read + network.rx_count) % AX_LINK_QUEUE];
    owned->bytes = length + 6;
    memcpy(owned->data, frame + 4, 6);
    memcpy(owned->data + 6, frame + 16, 6);
    memcpy(owned->data + 12, body + 6, length - 6);
    ++network.rx_count;
    ++network.statistics.rx_queued;
    if (network.rx_count > network.statistics.rx_high_water) {
      network.statistics.rx_high_water = network.rx_count;
    }
    net_worker_notify();
  } else {
    ++network.statistics.rx_dropped;
  }
  cpu_restore_interrupts(flags);
}

/* Open System authentication may carry trailing IEs. Check their extents
 * without interpreting unrelated extensions or reparsing the fixed fields. */
static bool authentication_tail_valid(const uint8_t *tail, size_t bytes)
{
  size_t offset = 0;
  while (offset < bytes) {
    if (bytes - offset < 2 || tail[offset + 1] > bytes - offset - 2) {
      return false;
    }
    offset += 2 + tail[offset + 1];
  }
  return true;
}

void ax200_station_notify(uint8_t group, uint8_t opcode, const uint8_t *payload, size_t bytes)
{
  if (!station.work || station.phase == STATION_IDLE || station.phase == STATION_DRAIN) {
    return;
  }
  if (group == AX200_PHY && opcode == AX200_THERMAL_KILL) {
    close_admission("firmware thermal notification");
    return;
  }
  if (group == AX_STATION_MAC_GROUP && opcode == 0xfb) {
    if (bytes != 16 || get32(payload) || get32(payload + 12) != 0 ||
        get32(payload + 4) > 1 || get32(payload + 8) > 1) {
      close_admission("invalid session-protection notification");
    } else if (!get32(payload + 4)) {
      close_admission("session protection refused");
      station.session = false;
      station.session_ended = true;
    } else if (!get32(payload + 8)) {
      station.session = false;
      station.session_ended = true;
    }
    return;
  }
  if ((group != AX200_LONG && group != AX200_LEGACY) || opcode != AX200_RX_MPDU ||
      bytes < AX200_RX_DESCRIPTOR_BYTES) {
    return;
  }
  uint32_t status = get32(payload + 12);
  size_t length = get16(payload);
  if (length > bytes - AX200_RX_DESCRIPTOR_BYTES || length < 24 ||
      (status & (AX200_RX_CRC_OK | AX200_RX_OVERRUN_OK)) !=
      (AX200_RX_CRC_OK | AX200_RX_OVERRUN_OK) ||
      (status & AX200_RX_RFKILL) ||
      payload[34] != station.work->target.channel || (payload[3] & AX200_RX_AMSDU) ||
      (get16(payload + 5) & AX_STATION_RX_AMPDU)) {
    return;
  }
  const uint8_t *borrowed = payload + AX200_RX_DESCRIPTOR_BYTES;
  if ((borrowed[0] & 3) || (borrowed[1] & AX_STATION_MORE_FRAG) ||
      (get16(borrowed + 22) & 15)) {
    return;
  }
  /* Copy only while the RX loan is live. Management padding follows the MAC
   * header; protected data padding follows its CCMP IV (pinned iwx). */
  uint8_t *frame = station.received_frame;
  bool padded = payload[3] & AX200_RX_PAD;
  if ((borrowed[0] & 0x0c) == AX_STATION_DATA) {
    if (length <= sizeof(station.received_frame)) {
      memcpy(frame, borrowed, length);
      receive_data(frame, length, status, padded, payload[2]);
      memzero_explicit(frame, sizeof(station.received_frame));
    }
    return;
  }
  if (status & AX200_RX_SECURITY_MASK) {
    return;
  }
  if (length > sizeof(station.received_frame) + (padded ? 2 : 0) || (padded && length < 26)) {
    return;
  }
  memcpy(frame, borrowed, 24);
  memcpy(frame + 24, borrowed + 24 + (padded ? 2 : 0), length - 24 - (padded ? 2 : 0));
  length -= padded ? 2 : 0;
  if (frame[0] == AX200_FRAME_BEACON && !(frame[1] & (3 | AX200_FRAME_PROTECTED))) {
    observe_beacon(frame, length, get32(payload + 36));
    return;
  }
  static const uint8_t broadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  bool disconnect = frame[0] == AX_STATION_DEAUTH || frame[0] == AX_STATION_DISASSOC;
  bool destination = !memcmp(frame + 4, station.address, 6) ||
      (disconnect && !memcmp(frame + 4, broadcast, sizeof(broadcast)));
  if (!destination ||
      memcmp(frame + 10, station.work->target.bssid, 6) ||
      memcmp(frame + 16, station.work->target.bssid, 6) ||
      (frame[1] & (3 | AX200_FRAME_PROTECTED))) {
    return;
  }
  if (frame[0] == AX_STATION_DEAUTH || frame[0] == AX_STATION_DISASSOC) {
    if (length >= 26) {
      station.ordinary_loss = true;
      close_admission("AP deauthentication or disassociation");
    }
  } else if (frame[0] == AX_STATION_AUTH && station.phase == STATION_AUTH) {
    if (length >= 30) {
      ktrace("wifi: authentication reply bytes=%zu algorithm=%u sequence=%u status=%u\n",
          length, get16(frame + 24), get16(frame + 26), get16(frame + 28));
    } else {
      ktrace("wifi: authentication reply truncated bytes=%zu\n", length);
    }
    if (length < 30 || get16(frame + 24) || get16(frame + 26) != 2 || get16(frame + 28) ||
        !authentication_tail_valid(frame + 30, length - 30)) {
      close_admission("invalid authentication response");
    } else {
      station.response = true;
    }
  } else if (frame[0] == AX_STATION_ASSOC_RESPONSE && station.phase == STATION_ASSOC) {
    struct station_elements elements;
    uint16_t capabilities = length >= 30 ? get16(frame + 24) : 0;
    uint16_t aid = length >= 30 ? get16(frame + 28) & 0x3fff : 0;
    if (length < 30 || get16(frame + 26) || !(capabilities & AX_STATION_CAP_ESS) ||
        (capabilities & 2) || !!(capabilities & AX_STATION_CAP_PRIVACY) != station.secure || aid < 1 || aid > 2007 ||
        !parse_elements(frame + 30, length - 30, &elements) ||
        (elements.basic & ~station.legacy_rates) ||
        (elements.rates & station.legacy_rates) != station.legacy_rates) {
      close_admission("invalid association response");
    } else {
      station.aid = aid;
      station.response = true;
    }
  }
}

static bool firmware_versions(void)
{
  static const struct { uint8_t group, opcode, command, notification; } versions[] = {
    {1, 0x08, 4, 0}, {5, 0x08, 2, 0}, {1, 0x28, 5, 0}, {1, 0x2b, 2, 1},
    {1, 0x18, 12, 2}, {1, 0x19, 2, 0}, {5, 0x17, 3, 2}, {3, 0x05, 1, 0},
    {1, 0x1c, 9, 7}, {5, 0x0f, 4, 0}, {1, 0x1e, 2, 1}, {1, 0xd0, 1, 0},
    {1, 0xd1, 3, 0}, {1, 0xa9, 1, 0}, {1, 0x9f, 5, 0}, {4, 5, 4, 1},
    {4, 7, 1, 0}, {12, 1, 6, 0},
  };
  for (size_t i = 0; i < sizeof(versions) / sizeof(*versions); ++i) {
    if (ax200_command_version(versions[i].group, versions[i].opcode, false) != versions[i].command ||
        ax200_command_version(versions[i].group, versions[i].opcode, true) != versions[i].notification) {
      return false;
    }
  }
  return true;
}

static bool phy_command(unsigned action, uint64_t deadline)
{
  /* Capability48 changes the embedded channel layout without changing the
   * PHY command version, including on hardware limited to 2.4/5 GHz (iwx). */
  if (ax200_firmware_capability(AX_STATION_CAP_ULTRA_HB_CHANNELS)) {
    struct ax_station_phy_uhb request = {
      .action = action, .band = 1, .channel = station.work->target.channel,
    };
    return fixed(AX200_LONG, AX_STATION_PHY, &request, sizeof(request), deadline);
  }
  struct ax_station_phy request = {.action = action, .band = 1, .channel = station.work->target.channel};
  return fixed(AX200_LONG, AX_STATION_PHY, &request, sizeof(request), deadline);
}

static bool binding_command(unsigned action, uint64_t deadline)
{
  struct ax_station_binding request = {.action = action, .macs = {0, UINT32_MAX, UINT32_MAX}};
  return command_status(AX_STATION_BINDING, &request, sizeof(request), 0, deadline);
}

static bool peer_command(bool update, bool drain, uint64_t deadline)
{
  struct ax_station_peer request = {
    .modify = update,
    .flags = drain ? AX_STATION_DRAIN : 0,
    .flags_mask = AX_STATION_WIDTH_MASK | AX_STATION_MIMO_MASK | AX_STATION_DRAIN,
  };
  if (!update) {
    memcpy(request.address, station.work->target.bssid, 6);
  }
  return command_status(AX_STATION_ADD_PEER, &request, sizeof(request), 1, deadline);
}

static bool mac_command(unsigned action, bool associated, uint64_t deadline)
{
  struct ax_station_mac request = {.action = action};
  ktrace("wifi: MAC context action=%u associated=%u phase=%u PHY=%u binding=%u peer=%u\n",
      action, associated, station.phase, station.phy, station.binding, station.peer);
  /* REMOVE carries only ID/color and action; the remaining payload is zero
   * in the pinned iwx_mac_ctxt_cmd_common REMOVE path. */
  if (action == AX_STATION_REMOVE) {
    return fixed(AX200_LONG, AX_STATION_MAC, &request, sizeof(request), deadline);
  }
  request.type = AX_STATION_BSS_STA;
  memcpy(request.address, station.address, 6);
  memcpy(request.bssid, station.work->target.bssid, 6);
  request.cck = station.basic_rates & 15;
  request.ofdm = station.basic_rates >> 4;
  unsigned lowest_cck = 12, lowest_ofdm = 12;
  for (unsigned i = 0; i < 12; ++i) {
    if (station.basic_rates & (1u << i)) {
      if (i < 4 && lowest_cck == 12) {
        lowest_cck = i;
      } else if (i >= 4 && lowest_ofdm == 12) {
        lowest_ofdm = i;
      }
    }
  }
  request.cck |= lowest_cck < 4 ? (1u << lowest_cck) - 1 : 15;
  request.ofdm |= 1;
  if (lowest_ofdm > 6) {
    request.ofdm |= 1u << 2;
  }
  if (lowest_ofdm > 8) {
    request.ofdm |= 1u << 4;
  }
  request.protection = station.erp & 2 ? AX_STATION_ERP_PROTECT : 0;
  request.preamble = (station.capabilities & AX_STATION_CAP_SHORT_PREAMBLE) && !(station.erp & 4) ?
      AX_STATION_SHORT_PREAMBLE : 0;
  request.slot = station.capabilities & AX_STATION_CAP_SHORT_SLOT ? AX_STATION_SHORT_SLOT : 0;
  request.filter = AX_STATION_FILTER_GROUP | AX_STATION_FILTER_BEACON;
  static const uint8_t fifo[4] = {1, 2, 3, 4};
  for (unsigned i = 0; i < 4; ++i) {
    request.ac[fifo[i]] = (struct ax_station_edca){.minimum = 15, .maximum = 1023, .aifs = 3, .fifos = 1u << fifo[i]};
  }
  request.sta.associated = associated;
  request.sta.beacon_interval = station.beacon_interval;
  request.sta.dtim_interval = station.beacon_interval * station.dtim_period;
  request.sta.listen_interval = 10;
  request.sta.aid = station.aid;
  if (associated) {
    uint32_t offset = station.dtim_count * station.beacon_interval * 1024u;
    request.sta.dtim_time = station.beacon_arrival + offset;
    request.sta.dtim_tsf = station.beacon_tsf + offset;
    request.sta.beacon_arrival = station.beacon_arrival;
  }
  return fixed(AX200_LONG, AX_STATION_MAC, &request, sizeof(request), deadline);
}

static bool refresh_mac(uint64_t deadline)
{
  if (!station.mac_update_pending) {
    return true;
  }
  station.mac_update_pending = false;
  return mac_command(AX_STATION_MODIFY, station.associated, deadline);
}

static bool session_command(unsigned action, uint64_t deadline)
{
  struct ax_station_session request = {.action = action, .duration = action == AX_STATION_ADD ?
      station.beacon_interval * 9u : 0};
  return fixed(AX_STATION_MAC_GROUP, AX_STATION_SESSION, &request, sizeof(request), deadline);
}

static bool smart_fifo(bool associated, uint64_t deadline)
{
  struct { uint32_t state, watermark[2], delay[5][2], full[5][2]; } request = {
    .state = associated ? 1 : 3, .watermark = {4096, associated ? 4096 : 8192},
  };
  for (unsigned i = 0; i < 5; ++i) {
    for (unsigned j = 0; j < 2; ++j) {
      request.delay[i][j] = 1000000;
    }
    request.full[i][0] = associated ? (i == 2 ? 10016 : 2016) : 400;
    request.full[i][1] = associated ? (i == 2 ? 2016 : 320) : 160;
  }
  return fixed(AX200_LONG, AX_STATION_SMART_FIFO, &request, sizeof(request), deadline);
}

static bool configure_station(uint64_t deadline)
{
  if (!phy_command(AX_STATION_ADD, deadline)) {
    return false;
  }
  station.phy = true;
  struct ax_station_rlc rlc = {.rx_chain = ax200_radio_rx_antennas() << 1 | 1u << 10 | 1u << 12};
  if (!fixed(AX_STATION_DATA_GROUP, AX_STATION_RLC, &rlc, sizeof(rlc), deadline) ||
      !mac_command(AX_STATION_ADD, false, deadline)) {
    return false;
  }
  station.mac = true;
  if (!binding_command(AX_STATION_ADD, deadline)) {
    return false;
  }
  station.binding = true;
  if (!peer_command(false, false, deadline)) {
    return false;
  }
  station.peer = true;
  if (!ax200_transport_data_prepare(deadline)) {
    return false;
  }
  station.queue = true;
  if (!session_command(AX_STATION_ADD, deadline)) {
    return false;
  }
  station.session = !station.session_ended;
  return true;
}

static uint32_t management_rate(void)
{
  unsigned chosen = 12;
  for (unsigned i = 0; i < 12; ++i) {
    if ((station.basic_rates & (1u << i)) &&
        (chosen == 12 || legacy_rates[i] < legacy_rates[chosen])) {
      chosen = i;
    }
  }
  KASSERT(chosen < 12);
  uint32_t antenna = ax200_radio_tx_antennas();
  antenna &= (uint32_t)-(int32_t)antenna;
  return antenna << AX_STATION_RATE_ANTENNA_SHIFT |
      (chosen < 4 ? chosen : (chosen - 4) | AX_STATION_RATE_OFDM);
}

static size_t management_frame(uint8_t frame[AX_MANAGEMENT_BYTES], bool association)
{
  memset(frame, 0, AX_MANAGEMENT_BYTES);
  frame[0] = association ? AX_STATION_ASSOC_REQUEST : AX_STATION_AUTH;
  memcpy(frame + 4, station.work->target.bssid, 6);
  memcpy(frame + 10, station.address, 6);
  memcpy(frame + 16, station.work->target.bssid, 6);
  put16(frame + 22, station.sequence << 4);
  station.sequence = (station.sequence + 1) & 0xfff;
  if (!association) {
    put16(frame + 26, 1);
    return 30;
  }
  uint16_t capabilities = AX_STATION_CAP_ESS |
      (station.capabilities & (AX_STATION_CAP_SHORT_PREAMBLE | AX_STATION_CAP_SHORT_SLOT)) |
      (station.secure ? AX_STATION_CAP_PRIVACY : 0);
  put16(frame + 24, capabilities);
  put16(frame + 26, 10);
  frame[28] = 0;
  frame[29] = station.work->target.ssid_bytes;
  memcpy(frame + 30, station.work->target.ssid, station.work->target.ssid_bytes);
  size_t offset = 30 + station.work->target.ssid_bytes;
  unsigned first = station.rate_count < 8 ? station.rate_count : 8;
  frame[offset++] = 1;
  frame[offset++] = first;
  memcpy(frame + offset, station.rates, first);
  offset += first;
  if (station.rate_count > first) {
    frame[offset++] = 50;
    frame[offset++] = station.rate_count - first;
    memcpy(frame + offset, station.rates + first, station.rate_count - first);
    offset += station.rate_count - first;
  }
  if (station.secure) {
    memcpy(frame + offset, station.association_rsn, sizeof(station.association_rsn));
    offset += sizeof(station.association_rsn);
  }
  return offset;
}

static bool exchange(bool association)
{
  station.phase = association ? STATION_ASSOC : STATION_AUTH;
  station.response = false;
  ax200_radio_detail(association ? "open station association in progress" : "Open System authentication in progress");
  for (unsigned attempt = 0; attempt < AX_LINK_EXCHANGES; ++attempt) {
    if (stop_reason() != CALL_OK ||
        ax200_radio_refresh(station.attempt_deadline) != CALL_OK ||
        !refresh_mac(station.attempt_deadline) ||
        !ax200_radio_tx_allowed(station.work->target.channel)) {
      return false;
    }
    uint8_t frame[AX_MANAGEMENT_BYTES];
    size_t bytes = management_frame(frame, association);
    if (!ax200_transport_transmit(frame, bytes, true, management_rate())) {
      return false;
    }
    uint64_t deadline = task_deadline_after_ms(AX_LINK_RESPONSE_MS);
    if (deadline > station.attempt_deadline) {
      deadline = station.attempt_deadline;
    }
    while (!task_deadline_expired(deadline)) {
      uint64_t event = ax200_transport_event();
      if (!ax200_transport_poll() || ax200_radio_refresh(deadline) != CALL_OK ||
          !refresh_mac(deadline) || ax200_radio_faulted() || station.failed || station.rejected ||
          ax200_transport_rfkill() || stop_reason() != CALL_OK ||
          !ax200_radio_tx_allowed(station.work->target.channel)) {
        return false;
      }
      if (station.response) {
        return true;
      }
      ax200_transport_wait(event, deadline);
    }
  }
  return false;
}

static bool data_configuration(uint64_t deadline)
{
  if (!peer_command(true, false, deadline) || !mac_command(AX_STATION_MODIFY, true, deadline) ||
      !smart_fifo(true, deadline)) {
    return false;
  }
  uint8_t multicast[12] = {1, 0, 0, 1};
  memcpy(multicast + 4, station.work->target.bssid, 6);
  uint8_t power[40] = {0};
  put16(power + 6, 25);
  struct ax_station_tlc tlc = {
    .chains = ax200_radio_tx_antennas() & (uint32_t)-(int32_t)ax200_radio_tx_antennas(),
    .legacy_rates = station.legacy_rates, .max_mpdu = 2346,
  };
  return fixed(AX200_LONG, AX_STATION_MULTICAST, multicast, sizeof(multicast), deadline) &&
      fixed(AX200_LONG, AX_STATION_MAC_POWER, power, sizeof(power), deadline) &&
      fixed(AX_STATION_DATA_GROUP, AX_STATION_TLC, &tlc, sizeof(tlc), deadline);
}

static void port_pause(void)
{
  uint64_t flags = cpu_save_interrupts();
  bool was_authorized = station.port_authorized;
  station.port_authorized = false;
  network.statistics.tx_dropped += network.tx_count;
  network.statistics.rx_dropped += network.rx_count;
  network.tx_count = network.rx_count = 0;
  if (station.authenticated && was_authorized) {
    station.rekey_deadline = task_deadline_after_ms(AX_LINK_HANDSHAKE_MS);
  }
  wifi_connection_state(station.work, station.associated, station.authenticated, false);
  cpu_restore_interrupts(flags);
}

static void port_ready(void)
{
  uint64_t flags = cpu_save_interrupts();
  station.port_authorized = true;
  station.authenticated = station.secure;
  station.phase = STATION_DATA;
  station.rekey_deadline = 0;
  network.snapshot.ready = true;
  memcpy(network.snapshot.mac, station.address, 6);
  wifi_connection_state(station.work, true, station.authenticated, true);
  if (!station.setup_finished) {
    wifi_connect_finished(station.work, CALL_OK);
    station.setup_finished = true;
  }
  net_worker_notify();
  cpu_restore_interrupts(flags);
}

static bool begin_auth(void)
{
  static const uint8_t selected[AX_RSN_IE_BYTES] = {
    48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4,
    1, 0, 0, 0x0f, 0xac, 2, 0, 0,
  };
  memcpy(station.association_rsn, selected, sizeof(selected));
  if (next_association_generation == UINT64_MAX) {
    station.auth_status = CALL_LIMIT;
    return false;
  }
  station.association_generation = ++next_association_generation;
  struct wifi_auth_begin begin = {.channel = station.work->target.channel,
    .ssid_bytes = station.work->target.ssid_bytes, .ap_rsn_bytes = station.ap_rsn_bytes,
    .association_rsn_bytes = sizeof(selected)};
  memcpy(begin.local_address, station.address, 6);
  memcpy(begin.bssid, station.work->target.bssid, 6);
  memcpy(begin.ssid, station.work->target.ssid, begin.ssid_bytes);
  memcpy(begin.ap_rsn, station.ap_rsn, station.ap_rsn_bytes);
  memcpy(begin.association_rsn, selected, sizeof(selected));
  uint64_t flags = cpu_save_interrupts();
  station.auth_started = true;
  station.auth_status = wifi_auth_begin(station.work->auth, station.work, station.association_generation,
      station.attempt_deadline, &begin, &station.work->credential);
  cpu_restore_interrupts(flags);
  memzero_explicit(&begin, sizeof(begin));
  if (station.auth_status == CALL_OK) {
    ktrace("wifi: WPA2 association generation=%llu\n", (unsigned long long)station.association_generation);
  }
  return station.auth_status == CALL_OK;
}

static enum call_status send_eapol(const struct wifi_auth_send_request *request)
{
  if (request->length > WIFI_AUTH_EAPOL_BYTES) {
    return CALL_FILE_TOO_LARGE;
  }
  if (!ax200_radio_tx_allowed(station.work->target.channel)) {
    return CALL_DENIED;
  }
  if (ax200_transport_tx_pending() >= AX_STATION_DATA_HIGH_WATER) {
    return CALL_BUSY;
  }
  bool encrypted = request->flags & WIFI_AUTH_EAPOL_ENCRYPT;
  if (encrypted && !station.security.pairwise.installed) {
    return CALL_DENIED;
  }
  uint8_t frame[AX_STATION_MPDU_BYTES] = {AX_STATION_DATA, 1};
  if (encrypted) {
    frame[1] |= AX200_FRAME_PROTECTED;
  }
  memcpy(frame + 4, station.work->target.bssid, 6);
  memcpy(frame + 10, station.address, 6);
  memcpy(frame + 16, station.work->target.bssid, 6);
  put16(frame + 22, station.sequence << 4);
  station.sequence = (station.sequence + 1) & 0xfff;
  static const uint8_t snap[8] = {0xaa, 0xaa, 3, 0, 0, 0, 0x88, 0x8e};
  memcpy(frame + 24, snap, sizeof(snap));
  memcpy(frame + 32, request->data, request->length);
  bool sent = encrypted ? ax200_security_transmit(&station.security, frame, request->length + 32, false, 0) :
      ax200_transport_transmit(frame, request->length + 32, false, 0);
  memzero_explicit(frame, sizeof(frame));
  return sent ? CALL_OK : ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN : CALL_IO;
}

static void process_auth(void)
{
  struct wifi_auth_request *request = wifi_auth_worker_take(station.work, station.association_generation);
  if (!request) {
    return;
  }
  enum call_status result = stop_reason();
  if (result == CALL_OK && (!station.associated || station.failed || station.rejected ||
      request->generation != station.association_generation)) {
    result = CALL_ENDPOINT_CLOSED;
  }
  if (result == CALL_OK && request->operation == WIFI_AUTH_SEND) {
    result = send_eapol(&request->input.send);
  } else if (result == CALL_OK && request->operation == WIFI_AUTH_KEY) {
    const struct wifi_auth_key_request *key = &request->input.key;
    port_pause();
    if (!ax200_transport_data_flush(request->deadline_ns)) {
      result = CALL_OUTCOME_UNKNOWN;
    } else if (key->action == WIFI_AUTH_KEY_INSTALL) {
      result = ax200_security_install(&station.security, key->group, key->key_id,
          key->key, key->receive_pn, request->deadline_ns);
    } else {
      const struct ax200_ccmp_key *installed = key->group ? &station.security.group : &station.security.pairwise;
      result = installed->installed && installed->id != key->key_id ? CALL_BAD_REQUEST :
          ax200_security_remove(&station.security, key->group, request->deadline_ns);
    }
    if (result != CALL_OK) {
      station.auth_status = result;
      station.security_failure = true;
      close_admission("CCMP key operation failed");
    }
  } else if (result == CALL_OK && request->operation == WIFI_AUTH_AUTHORIZE) {
    if (!request->input.authorize.enabled) {
      port_pause();
    } else if (!station.security.pairwise.installed || !station.security.group.installed) {
      result = CALL_DENIED;
    } else {
      port_ready();
      ax200_radio_detail("WPA2-CCMP authentication complete; data port ready");
    }
  } else if (result == CALL_OK && request->operation == WIFI_AUTH_FAIL) {
    station.auth_status = request->input.fail.status;
    station.security_failure = true;
    close_admission("supplicant authentication failed");
  }
  if (result == CALL_OUTCOME_UNKNOWN) {
    station.auth_status = result;
    close_admission("authentication command completion uncertain");
  }
  wifi_auth_worker_complete(request, result);
}

static bool transmit_network(void)
{
  for (unsigned batch = 0; batch < 8; ++batch) {
    struct ax_ethernet_frame owned;
    uint64_t flags = cpu_save_interrupts();
    if (!network.snapshot.ready || !station.port_authorized || !network.tx_count ||
        ax200_transport_tx_pending() >= AX_STATION_DATA_HIGH_WATER) {
      cpu_restore_interrupts(flags);
      return true;
    }
    owned = network.tx[network.tx_read];
    network.tx_read = (network.tx_read + 1) % AX_LINK_QUEUE;
    --network.tx_count;
    cpu_restore_interrupts(flags);
    if (!ax200_radio_tx_allowed(station.work->target.channel) || stop_reason() != CALL_OK) {
      return false;
    }
    uint8_t frame[1532] = {AX_STATION_DATA, 1};
    if (station.secure) {
      frame[1] |= AX200_FRAME_PROTECTED;
    }
    memcpy(frame + 4, station.work->target.bssid, 6);
    memcpy(frame + 10, station.address, 6);
    memcpy(frame + 16, owned.data, 6);
    put16(frame + 22, station.sequence << 4);
    station.sequence = (station.sequence + 1) & 0xfff;
    static const uint8_t snap[6] = {0xaa, 0xaa, 3, 0, 0, 0};
    memcpy(frame + 24, snap, sizeof(snap));
    memcpy(frame + 30, owned.data + 12, owned.bytes - 12);
    bool sent = station.secure ? ax200_security_transmit(&station.security, frame, owned.bytes + 18, false, 0) :
        ax200_transport_transmit(frame, owned.bytes + 18, false, 0);
    memzero_explicit(frame, sizeof(frame));
    memzero_explicit(&owned, sizeof(owned));
    if (!sent) {
      return false;
    }
  }
  return true;
}

static bool retire_station(uint64_t deadline)
{
  station.phase = STATION_DRAIN;
  if (station.session && !session_command(AX_STATION_REMOVE, deadline)) {
    return false;
  }
  if (station.peer && !peer_command(true, true, deadline)) {
    return false;
  }
  if (station.queue && (!ax200_transport_data_flush(deadline) ||
      ax200_security_remove(&station.security, true, deadline) != CALL_OK ||
      ax200_security_remove(&station.security, false, deadline) != CALL_OK ||
      !ax200_transport_data_remove(deadline))) {
    return false;
  }
  if (station.peer) {
    uint32_t peer = 0;
    if (!fixed(AX200_LONG, AX_STATION_REMOVE_PEER, &peer, sizeof(peer), deadline)) {
      return false;
    }
  }
  if (station.binding && !binding_command(AX_STATION_REMOVE, deadline)) {
    return false;
  }
  if (station.mac && !mac_command(AX_STATION_REMOVE, false, deadline)) {
    return false;
  }
  if (station.phy && !phy_command(AX_STATION_REMOVE, deadline)) {
    return false;
  }
  if (station.mac && !smart_fifo(false, deadline)) {
    return false;
  }
  while (!ax200_transport_drained()) {
    if (task_deadline_expired(deadline) || !ax200_transport_poll()) {
      return false;
    }
    kernel_task_yield_if_runnable();
  }
  return true;
}

void ax200_station_observation_begin(void)
{
  KASSERT(station.work && station.phase == STATION_IDLE);
  station.phase = STATION_OBSERVE;
}

static enum call_status prepare_station(void)
{
  ax200_radio_detail("passively confirming the explicit station target");
  if (!ax200_transport_address(station.address)) {
    station.failure = "station address unavailable; terminal drained";
    return CALL_UNAVAILABLE;
  }
  if (!firmware_versions() || (station.secure && !ax200_security_supported())) {
    station.failure = "station firmware command versions unsupported; terminal drained";
    return CALL_UNAVAILABLE;
  }
  if (!ax200_radio_target_scan(station.work, station.attempt_deadline) || !station.observed || station.rejected) {
    station.failure = "passive target profile unconfirmed or refused; terminal drained";
    /* After a published secure link, absence can be an ordinary AP outage. A
     * received but rejected profile is not permission to retry weaker policy. */
    station.ordinary_loss = !station.observed && !station.rejected;
    return ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN : CALL_UNAVAILABLE;
  }
  enum call_status stopped = stop_reason();
  if (stopped != CALL_OK) {
    return stopped;
  }
  ax200_radio_detail("validating platform transmit limits and firmware regulatory profile");
  station.failure = platform_policy(station.attempt_deadline);
  if (station.failure) {
    return CALL_DENIED;
  }
  const char *country_failure;
  if (!ax200_radio_transmit_policy(station.work->target.channel, station.attempt_deadline, &country_failure)) {
    station.failure = country_failure;
    return CALL_DENIED;
  }
  ax200_radio_detail("configuring legacy 20 MHz PHY/MAC/station");
  if (!configure_station(station.attempt_deadline) || (station.secure && !begin_auth()) ||
      !exchange(false) || !exchange(true)) {
    if (!station.failure) {
      station.failure = "station setup/management command refused; terminal drained";
    }
    return ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN :
        station.auth_status != CALL_OK ? station.auth_status : CALL_IO;
  }
  station.associated = true;
  uint64_t flags = cpu_save_interrupts();
  wifi_connection_state(station.work, true, false, false);
  cpu_restore_interrupts(flags);
  if (!data_configuration(station.attempt_deadline)) {
    station.failure = "station data configuration refused; terminal drained";
    return ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN : CALL_IO;
  }
  stopped = stop_reason();
  if (stopped != CALL_OK) {
    return stopped;
  }
  if (!ax200_radio_tx_allowed(station.work->target.channel) || station.failed || station.rejected) {
    return station.secure ? CALL_UNAVAILABLE : CALL_ABANDONED;
  }
  station.last_tx_progress = arch_monotonic_ns();
  if (station.secure) {
    station.phase = STATION_HANDSHAKE;
    ax200_radio_detail("WPA2 handshake in progress; ordinary data port closed");
  } else {
    port_ready();
    ax200_radio_detail("open legacy link L2 ready; MTU 1500; TX ring 256");
  }
  return CALL_OK;
}

static enum call_status run_station(void)
{
  for (;;) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll() || ax200_radio_faulted()) {
      return CALL_OUTCOME_UNKNOWN;
    }
    enum call_status result = stop_reason();
    if (result != CALL_OK) {
      return result;
    }
    result = ax200_radio_refresh(task_deadline_after_ms(AX_LINK_RESPONSE_MS));
    if (result != CALL_OK) {
      if (result != CALL_OUTCOME_UNKNOWN) {
        close_admission("firmware regulatory refresh interrupted the link");
      }
      return result;
    }
    if (!refresh_mac(task_deadline_after_ms(AX_LINK_RESPONSE_MS))) {
      return ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN : CALL_IO;
    }
    if (station.secure && station.auth_started) {
      process_auth();
    }
    result = stop_reason();
    if (result != CALL_OK) {
      return result;
    }
    if (station.auth_status != CALL_OK) {
      return station.auth_status;
    }
    if (station.failed || station.rejected || ax200_transport_rfkill() ||
        !ax200_radio_tx_allowed(station.work->target.channel)) {
      return CALL_UNAVAILABLE;
    }
    uint64_t now = arch_monotonic_ns();
    if ((!station.authenticated && station.secure && now >= station.attempt_deadline) ||
        (station.rekey_deadline && now >= station.rekey_deadline)) {
      close_admission("authentication or rekey deadline expired");
      return CALL_TIMED_OUT;
    }
    if (now - station.last_beacon >= AX_LINK_BEACON_LOSS_NS) {
      station.ordinary_loss = true;
      close_admission("AP beacon deadline expired");
      return CALL_TIMED_OUT;
    }
    unsigned pending = ax200_transport_tx_pending();
    uint64_t completed = ax200_transport_tx_completed();
    if (!pending || completed != station.last_tx_completed) {
      station.last_tx_progress = now;
    } else if (now - station.last_tx_progress >= AX_LINK_TX_STALL_NS) {
      station.ordinary_loss = true;
      close_admission("data transmit progress deadline expired");
      return CALL_TIMED_OUT;
    }
    station.last_tx_completed = completed;
    if (station.mac_update_pending) {
      continue;
    }
    if (!transmit_network()) {
      return ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN : CALL_UNAVAILABLE;
    }
    uint64_t deadline = task_deadline_after_ms(AX_LINK_STEP_MS);
    if (station.secure && !station.authenticated && deadline > station.attempt_deadline) {
      deadline = station.attempt_deadline;
    }
    if (station.rekey_deadline && deadline > station.rekey_deadline) {
      deadline = station.rekey_deadline;
    }
    ax200_transport_wait(event, deadline);
  }
}

static enum call_status wait_replacement_auth(void)
{
  station.auth_started = false;
  station.phase = STATION_IDLE;
  /* Called only after old station/key retirement. Releasing closed storage
   * lets the session use the last broker-context slot for its replacement. */
  uint64_t flags = cpu_save_interrupts();
  wifi_auth_release(station.work->auth);
  station.work->auth = NULL;
  cpu_restore_interrupts(flags);
  /* Allow two maximum session backoff intervals, including claim/drain work.
   * A stopped supervisor must not occupy the sole radio worker forever. */
  uint64_t replacement_deadline = task_deadline_after_ms(AX_LINK_AUTH_REPLACEMENT_MS);
  ax200_radio_detail("authentication worker lost; protected port closed while awaiting replacement");
  for (;;) {
    uint64_t event = ax200_transport_event();
    if (task_deadline_expired(replacement_deadline)) {
      station.failure = "authentication worker replacement deadline expired; terminal drained";
      return CALL_TIMED_OUT;
    }
    flags = cpu_save_interrupts();
    enum call_status status = wifi_connect_stop_reason(station.work);
    cpu_restore_interrupts(flags);
    if (status != CALL_OK) {
      return status;
    }
    if (!ax200_transport_poll() || ax200_radio_faulted()) {
      return CALL_OUTCOME_UNKNOWN;
    }
    uint64_t deadline = task_deadline_after_ms(AX_LINK_RESPONSE_MS);
    if (deadline > replacement_deadline) {
      deadline = replacement_deadline;
    }
    status = ax200_radio_refresh(deadline);
    if (status != CALL_OK && status != CALL_UNAVAILABLE) {
      return status;
    }
    if (ax200_transport_rfkill()) {
      return CALL_UNAVAILABLE;
    }
    if (status == CALL_OK) {
      flags = cpu_save_interrupts();
      status = wifi_connect_refresh_auth(station.work);
      cpu_restore_interrupts(flags);
      if (status == CALL_OK) {
        return CALL_OK;
      }
      if (status != CALL_UNAVAILABLE && status != CALL_BUSY && status != CALL_LIMIT) {
        return status;
      }
    }
    deadline = task_deadline_after_ms(100);
    if (deadline > replacement_deadline) {
      deadline = replacement_deadline;
    }
    ax200_transport_wait(event, deadline);
  }
}

static enum call_status recovery_wait(unsigned delay_ms)
{
  uint64_t deadline = task_deadline_after_ms(delay_ms);
  station.auth_started = false;
  station.phase = STATION_IDLE;
  ax200_radio_detail("ordinary AP loss; protected link recovery waiting");
  while (!task_deadline_expired(deadline)) {
    uint64_t event = ax200_transport_event();
    enum call_status stopped = stop_reason();
    if (stopped == CALL_ENDPOINT_CLOSED) {
      stopped = wait_replacement_auth();
    }
    if (stopped != CALL_OK) {
      return stopped;
    }
    if (!ax200_transport_poll() || ax200_radio_faulted()) {
      return CALL_OUTCOME_UNKNOWN;
    }
    enum call_status refreshed = ax200_radio_refresh(deadline);
    if (refreshed != CALL_OK && refreshed != CALL_UNAVAILABLE) {
      return refreshed;
    }
    if (ax200_transport_rfkill()) {
      return CALL_UNAVAILABLE;
    }
    ax200_transport_wait(event, deadline);
  }
  enum call_status stopped = stop_reason();
  return stopped == CALL_ENDPOINT_CLOSED ? wait_replacement_auth() : stopped;
}

void ax200_station_run(struct wifi_connect_work *work)
{
  bool published = false, first = true;
  unsigned recovery_ms = AX_LINK_RECOVERY_MIN_MS;
  enum call_status result = CALL_OK;
  for (;;) {
    memset(&station, 0, sizeof(station));
    station.work = work;
    station.secure = work->target.security == WIFI_SECURITY_WPA2_PSK;
    station.setup_finished = published;
    station.attempt_deadline = published ? task_deadline_after_ms(AX_LINK_HANDSHAKE_MS) : work->deadline_ns;
    uint64_t flags = cpu_save_interrupts();
    bool epoch = next_incarnation();
    network.snapshot.ready = false;
    network.snapshot.security = work->target.security;
    network.tx_count = network.rx_count = network.tx_read = network.rx_read = 0;
    bool admitted = epoch && (!first || wifi_connect_begin(work));
    if (admitted) {
      wifi_connection_state(work, false, false, false);
    }
    net_worker_notify();
    cpu_restore_interrupts(flags);
    first = false;
    if (!admitted) {
      result = stop_reason();
      if (result == CALL_OK) {
        result = CALL_UNAVAILABLE;
      }
    } else {
      result = prepare_station();
      if (result == CALL_OK) {
        result = run_station();
      }
    }
    published = station.setup_finished;
    ktrace("wifi: station retirement phase=%u status=%u setup-complete=%u response=%u failed=%u rejected=%u reason=%s\n",
        station.phase, result, published, station.response, station.failed,
        station.rejected, station.failure ? station.failure : "station stopped");
    ax200_radio_detail("station retiring; new data admission closed");
    flags = cpu_save_interrupts();
    station.port_authorized = false;
    network.snapshot.ready = false;
    if (!next_incarnation()) {
      result = CALL_UNAVAILABLE;
    }
    network.statistics.tx_dropped += network.tx_count;
    network.statistics.rx_dropped += network.rx_count;
    network.tx_count = network.rx_count = 0;
    wifi_connection_draining(work);
    net_worker_notify();
    cpu_restore_interrupts(flags);
    enum call_status auth_stopped = station.secure ? wifi_auth_stop_reason(work->auth, 0) : CALL_OK;
    flags = cpu_save_interrupts();
    enum call_status stopped = wifi_connect_stop_reason(work);
    cpu_restore_interrupts(flags);
    if (stopped != CALL_OK) {
      result = stopped;
    }
    bool worker_lost = auth_stopped == CALL_ENDPOINT_CLOSED;
    bool drained = !ax200_radio_faulted() && retire_station(task_deadline_after_ms(AX_LINK_DRAIN_MS));
    if (!drained || result == CALL_OUTCOME_UNKNOWN) {
      ax200_radio_quarantine("station firmware retirement uncertain; DMA retained until reboot");
      result = CALL_OUTCOME_UNKNOWN;
    }
    ax200_security_clear(&station.security);
    if (station.auth_started) {
      flags = cpu_save_interrupts();
      wifi_auth_retire(work->auth, station.association_generation, result);
      cpu_restore_interrupts(flags);
    }
    bool recover = station.secure && (station.ordinary_loss || worker_lost) &&
        (published || worker_lost) && !station.security_failure && !station.rejected &&
        drained && result != CALL_OUTCOME_UNKNOWN && stopped == CALL_OK;
    if (!recover) {
      break;
    }
    if (worker_lost) {
      result = wait_replacement_auth();
      if (result != CALL_OK) {
        break;
      }
    }
    if (station.authenticated) {
      recovery_ms = AX_LINK_RECOVERY_MIN_MS;
    }
    result = recovery_wait(recovery_ms);
    if (result != CALL_OK) {
      break;
    }
    if (recovery_ms < AX_LINK_RECOVERY_MAX_MS) {
      recovery_ms *= 2;
      if (recovery_ms > AX_LINK_RECOVERY_MAX_MS) {
        recovery_ms = AX_LINK_RECOVERY_MAX_MS;
      }
    }
  }
  if (result != CALL_OUTCOME_UNKNOWN) {
    ax200_radio_detail(station.failure ? station.failure : "station terminal drained");
  }
  uint64_t flags = cpu_save_interrupts();
  ax200_radio_connection_clear(work);
  station.work = NULL;
  station.phase = STATION_IDLE;
  if (!published) {
    wifi_connect_finished(work, result);
  }
  wifi_connection_retired(work, result);
  cpu_restore_interrupts(flags);
  memzero_explicit(&station, sizeof(station));
}
