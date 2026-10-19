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
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/wifi/ax200_transport.h>
#include "ax200_firmware.h"
#include "ax200_station.h"
#include "ax200_station_wire.h"

#define AX_LINK_QUEUE 32u
#define AX_LINK_STEP_MS 20u
#define AX_LINK_DRAIN_MS 2000u
#define AX_LINK_BEACON_LOSS_NS UINT64_C(3000000000)
#define AX_LINK_TX_STALL_NS UINT64_C(3000000000)
#define AX_LINK_RESPONSE_MS 1000u
#define AX_LINK_EXCHANGES 3u

struct ax_ethernet_frame { uint16_t bytes; uint8_t data[WIFI_ETHERNET_BYTES]; };
static struct {
  struct wifi_network_snapshot snapshot;
  uint64_t interface_id;
  struct wifi_network_statistics statistics;
  unsigned tx_read, tx_count, rx_read, rx_count;
  struct ax_ethernet_frame tx[AX_LINK_QUEUE], rx[AX_LINK_QUEUE];
} network;

enum station_phase { STATION_IDLE, STATION_OBSERVE, STATION_AUTH, STATION_ASSOC, STATION_DATA, STATION_DRAIN };
static struct {
  struct wifi_connect_work *work;
  enum station_phase phase;
  bool observed, rejected, response, failed, phy, mac, binding, peer, queue, session, session_ended;
  uint8_t address[6], rates[12], rate_count, dtim_count, dtim_period, erp;
  uint16_t legacy_rates, basic_rates, capabilities, beacon_interval, aid, sequence, last_rx_sequence;
  uint64_t rx_sequence_window;
  bool last_rx_valid;
  uint32_t beacon_arrival;
  uint64_t beacon_tsf, last_beacon, last_tx_progress;
  uint64_t last_tx_completed;
} station;

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
  if (!network.snapshot.ready || generation != network.snapshot.generation || !network.rx_count) {
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

static bool platform_policy(uint64_t deadline)
{
  struct acpi_wifi_policy policy;
  uint64_t flags = cpu_save_interrupts();
  enum acpi_wifi_state captured = acpi_wifi_snapshot(&policy);
  cpu_restore_interrupts(flags);
  struct pci_address address;
  if (captured != ACPI_WIFI_VALID || !ax200_transport_pci_address(&address) ||
      memcmp(&address, &policy.address, sizeof(address)) ||
      !policy_state_known(policy.glai.state) ||
      (policy.glai.state == ACPI_WIFI_VALID && policy.glai.value) ||
      !policy_state_known(policy.sar.state) || !policy_state_known(policy.ewrd.state) ||
      !policy_state_known(policy.geo.state) || !policy_state_known(policy.ppag.state) ||
      !policy_state_known(policy.dsm.state)) {
    return false;
  }
  /* PPAG applicability requires a known approved platform identity. The native
   * snapshot currently supplies no SMBIOS proof; enabled PPAG cannot be guessed. */
  if (policy.ppag.state == ACPI_WIFI_VALID && policy.ppag.flags) {
    return false;
  }
  uint32_t lari[6] = {0};
  if (policy.dsm.state == ACPI_WIFI_VALID) {
    static const uint8_t functions[] = {1, 2, 3, 4, 6, 7, 8, 9};
    for (size_t f = 0; f < sizeof(functions); ++f) {
      unsigned i = functions[f];
      if ((policy.dsm.supported & (1u << i)) && !policy_state_known(policy.dsm.function[i].state)) {
        return false;
      }
    }
    if (policy.dsm.function[1].state == ACPI_WIFI_VALID) {
      unsigned value = policy.dsm.function[1].value;
      if (value > 2) {
        return false;
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
      return false;
    }
  }
  if (policy.sar.state != ACPI_WIFI_VALID) {
    return policy.geo.state != ACPI_WIFI_VALID;
  }
  if (policy.sar.chains < 2 || policy.sar.bands < 5 || policy.sar.revision > 2 ||
      policy.sar.flags >> 1 || !ax200_firmware_api(AX_STATION_API_TX_POWER)) {
    return false;
  }
  uint8_t power[44] = {0};
  power[0] = 2; /* SET_CHAINS; preserve the raw eighth-dBm platform limits. */
  for (unsigned chain = 0; chain < 2; ++chain) {
    for (unsigned band = 0; band < 5; ++band) {
      put16(power + 16 + (chain * 5 + band) * 2, policy.sar.limits[chain][band]);
    }
  }
  if (!fixed(AX200_LONG, AX_STATION_TX_POWER, power, sizeof(power), deadline)) {
    return false;
  }
  if (policy.geo.state == ACPI_WIFI_VALID) {
    if (policy.geo.profiles > 8 || policy.geo.profiles < 3 || policy.geo.bands < 2 ||
        policy.geo.revision > 3) {
      return false;
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
      return false;
    }
  }
  return true;
}

static const uint8_t legacy_rates[12] = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};
struct station_elements {
  const uint8_t *ssid;
  uint8_t ssid_bytes, dtim_count, dtim_period, erp;
  uint16_t rates, basic;
  bool ssid_seen, rates_seen, extended_seen, tim_seen, erp_seen;
};

static bool add_rates(struct station_elements *elements, const uint8_t *rates, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    unsigned index = 0;
    while (index < 12 && legacy_rates[index] != (rates[i] & 0x7f)) {
      ++index;
    }
    if (index == 12) {
      if (rates[i] & 0x80) {
        return false;
      }
      continue;
    }
    if (elements->rates & (1u << index)) {
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
    if (bytes - offset < 2) {
      return false;
    }
    uint8_t id = data[offset], length = data[offset + 1];
    offset += 2;
    if (length > bytes - offset) {
      return false;
    }
    const uint8_t *value = data + offset;
    switch (id) {
    case AX_STATION_IE_SSID:
      if (elements->ssid_seen || length > 32) {
        return false;
      }
      elements->ssid_seen = true;
      elements->ssid = value;
      elements->ssid_bytes = length;
      break;
    case AX_STATION_IE_RATES:
      if (elements->rates_seen || !length || length > 8 || !add_rates(elements, value, length)) {
        return false;
      }
      elements->rates_seen = true;
      break;
    case AX_STATION_IE_EXTENDED_RATES:
      if (elements->extended_seen || !length || !add_rates(elements, value, length)) {
        return false;
      }
      elements->extended_seen = true;
      break;
    case AX_STATION_IE_CHANNEL:
      if (length != 1 || value[0] != station.work->target.channel) {
        return false;
      }
      break;
    case AX_STATION_IE_TIM:
      if (elements->tim_seen || length < 4 || !value[1] || value[0] >= value[1]) {
        return false;
      }
      elements->tim_seen = true;
      elements->dtim_count = value[0];
      elements->dtim_period = value[1];
      break;
    case AX_STATION_IE_CHANNEL_SWITCH: /* A channel switch is outside the one-channel profile. */
    case AX_STATION_IE_RSN: /* RSN */
    case AX_STATION_IE_RSNXE: /* RSNXE */
      return false;
    case AX_STATION_IE_ERP:
      if (elements->erp_seen || length != 1 || (value[0] & ~7u)) {
        return false;
      }
      elements->erp_seen = true;
      elements->erp = value[0];
      break;
    case AX_STATION_IE_HT_OPERATION:
      if (length != 22 || value[0] != station.work->target.channel || (value[1] & 7)) {
        return false;
      }
      break;
    case AX_STATION_IE_VENDOR:
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
  return elements->rates_seen && elements->rates && elements->basic;
}

static void close_admission(void)
{
  station.failed = true;
  uint64_t flags = cpu_save_interrupts();
  network.snapshot.ready = false;
  net_worker_notify();
  cpu_restore_interrupts(flags);
}

static void observe_beacon(const uint8_t *frame, size_t bytes, uint32_t arrival)
{
  if (bytes < 36) {
    return;
  }
  bool target = !memcmp(frame + 16, station.work->target.bssid, 6);
  struct station_elements elements;
  bool valid = parse_elements(frame + 36, bytes - 36, &elements);
  if (!target) {
    if (station.phase == STATION_OBSERVE && valid && elements.ssid_seen &&
        elements.ssid_bytes == station.work->target.ssid_bytes &&
        !memcmp(elements.ssid, station.work->target.ssid, elements.ssid_bytes)) {
      station.rejected = true;
      close_admission();
    }
    return;
  }
  uint16_t capabilities = get16(frame + 34), interval = get16(frame + 32);
  if (!valid || !elements.ssid_seen || !elements.tim_seen ||
      elements.ssid_bytes != station.work->target.ssid_bytes ||
      memcmp(elements.ssid, station.work->target.ssid, elements.ssid_bytes) ||
      memcmp(frame + 10, station.work->target.bssid, 6) ||
      !(capabilities & AX_STATION_CAP_ESS) ||
      (capabilities & (AX_STATION_CAP_PRIVACY | 2)) || !interval || interval > 1000) {
    station.rejected = true;
    close_admission();
    return;
  }
  if (station.observed && (station.legacy_rates != elements.rates ||
      station.basic_rates != elements.basic || station.capabilities != capabilities ||
      station.beacon_interval != interval || station.erp != elements.erp)) {
    station.rejected = true;
    close_admission();
    return;
  }
  station.observed = true;
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

static void receive_data(const uint8_t *frame, size_t bytes)
{
  static const uint8_t snap[6] = {0xaa, 0xaa, 3, 0, 0, 0};
  if (station.phase != STATION_DATA || frame[0] != AX_STATION_DATA ||
      (frame[1] & 3) != 2 || (frame[1] & (AX_STATION_MORE_FRAG | AX200_FRAME_PROTECTED)) ||
      (get16(frame + 22) & 15) || bytes < 32 || bytes > 1532 ||
      memcmp(frame + 10, station.work->target.bssid, 6) || (frame[16] & 1) ||
      (!(frame[4] & 1) && memcmp(frame + 4, station.address, 6)) ||
      memcmp(frame + 24, snap, sizeof(snap)) || get16(frame + 30) == 0) {
    return;
  }
  /* Only EtherType Ethernet frames are admitted; no 802.3 length fallback. */
  unsigned ether_type = (unsigned)frame[30] << 8 | frame[31];
  if (ether_type < 1536) {
    return;
  }
  uint16_t sequence = get16(frame + 22) >> 4;
  if (!station.last_rx_valid) {
    station.last_rx_valid = true;
    station.last_rx_sequence = sequence;
    station.rx_sequence_window = 1;
  } else {
    unsigned forward = (sequence - station.last_rx_sequence) & 0xfff;
    if (forward < 2048 && forward) {
      station.rx_sequence_window = forward < 64 ? (station.rx_sequence_window << forward) | 1 : 1;
      station.last_rx_sequence = sequence;
    } else {
      unsigned back = (station.last_rx_sequence - sequence) & 0xfff;
      if (back >= 64 || (station.rx_sequence_window & (UINT64_C(1) << back))) {
        return;
      }
      station.rx_sequence_window |= UINT64_C(1) << back;
    }
  }
  uint64_t flags = cpu_save_interrupts();
  if (network.snapshot.ready && network.rx_count < AX_LINK_QUEUE) {
    struct ax_ethernet_frame *owned = &network.rx[(network.rx_read + network.rx_count) % AX_LINK_QUEUE];
    owned->bytes = bytes - 18;
    memcpy(owned->data, frame + 4, 6);
    memcpy(owned->data + 6, frame + 16, 6);
    memcpy(owned->data + 12, frame + 30, bytes - 30);
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

void ax200_station_notify(uint8_t group, uint8_t opcode, const uint8_t *payload, size_t bytes)
{
  if (!station.work || station.phase == STATION_IDLE || station.phase == STATION_DRAIN) {
    return;
  }
  if (((group == AX200_LONG || group == AX200_LEGACY) && opcode == AX200_MCC_CHANGED) ||
      (group == AX200_PHY && opcode == AX200_THERMAL_KILL)) {
    close_admission();
    return;
  }
  if (group == AX_STATION_MAC_GROUP && opcode == 0xfb) {
    if (bytes != 16 || get32(payload) || get32(payload + 12) != 0 ||
        get32(payload + 4) > 1 || get32(payload + 8) > 1) {
      close_admission();
    } else if (!get32(payload + 4)) {
      close_admission();
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
      (status & (AX200_RX_SECURITY_MASK | AX200_RX_RFKILL)) ||
      payload[34] != station.work->target.channel || (payload[3] & AX200_RX_AMSDU) ||
      (get16(payload + 5) & AX_STATION_RX_AMPDU)) {
    return;
  }
  const uint8_t *borrowed = payload + AX200_RX_DESCRIPTOR_BYTES;
  if ((borrowed[0] & 3) || (borrowed[1] & AX_STATION_MORE_FRAG) ||
      (get16(borrowed + 22) & 15)) {
    return;
  }
  /* Hardware inserts two bytes after the MAC header. Copy only while the RX
   * loan is live; all parsing and queues use this bounded owned frame. */
  uint8_t frame[AX_STATION_FRAME_BYTES];
  bool padded = payload[3] & AX200_RX_PAD;
  if (length > sizeof(frame) + (padded ? 2 : 0) || (padded && length < 26)) {
    return;
  }
  memcpy(frame, borrowed, 24);
  memcpy(frame + 24, borrowed + 24 + (padded ? 2 : 0), length - 24 - (padded ? 2 : 0));
  length -= padded ? 2 : 0;
  if (frame[0] == AX200_FRAME_BEACON && !(frame[1] & (3 | AX200_FRAME_PROTECTED))) {
    observe_beacon(frame, length, get32(payload + 36));
    return;
  }
  if ((frame[0] & 0x0c) == AX_STATION_DATA) {
    receive_data(frame, length);
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
      close_admission();
    }
  } else if (frame[0] == AX_STATION_AUTH && station.phase == STATION_AUTH) {
    if (length != 30 || get16(frame + 24) || get16(frame + 26) != 2 || get16(frame + 28)) {
      close_admission();
    } else {
      station.response = true;
    }
  } else if (frame[0] == AX_STATION_ASSOC_RESPONSE && station.phase == STATION_ASSOC) {
    struct station_elements elements;
    uint16_t capabilities = length >= 30 ? get16(frame + 24) : 0;
    uint16_t aid = length >= 30 ? get16(frame + 28) & 0x3fff : 0;
    if (length < 30 || get16(frame + 26) || !(capabilities & AX_STATION_CAP_ESS) ||
        (capabilities & (AX_STATION_CAP_PRIVACY | 2)) || aid < 1 || aid > 2007 ||
        !parse_elements(frame + 30, length - 30, &elements) ||
        (elements.basic & ~station.legacy_rates) ||
        (elements.rates & station.legacy_rates) != station.legacy_rates) {
      close_admission();
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
  struct ax_station_mac request = {.action = action, .type = AX_STATION_BSS_STA};
  if (action == AX_STATION_REMOVE) {
    return fixed(AX200_LONG, AX_STATION_MAC, &request, sizeof(request), deadline);
  }
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

static size_t management_frame(uint8_t frame[80], bool association)
{
  memset(frame, 0, 80);
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
      (station.capabilities & (AX_STATION_CAP_SHORT_PREAMBLE | AX_STATION_CAP_SHORT_SLOT));
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
  return offset;
}

static bool exchange(bool association)
{
  station.phase = association ? STATION_ASSOC : STATION_AUTH;
  station.response = false;
  ax200_radio_detail(association ? "open station association in progress" : "Open System authentication in progress");
  for (unsigned attempt = 0; attempt < AX_LINK_EXCHANGES; ++attempt) {
    if (stop_reason() != CALL_OK || !ax200_radio_tx_allowed(station.work->target.channel)) {
      return false;
    }
    uint8_t frame[80];
    size_t bytes = management_frame(frame, association);
    if (!ax200_transport_transmit(frame, bytes, true, management_rate())) {
      return false;
    }
    uint64_t deadline = task_deadline_after_ms(AX_LINK_RESPONSE_MS);
    if (deadline > station.work->deadline_ns) {
      deadline = station.work->deadline_ns;
    }
    while (!task_deadline_expired(deadline)) {
      uint64_t event = ax200_transport_event();
      if (!ax200_transport_poll() || ax200_radio_faulted() || station.failed || station.rejected ||
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

static bool transmit_network(void)
{
  for (unsigned sent = 0; sent < 8; ++sent) {
    struct ax_ethernet_frame owned;
    uint64_t flags = cpu_save_interrupts();
    if (!network.snapshot.ready || !network.tx_count || ax200_transport_tx_pending() >= AX_STATION_DATA_HIGH_WATER) {
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
    memcpy(frame + 4, station.work->target.bssid, 6);
    memcpy(frame + 10, station.address, 6);
    memcpy(frame + 16, owned.data, 6);
    put16(frame + 22, station.sequence << 4);
    station.sequence = (station.sequence + 1) & 0xfff;
    static const uint8_t snap[6] = {0xaa, 0xaa, 3, 0, 0, 0};
    memcpy(frame + 24, snap, sizeof(snap));
    memcpy(frame + 30, owned.data + 12, owned.bytes - 12);
    if (!ax200_transport_transmit(frame, owned.bytes + 18, false, 0)) {
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

void ax200_station_run(struct wifi_connect_work *work)
{
  memset(&station, 0, sizeof(station));
  station.work = work;
  station.phase = STATION_IDLE;
  enum call_status result = CALL_OK;
  bool setup_finished = false;
  uint64_t flags = cpu_save_interrupts();
  bool epoch = next_incarnation();
  network.snapshot.ready = false;
  network.tx_count = network.rx_count = network.tx_read = network.rx_read = 0;
  bool begin = epoch && wifi_connect_begin(work);
  net_worker_notify();
  cpu_restore_interrupts(flags);
  if (!begin) {
    result = stop_reason();
    if (result == CALL_OK) {
      result = CALL_UNAVAILABLE;
    }
    goto retire;
  }
  ax200_radio_detail("passively confirming the explicit open station target");
  if (!ax200_transport_address(station.address) || !firmware_versions() ||
      !ax200_radio_target_scan(work) || !station.observed || station.rejected) {
    result = ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN : CALL_UNAVAILABLE;
    goto retire;
  }
  ax200_radio_detail("validating platform transmit limits and explicit country");
  if (stop_reason() != CALL_OK || !platform_policy(work->deadline_ns) ||
      !ax200_radio_transmit_policy(work->target.channel, work->deadline_ns)) {
    result = CALL_DENIED;
    goto retire;
  }
  ax200_radio_detail("configuring legacy 20 MHz PHY/MAC/station");
  if (!configure_station(work->deadline_ns) || !exchange(false) || !exchange(true) ||
      !data_configuration(work->deadline_ns)) {
    result = ax200_radio_faulted() ? CALL_OUTCOME_UNKNOWN : CALL_IO;
    goto retire;
  }
  if (stop_reason() != CALL_OK || !ax200_radio_tx_allowed(work->target.channel) || station.failed || station.rejected) {
    result = CALL_ABANDONED;
    goto retire;
  }
  station.phase = STATION_DATA;
  ax200_radio_detail("open legacy link L2 ready; MTU 1500; TX ring 256");
  station.last_tx_progress = arch_monotonic_ns();
  flags = cpu_save_interrupts();
  network.snapshot.ready = true;
  memcpy(network.snapshot.mac, station.address, 6);
  wifi_connect_finished(work, CALL_OK);
  setup_finished = true;
  net_worker_notify();
  cpu_restore_interrupts(flags);
  for (;;) {
    uint64_t event = ax200_transport_event();
    if (!ax200_transport_poll() || ax200_radio_faulted()) {
      result = CALL_OUTCOME_UNKNOWN;
      break;
    }
    result = stop_reason();
    if (result != CALL_OK) {
      break;
    }
    if (station.failed || station.rejected || ax200_transport_rfkill() ||
        !ax200_radio_tx_allowed(work->target.channel)) {
      result = CALL_UNAVAILABLE;
      break;
    }
    uint64_t now = arch_monotonic_ns();
    if (now - station.last_beacon >= AX_LINK_BEACON_LOSS_NS) {
      result = CALL_TIMED_OUT;
      break;
    }
    unsigned pending = ax200_transport_tx_pending();
    uint64_t completed = ax200_transport_tx_completed();
    if (!pending || completed != station.last_tx_completed) {
      station.last_tx_progress = now;
    } else if (now - station.last_tx_progress >= AX_LINK_TX_STALL_NS) {
      result = CALL_TIMED_OUT;
      break;
    }
    station.last_tx_completed = completed;
    if (!transmit_network()) {
      result = CALL_UNAVAILABLE;
      break;
    }
    ax200_transport_wait(event, task_deadline_after_ms(AX_LINK_STEP_MS));
  }
retire:
  ax200_radio_detail("station retiring; new data admission closed");
  flags = cpu_save_interrupts();
  network.snapshot.ready = false;
  if (!next_incarnation()) {
    result = CALL_UNAVAILABLE;
  }
  network.statistics.tx_dropped += network.tx_count;
  network.statistics.rx_dropped += network.rx_count;
  network.tx_count = network.rx_count = 0;
  net_worker_notify();
  cpu_restore_interrupts(flags);
  enum call_status stopped = stop_reason();
  if (!setup_finished && stopped != CALL_OK) {
    result = stopped;
  }
  bool drained = !ax200_radio_faulted() && retire_station(task_deadline_after_ms(AX_LINK_DRAIN_MS));
  if (!drained || result == CALL_OUTCOME_UNKNOWN) {
    ax200_radio_quarantine("station firmware retirement uncertain; DMA retained until reboot");
    result = CALL_OUTCOME_UNKNOWN;
  }
  if (drained && result != CALL_OUTCOME_UNKNOWN) {
    ax200_radio_detail("station terminal drained; fresh explicit connect required");
  }
  flags = cpu_save_interrupts();
  ax200_radio_connection_clear(work);
  station.work = NULL;
  station.phase = STATION_IDLE;
  if (!setup_finished) {
    wifi_connect_finished(work, result);
  }
  wifi_connection_retired(work, result);
  cpu_restore_interrupts(flags);
  memzero_explicit(&station, sizeof(station));
}
