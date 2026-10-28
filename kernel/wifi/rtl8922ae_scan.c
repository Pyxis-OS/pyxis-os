/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89 fw.c/fw.h and cam.c/cam.h at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449; BSD alternative.
 * Complete notice: kernel/wifi/NOTICE.
 */
#include <kernel/format.h>
#include <kernel/memory.h>
#include <kernel/string.h>
#include "rtl8922ae_io.h"
#include "rtl8922ae_scan.h"
#include "rtl8922ae_transport.h"

#define H2C_CATEGORY_MAC 1u
#define H2C_CLASS_FRAME_EXCHANGE 5u
#define H2C_CLASS_ADDRESS_CAM 6u
#define H2C_CLASS_MEDIA 8u
#define H2C_CLASS_OFFLOAD 9u
#define H2C_DEFAULT_DMAC 0x0cu
#define H2C_DEFAULT_CMAC 0x11u
#define H2C_CAM_UPDATE 0u
#define H2C_JOIN_INFO 0u
#define H2C_ROLE_MAINTAIN 4u
#define H2C_OFLD_CFG 0x14u
#define H2C_ADD_SCAN_CHANNEL 0x16u
#define H2C_SCAN_BE 0x2cu
#define H2C_MACID_PAUSE_SLEEP 0x28u
#define C2H_CLASS_INFO 0u
#define C2H_CLASS_OFFLOAD 1u
#define C2H_DONE_ACK 1u
#define C2H_SCAN_REPORT 9u
#define C2H_HEADER_BYTES 8u
#define C2H_ACK_BYTES 12u
#define C2H_SCAN_BYTES 36u
#define C2H_SCAN_RETURN_MASK 0x3fu
#define SCAN_END_NOTIFY 5u
#define SCAN_ENTER_CHANNEL_NOTIFY 3u
#define SCAN_STATUS_SUCCESS 1u
#define SCAN_NOTIFY_MASK 0x1fu
#define SCAN_PACKET_NONE 0xffu
#define SCAN_CHANNEL_INVALID 0xffu
#define SCAN_START 1u
#define SCAN_STOP 0u
#define SCAN_CHANNEL_TIME_MS 45u
#define SCAN_PAUSE_DATA (UINT32_C(1) << 6)
#define SCAN_NOTIFY_SHIFT 9u
#define SCAN_NOTIFY_END (UINT32_C(1) << 6)
#define SCAN_LEARN_CHANNEL (UINT32_C(1) << 7)
#define SCAN_CONFIG_BYTES 40u
#define SCAN_CHANNEL_BYTES 32u
#define SCAN_LIST_BYTES (4u + SCAN_CHANNEL_BYTES)
#define SCAN_MACC_ROLE_WORDS 1u
#define SCAN_OP_CHANNEL_WORDS 5u
#define SCAN_MLO_1_PLUS_1_1RF UINT32_C(0x1011)
#define SCAN_ALL_6GHZ_DISABLED UINT64_C(0x07ffffffffffffff)
#define FIRMWARE_WAIT_NS UINT64_C(100000000)
#define SCAN_POLL_NS UINT64_C(50000)
#define WIFI_ROLE_STATION 1u
#define ROLE_WIFI_SHIFT 13u
#define JOIN_DISCONNECT (UINT32_C(1) << 8)
#define JOIN_WIFI_SHIFT 26u
#define JOIN_MLO_MLSR (UINT32_C(1) << 12)
#define JOIN_EMLSR_PADDING_SHIFT 16u
#define JOIN_EMLSR_TRANSITION_SHIFT 19u
#define JOIN_EMLSR_PADDING_256US 4u
#define JOIN_EMLSR_TRANSITION_256US 5u
#define MACID_PAUSE_SLEEP_BYTES 256u
#define MACID_PAUSE_MASK_OFFSET 16u
#define MACID_SLEEP_MASK_OFFSET 48u
#define MACID_ZERO_MASK UINT32_C(1)
#define CAM_COMMAND_BYTES 60u
#define CAM_ADDRESS_BYTES 0x20u
#define CAM_BSSID_BYTES 8u
#define CAM_VALID UINT32_C(1)
#define CAM_SECURITY_NORMAL (UINT32_C(2) << 16)
#define CAM_BSSID_MATCH_ALL (UINT32_C(0x3f) << 2)
#define CONTROL_DATA_WORDS 16u
#define CONTROL_COMMAND_BYTES (4u * (1u + 2u * CONTROL_DATA_WORDS))
#define CONTROL_UPDATE (UINT32_C(1) << 7)
#define CMAC_DATA_RATE 4u
#define CMAC_RTS_RATE 0xau
#define CMAC_RESPONSE_RATE 0xbu
#define CMAC_NOMINAL_PADDING (UINT32_C(2) | UINT32_C(2) << 2 | \
    UINT32_C(2) << 4 | UINT32_C(2) << 6 | UINT32_C(2) << 8)
#define CMAC_ACTIVE_SUBCHANNELS (UINT32_C(0xffff) << 16)
#define CMAC_CSI_NC UINT32_C(1)
#define CMAC_CSI_NR (UINT32_C(1) << 3)
#define CMAC_CSI_CB (UINT32_C(1) << 8)
#define CMAC_CSI_PARAMETER_ENABLE (UINT32_C(1) << 15)
#define CMAC_CSI_RATE (UINT32_C(0xb) << 16)
#define CMAC_W0_MASK UINT32_C(0xfff7ffff)
#define CMAC_W4_MASK UINT32_C(0xffffdfff)
#define CMAC_W5_MASK UINT32_C(0xffff7fff)
#define CMAC_W6_MASK UINT32_C(0x80ffffff)
#define CMAC_W7_MASK UINT32_C(0xefffefff)
#define CMAC_W8_MASK UINT32_C(0x0000ffff)
#define CMAC_W15_MASK UINT32_C(0x0fffffff)
#define DMAC_W4_MASK UINT32_C(0xffffffe0)
#define DMAC_W5_MASK UINT32_C(0xffffff0f)
#define DMAC_W12_MASK UINT32_C(0x0000ffff)
#define ALL_WORD_BITS UINT32_MAX

/* The worker owns these flags. C2H delivery is synchronous with transport poll;
 * like Linux's fw_ofld_wait, completion matches the command, not H2C sequence. */
static struct {
  bool ready, running, ended, end_ok, fault, uncertain;
  bool waiting, acked, ack_ok;
  bool offload_acked;
  uint8_t wait_function, channel, ack_return, ack_sequence;
  uint8_t offload_return, offload_sequence;
  uint8_t cam[CAM_COMMAND_BYTES];
} scan;
static char ack_detail[WIFI_DETAIL_BYTES];

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

void rtl8922ae_scan_reset(void)
{
  memset(&scan, 0, sizeof(scan));
}

void rtl8922ae_scan_status(struct wifi_status_reply *status)
{
  char suffix[40];
  int bytes;
  if (scan.offload_acked) {
    bytes = sprintf(suffix, "; OFLD_ACK raw=%02x seq=%u",
        (unsigned)scan.offload_return, (unsigned)scan.offload_sequence);
  } else {
    bytes = sprintf(suffix, "; OFLD_ACK unobserved");
  }
  size_t used = strnlen(status->detail, sizeof(status->detail));
  /* Preserve the primary cause if its text leaves no room for the observation. */
  if (used + (size_t)bytes < sizeof(status->detail)) {
    memcpy(status->detail + used, suffix, (size_t)bytes + 1);
  }
}

uint8_t rtl8922ae_scan_channel(void)
{
  return scan.channel;
}

bool rtl8922ae_scan_reusable(void)
{
  return scan.ready && !scan.running && !scan.uncertain && !scan.fault;
}

bool rtl8922ae_scan_receive(const void *data, size_t bytes)
{
  if (bytes < C2H_HEADER_BYTES || scan.fault) {
    scan.uncertain = true;
    scan.fault = true;
    return false;
  }
  const uint8_t *packet = data;
  uint32_t header = get32(packet);
  unsigned category = header & 3u, class_id = header >> 2 & 63u;
  unsigned function = header >> 8 & 255u;
  if (category != H2C_CATEGORY_MAC) return true;
  if (class_id == C2H_CLASS_INFO && function == C2H_DONE_ACK) {
    if (bytes < C2H_ACK_BYTES) {
      scan.fault = true;
      scan.uncertain = true;
      return false;
    }
    uint32_t ack = get32(packet + C2H_HEADER_BYTES);
    unsigned ack_category = ack & 3u, ack_class = ack >> 2 & 63u;
    unsigned ack_function = ack >> 8 & 255u;
    if (ack_category == H2C_CATEGORY_MAC && ack_class == H2C_CLASS_OFFLOAD &&
        ack_function == H2C_OFLD_CFG) {
      scan.offload_return = ack >> 16;
      scan.offload_sequence = ack >> 24;
      scan.offload_acked = true;
    }
    if (ack_category != H2C_CATEGORY_MAC || ack_class != H2C_CLASS_OFFLOAD ||
        !scan.waiting || ack_function != scan.wait_function) return true;
    scan.ack_return = ack >> 16;
    scan.ack_sequence = ack >> 24;
    scan.ack_ok = !(scan.ack_return & C2H_SCAN_RETURN_MASK);
    if (!scan.ack_ok) scan.uncertain = true;
    scan.acked = true;
  } else if (class_id == C2H_CLASS_OFFLOAD && function == C2H_SCAN_REPORT) {
    if (bytes < C2H_SCAN_BYTES) {
      scan.fault = true;
      scan.uncertain = true;
      return false;
    }
    uint32_t report = get32(packet + C2H_HEADER_BYTES);
    unsigned reason = report >> 16 & 15u, status = report >> 20 & 15u;
    if (scan.running && reason == SCAN_ENTER_CHANNEL_NOTIFY) scan.channel = report & 255u;
    if (scan.running && reason == SCAN_END_NOTIFY) {
      scan.end_ok = status == SCAN_STATUS_SUCCESS;
      if (!scan.end_ok) scan.uncertain = true;
      scan.ended = true;
    }
  }
  return true;
}

static bool command(uint8_t class_id, uint8_t function, bool done_ack,
    const void *payload, size_t bytes, uint64_t deadline, const char **detail)
{
  bool submitted = rtl8922ae_transport_h2c(H2C_CATEGORY_MAC, class_id, function,
      false, done_ack, payload, bytes, NULL, deadline, detail);
  if (!submitted) scan.uncertain = true;
  return submitted;
}

bool rtl8922ae_scan_refresh_cam(uint64_t deadline, const char **detail)
{
  if (!rtl8922ae_scan_reusable()) {
    return fail(detail, "RTL8922AE scan CAM refresh unavailable");
  }
  return command(H2C_CLASS_ADDRESS_CAM, H2C_CAM_UPDATE, true,
      scan.cam, sizeof(scan.cam), deadline, detail);
}

static bool acknowledged_command(uint8_t function, const void *payload, size_t bytes,
    uint64_t deadline, const char **detail)
{
  scan.wait_function = function;
  scan.acked = false;
  scan.ack_return = 0;
  scan.ack_sequence = 0;
  scan.waiting = true;
  bool submitted = rtl8922ae_transport_h2c(H2C_CATEGORY_MAC, H2C_CLASS_OFFLOAD,
      function, true, true, payload, bytes, NULL, deadline, detail);
  /* Like the reference, install the condition before TX, then give the
   * completion its 100 ms window after TX returns. Native TX also retires
   * its borrowed DMA slot; that must not consume the completion window. */
  uint64_t local = rtl8922ae_deadline(deadline, FIRMWARE_WAIT_NS);
  bool transport_ok = submitted;
  if (submitted) {
    do {
      if (!rtl8922ae_transport_poll() || scan.fault) {
        transport_ok = false;
        break;
      }
      if (scan.acked) break;
    } while (rtl8922ae_sleep(SCAN_POLL_NS, local));
  }
  scan.waiting = false;
  if (!transport_ok || !scan.acked || !scan.ack_ok) {
    scan.uncertain = true;
    if (!submitted) return false;
    const char *step = function == H2C_ADD_SCAN_CHANNEL ? "ADD" : "START";
    if (scan.fault) {
      sprintf(ack_detail, "RTL scan %s malformed C2H while awaiting DONE_ACK", step);
    } else if (!transport_ok) {
      sprintf(ack_detail, "RTL scan %s transport failed while awaiting DONE_ACK", step);
    } else if (scan.acked) {
      sprintf(ack_detail, "RTL scan %s DONE_ACK rejected raw=%02x code=%02x seq=%u",
          step, (unsigned)scan.ack_return,
          (unsigned)(scan.ack_return & C2H_SCAN_RETURN_MASK), (unsigned)scan.ack_sequence);
    } else {
      sprintf(ack_detail, "RTL scan %s DONE_ACK %s", step,
          rtl8922ae_operation_continue() ? "timeout" : "interrupted");
    }
    return fail(detail, ack_detail);
  }
  return true;
}

static bool default_control_tables(uint64_t deadline, const char **detail)
{
  /* G7 CMAC and V2 DMAC each have c0, 16 data words, then 16 mask words.
   * Unwritten/reserved words stay zero as in the reference's zeroed H2C. */
  uint8_t cmac[CONTROL_COMMAND_BYTES] = {0};
  put32(cmac, CONTROL_UPDATE);
  put32(cmac + 4, CMAC_DATA_RATE);
  put32(cmac + 8, CMAC_DATA_RATE | CMAC_RTS_RATE << 16 | CMAC_DATA_RATE << 28);
  put32(cmac + 4 * 5, CMAC_ACTIVE_SUBCHANNELS);
  put32(cmac + 4 * 6, CMAC_NOMINAL_PADDING);
  put32(cmac + 4 * 7, CMAC_RESPONSE_RATE << 12);
  put32(cmac + 4 * 8, CMAC_CSI_NC | CMAC_CSI_NR | CMAC_CSI_CB |
      CMAC_CSI_PARAMETER_ENABLE | CMAC_CSI_RATE);
  const uint32_t cmac_masks[CONTROL_DATA_WORDS] = {
    CMAC_W0_MASK, ALL_WORD_BITS, ALL_WORD_BITS, ALL_WORD_BITS,
    CMAC_W4_MASK, CMAC_W5_MASK, CMAC_W6_MASK, CMAC_W7_MASK, CMAC_W8_MASK,
    0, 0, 0, 0, 0, ALL_WORD_BITS, CMAC_W15_MASK,
  };
  for (unsigned i = 0; i < CONTROL_DATA_WORDS; ++i) {
    put32(cmac + 4 * (1 + CONTROL_DATA_WORDS + i), cmac_masks[i]);
  }
  if (!command(H2C_CLASS_FRAME_EXCHANGE, H2C_DEFAULT_CMAC, true,
      cmac, sizeof(cmac), deadline, detail)) return false;

  uint8_t dmac[CONTROL_COMMAND_BYTES] = {0};
  put32(dmac, CONTROL_UPDATE);
  const uint32_t dmac_masks[CONTROL_DATA_WORDS] = {
    ALL_WORD_BITS, ALL_WORD_BITS, ALL_WORD_BITS, ALL_WORD_BITS,
    DMAC_W4_MASK, DMAC_W5_MASK, ALL_WORD_BITS, ALL_WORD_BITS,
    ALL_WORD_BITS, ALL_WORD_BITS, ALL_WORD_BITS, ALL_WORD_BITS,
    DMAC_W12_MASK, 0, 0, 0,
  };
  for (unsigned i = 0; i < CONTROL_DATA_WORDS; ++i) {
    put32(dmac + 4 * (1 + CONTROL_DATA_WORDS + i), dmac_masks[i]);
  }
  return command(H2C_CLASS_FRAME_EXCHANGE, H2C_DEFAULT_DMAC, false,
      dmac, sizeof(dmac), deadline, detail);
}

bool rtl8922ae_scan_setup(const uint8_t mac[6], uint64_t deadline, const char **detail)
{
  if (scan.ready || scan.uncertain || scan.fault) {
    return fail(detail, "RTL8922AE scan role setup unavailable");
  }
  if (!rtl8922ae_transport_poll()) {
    scan.uncertain = true;
    return fail(detail, "RTL8922AE scan role setup transport unavailable");
  }
  uint8_t unpause[MACID_PAUSE_SLEEP_BYTES] = {0};
  put32(unpause + MACID_PAUSE_MASK_OFFSET, MACID_ZERO_MASK);
  put32(unpause + MACID_SLEEP_MASK_OFFSET, MACID_ZERO_MASK);
  if (!rtl8922ae_transport_h2c(H2C_CATEGORY_MAC, H2C_CLASS_OFFLOAD,
      H2C_MACID_PAUSE_SLEEP, true, false, unpause, sizeof(unpause), NULL,
      deadline, detail)) {
    scan.uncertain = true;
    return false;
  }

  uint8_t role[4] = {0};
  /* CREATE, client self-role, NOLINK, port0/MACID0/CMAC0; no station peer. */
  put32(role, WIFI_ROLE_STATION << ROLE_WIFI_SHIFT);
  if (!command(H2C_CLASS_MEDIA, H2C_ROLE_MAINTAIN, true,
      role, sizeof(role), deadline, detail)) return false;

  uint8_t join[12] = {0};
  put32(join, JOIN_DISCONNECT | WIFI_ROLE_STATION << JOIN_WIFI_SHIFT);
  put32(join + 4, JOIN_MLO_MLSR | JOIN_EMLSR_PADDING_256US << JOIN_EMLSR_PADDING_SHIFT |
      JOIN_EMLSR_TRANSITION_256US << JOIN_EMLSR_TRANSITION_SHIFT);
  if (!command(H2C_CLASS_MEDIA, H2C_JOIN_INFO, true,
      join, sizeof(join), deadline, detail)) return false;

  uint8_t *cam = scan.cam;
  /* First free address/BSSID entries are index0. BSSID/TMA and all security
   * entries stay zero for the unassociated role; SMA is the board address. */
  uint8_t hash = 0;
  for (unsigned i = 0; i < 6; ++i) hash ^= mac[i];
  put32(cam + 4, CAM_ADDRESS_BYTES << 16);
  put32(cam + 8, CAM_VALID | (uint32_t)hash << 16);
  memcpy(cam + 16, mac, 6);
  put32(cam + 36, CAM_SECURITY_NORMAL);
  put32(cam + 48, CAM_BSSID_BYTES << 16);
  put32(cam + 52, CAM_VALID | CAM_BSSID_MATCH_ALL);
  if (!command(H2C_CLASS_ADDRESS_CAM, H2C_CAM_UPDATE, true,
      cam, sizeof(scan.cam), deadline, detail)) return false;
  if (!default_control_tables(deadline, detail)) return false;
  scan.ready = true;
  return true;
}

static void scan_configuration(uint8_t payload[SCAN_CONFIG_BYTES], unsigned operation)
{
  /* Standalone, no repeat, no operating-channel or MACC roles, no SSIDs or
   * probe templates. Current BE firmware includes the explicit w9 sizes. */
  memset(payload, 0, SCAN_CONFIG_BYTES);
  put32(payload, operation | SCAN_NOTIFY_END | SCAN_LEARN_CHANNEL);
  put32(payload + 8, SCAN_CHANNEL_INVALID << 24);
  put32(payload + 12, SCAN_PACKET_NONE << 24);
  put32(payload + 16, SCAN_PACKET_NONE | SCAN_PACKET_NONE << 8);
  /* Match the coherent DBCC PHY/RFK topology. */
  put32(payload + 20, SCAN_MLO_1_PLUS_1_1RF);
  /* Match the observed device's 59-channel bitmap, prohibiting every valid
   * 6 GHz channel without setting its five unused upper bits. */
  put32(payload + 24, (uint32_t)SCAN_ALL_6GHZ_DISABLED);
  put32(payload + 28, (uint32_t)(SCAN_ALL_6GHZ_DISABLED >> 32));
  put32(payload + 36, SCAN_CONFIG_BYTES / 4 | SCAN_MACC_ROLE_WORDS << 8 |
      SCAN_OP_CHANNEL_WORDS << 16);
}

static bool wait_end(uint64_t deadline, const char **detail)
{
  do {
    if (!rtl8922ae_transport_poll() || scan.fault) {
      scan.uncertain = true;
      return fail(detail, "RTL8922AE scan completion transport failed");
    }
    if (scan.ended) {
      scan.running = false;
      return scan.end_ok || fail(detail, "RTL8922AE scan END reported failure");
    }
  } while (rtl8922ae_sleep(SCAN_POLL_NS, deadline));
  return fail(detail, "RTL8922AE scan END failed/timed out");
}

bool rtl8922ae_scan_run(uint8_t channel, uint64_t deadline, const char **detail)
{
  if (!rtl8922ae_scan_reusable()) {
    return fail(detail, "RTL8922AE scan context unavailable");
  }
  if (!rtl8922ae_transport_poll()) {
    scan.uncertain = true;
    return fail(detail, "RTL8922AE scan context transport unavailable");
  }
  uint8_t list[SCAN_LIST_BYTES] = {0};
  /* Linux CHAN_ACTIVE with zero SSIDs: 20 MHz, no probe, null or random SN.
   * Current BE channel format carries the period in w7 rather than w0. */
  list[0] = 1;
  list[1] = SCAN_CHANNEL_BYTES / 4;
  put32(list + 4, (uint32_t)channel << 16 | (uint32_t)channel << 24);
  put32(list + 8, SCAN_PAUSE_DATA | SCAN_NOTIFY_MASK << SCAN_NOTIFY_SHIFT);
  put32(list + 16, ALL_WORD_BITS);
  put32(list + 20, ALL_WORD_BITS);
  put32(list + 32, SCAN_CHANNEL_TIME_MS);
  if (!acknowledged_command(H2C_ADD_SCAN_CHANNEL, list, sizeof(list),
      deadline, detail)) return false;

  uint8_t payload[SCAN_CONFIG_BYTES];
  scan_configuration(payload, SCAN_START);
  scan.running = true;
  scan.ended = false;
  if (!acknowledged_command(H2C_SCAN_BE, payload, sizeof(payload), deadline, detail)) {
    return false;
  }
  return wait_end(deadline, detail);
}

bool rtl8922ae_scan_stop(uint64_t deadline, const char **detail)
{
  if (!scan.running) return true;
  if (scan.fault || !rtl8922ae_transport_poll()) {
    scan.uncertain = true;
    return fail(detail, "RTL8922AE scan STOP transport unavailable");
  }
  if (scan.ended) return wait_end(deadline, detail);
  uint8_t payload[SCAN_CONFIG_BYTES];
  scan_configuration(payload, SCAN_STOP);
  /* STOP's DONE_ACK is not its completion. Linux waits for END_SCAN instead. */
  if (!rtl8922ae_transport_h2c(H2C_CATEGORY_MAC, H2C_CLASS_OFFLOAD, H2C_SCAN_BE,
      true, true, payload, sizeof(payload), NULL, deadline, detail)) {
    scan.uncertain = true;
    return false;
  }
  bool ended = wait_end(rtl8922ae_deadline(deadline, FIRMWARE_WAIT_NS), detail);
  if (!ended) scan.uncertain = true;
  return ended;
}
