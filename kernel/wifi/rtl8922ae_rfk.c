/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89 fw.c/fw.h and phy.c at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449; BSD alternative.
 * Complete notice: kernel/wifi/NOTICE.
 */
#include <kernel/memory.h>
#include "rtl8922ae_io.h"
#include "rtl8922ae_rfk.h"
#include "rtl8922ae_transport.h"

#define H2C_CATEGORY_RF 2u
#define H2C_CLASS_RFK 11u
#define H2C_RFK_TSSI 0u
#define H2C_RFK_DACK 5u
#define H2C_RFK_RXDCK 6u
#define H2C_RFK_PRE_NOTIFY 8u
#define C2H_CATEGORY_MAC 1u
#define C2H_CLASS_INFO 0u
#define C2H_DONE_ACK 1u
#define C2H_CLASS_RFK_REPORT 9u
#define C2H_RFK_STATE 0u
#define RFK_STATE_START 0u
#define RFK_STATE_OK 1u
#define RFK_STATE_LAST 4u
#define RFK_HEADER_BYTES 8u
#define RFK_REPORT_BYTES 10u
#define RFK_H2C_RETURN_MASK UINT32_C(0x00ff0000)
#define RFK_BOTH_PATHS 3u
#define RFK_TSSI_SCAN 1u
#define RFK_MLO_SINGLE_PHY UINT32_C(0x1002)
#define TSSI_REFERENCE_THERMAL 0x38u
#define TSSI_THERMAL_POINTS 128u
#define TSSI_TRACKING_POINTS 30u
#define TRACKING_2GA_N 10u
#define TRACKING_2GA_P 11u
#define TRACKING_2GB_N 8u
#define TRACKING_2GB_P 9u
#define RFK_POLL_NS UINT64_C(50000)
#define NS_PER_MS UINT64_C(1000000)

/* The BSP worker owns both this context and all runtime C2H dispatch. */
static struct {
  struct rtl8922ae_rfk_observations observations;
  bool active, complete, fault;
} rfk;

static uint32_t get32(const uint8_t *bytes)
{
  return bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 |
      (uint32_t)bytes[3] << 24;
}

static void put32(uint8_t *bytes, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) bytes[i] = value >> (8 * i);
}

static bool fail(const char **detail, const char *message)
{
  *detail = message;
  return false;
}

void rtl8922ae_rfk_reset(void)
{
  memset(&rfk, 0, sizeof(rfk));
}

void rtl8922ae_rfk_observe(struct rtl8922ae_rfk_observations *out)
{
  *out = rfk.observations;
}

bool rtl8922ae_runtime_receive(const void *data, size_t bytes)
{
  if (bytes < RFK_HEADER_BYTES || rfk.fault) return false;
  const uint8_t *packet = data;
  uint32_t header = get32(packet);
  unsigned category = header & 3u, class_id = header >> 2 & 63u;
  unsigned function = header >> 8 & 255u;
  if (category == C2H_CATEGORY_MAC && class_id == C2H_CLASS_INFO && function == C2H_DONE_ACK) {
    if (bytes < 12 || (get32(packet + 8) & RFK_H2C_RETURN_MASK)) {
      rfk.fault = true;
      return false;
    }
  }
  if (category != H2C_CATEGORY_RF || class_id != C2H_CLASS_RFK_REPORT || function != C2H_RFK_STATE) {
    return true;
  }
  if (bytes < RFK_REPORT_BYTES || packet[8] > RFK_STATE_LAST) {
    rfk.fault = true;
    return false;
  }
  /* Unsolicited bootstrap reports cannot satisfy an operation. Commands are
   * strictly serialized and no next command is sent after uncertainty. */
  if (!rfk.active) return true;
  if (rfk.complete && (rfk.observations.state != packet[8] ||
      rfk.observations.version != packet[9])) {
    rfk.fault = true;
    return false;
  }
  rfk.observations.state = packet[8];
  rfk.observations.version = packet[9];
  rfk.observations.state_valid = true;
  rfk.complete = packet[8] != RFK_STATE_START;
  return true;
}

static bool run(unsigned function, const void *data, size_t bytes, unsigned timeout_ms,
    uint32_t bit, uint64_t deadline, const char **detail)
{
  if (rfk.active || rfk.fault || !rtl8922ae_transport_poll()) {
    return fail(detail, "RTL8922AE RFK transport unavailable");
  }
  rfk.complete = false;
  rfk.active = true;
  uint64_t local = rtl8922ae_deadline(deadline, timeout_ms * NS_PER_MS);
  bool submitted = rtl8922ae_transport_h2c(H2C_CATEGORY_RF, H2C_CLASS_RFK, function,
      false, false, data, bytes, NULL, local, detail);
  bool completed = false;
  if (submitted) {
    do {
      if (!rtl8922ae_transport_poll() || rfk.fault) break;
      if (rfk.complete) {
        completed = rfk.observations.state == RFK_STATE_OK;
        break;
      }
    } while (rtl8922ae_sleep(RFK_POLL_NS, local));
  }
  rfk.active = false;
  if (!completed) {
    rfk.fault = true;
    if (!submitted) return false;
    switch (function) {
    case H2C_RFK_PRE_NOTIFY: return fail(detail, "RTL8922AE PRE_NOTIFY completion failed/timed out");
    case H2C_RFK_DACK: return fail(detail, "RTL8922AE DACK completion failed/timed out");
    case H2C_RFK_RXDCK: return fail(detail, "RTL8922AE RXDCK completion failed/timed out");
    default: return fail(detail, "RTL8922AE TSSI completion failed/timed out");
    }
  }
  rfk.observations.completed |= bit;
  return true;
}

bool rtl8922ae_rfk_prepare(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_board *board, uint8_t cut, uint8_t channel,
    struct rtl8922ae_rfk_commands *commands, const char **detail)
{
  memset(commands, 0, sizeof(*commands));
  if (!board->tssi_known || !channel || channel > 13) {
    return fail(detail, "RTL8922AE TSSI factory inputs unavailable");
  }
  unsigned group = channel <= 2 ? 0 : channel <= 5 ? 1 : channel <= 8 ? 2 :
      channel <= 11 ? 3 : 4;
  uint8_t *tssi = commands->tssi;
  tssi[0] = sizeof(commands->tssi) & 255;
  tssi[1] = sizeof(commands->tssi) >> 8;
  tssi[3] = channel;
  tssi[6] = 1; /* Reference hwtx_en: calibration is TX-capable. */
  tssi[7] = cut;
  for (unsigned path = 0; path < 2; ++path) {
    int8_t cck = board->tssi_cck[path][group];
    int8_t ofdm = board->tssi_ofdm_2g[path][group];
    tssi[10 + path] = cck;
    tssi[12 + path] = cck;
    tssi[14 + path] = cck;
    for (unsigned i = 1; i <= 6; ++i) tssi[16 + i * 2 + path] = ofdm;
    /* RTL8922A has no extra TSSI trim parser; its optional DE adjustment and
     * modulation-difference fields remain zero as in the reference. */
    uint8_t thermal = board->tssi_thermal[path];
    if (thermal == UINT8_MAX) {
      commands->thermal_default_paths |= 1u << path;
      tssi[40 + path] = TSSI_REFERENCE_THERMAL;
      continue;
    }
    tssi[40 + path] = thermal;
    for (unsigned i = 0; i < TSSI_THERMAL_POINTS; ++i) {
      unsigned position = i ^ 3u;
      unsigned point = position < 64 ? position : 128 - position;
      if (point >= TSSI_TRACKING_POINTS) point = TSSI_TRACKING_POINTS - 1;
      unsigned type = path ? (position < 64 ? TRACKING_2GB_P : TRACKING_2GB_N) :
          (position < 64 ? TRACKING_2GA_P : TRACKING_2GA_N);
      int8_t delta;
      if (rtl8922ae_firmware_tracking_delta(asset, bytes, tables, type, 0, point,
          &delta) != RTL8922AE_TABLES_OK) {
        memset(commands, 0, sizeof(*commands));
        return fail(detail, "RTL8922AE TSSI tracking table unavailable");
      }
      tssi[42 + path * TSSI_THERMAL_POINTS + i] = position < 64 ? delta : -delta;
    }
  }
  tssi[298] = RFK_TSSI_SCAN;
  tssi[299] = board->rfe;
  /* First one-PHY context: both paths use their first vacant coefficient slot. */
  for (unsigned path = 0; path < 2; ++path) {
    put32(commands->pre_notify + path * 12, channel);
    put32(commands->pre_notify + 52 + path * 4, channel);
  }
  put32(commands->pre_notify + 48, RFK_MLO_SINGLE_PHY);
  commands->channel = channel;
  commands->valid = true;
  return true;
}

bool rtl8922ae_rfk_pre_notify(const struct rtl8922ae_rfk_commands *commands,
    uint64_t deadline, const char **detail)
{
  if (!commands->valid) return fail(detail, "RTL8922AE RFK inputs unavailable");
  return run(H2C_RFK_PRE_NOTIFY, commands->pre_notify, sizeof(commands->pre_notify), 5,
      RTL8922AE_RFK_PRE_NOTIFY, deadline, detail);
}

bool rtl8922ae_rfk_dack(uint64_t deadline, const char **detail)
{
  uint8_t dack[12] = {0};
  put32(dack, sizeof(dack));
  return run(H2C_RFK_DACK, dack, sizeof(dack), 58, RTL8922AE_RFK_DACK, deadline, detail);
}

bool rtl8922ae_rfk_rxdck(const struct rtl8922ae_rfk_commands *commands,
    uint64_t deadline, const char **detail)
{
  if (!commands->valid) return fail(detail, "RTL8922AE RFK inputs unavailable");
  uint8_t rxdck[9] = {sizeof(rxdck), 0, 0, RFK_BOTH_PATHS, 0, 0, commands->channel, 0, 0};
  return run(H2C_RFK_RXDCK, rxdck, sizeof(rxdck), 128, RTL8922AE_RFK_RXDCK, deadline, detail);
}

bool rtl8922ae_rfk_tssi(const struct rtl8922ae_rfk_commands *commands,
    uint64_t deadline, const char **detail)
{
  if (!commands->valid) return fail(detail, "RTL8922AE TSSI inputs unavailable");
  return run(H2C_RFK_TSSI, commands->tssi, sizeof(commands->tssi), 6,
      RTL8922AE_RFK_TSSI, deadline, detail);
}
