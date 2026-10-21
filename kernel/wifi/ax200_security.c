/* CCMP command/RX integration follows the pinned OpenBSD iwx reference:
 * if_iwx.c iwx_add_sta_key_cmd, iwx_delete_key, iwx_ccmp_decap and iwx_tx.
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 * BSD-3-Clause alternative selected; full retained notice in kernel/wifi/NOTICE.
 */
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/wifi/ax200_transport.h>
#include "ax200_firmware.h"
#include "ax200_security.h"
#include "ax200_station.h"

#define AX200_CCMP_EXT_IV UINT8_C(0x20)
#define AX200_CCMP_KEY_ID_SHIFT 6u
#define AX200_CCMP_MAC_BYTES 24u
#define AX200_CCMP_DATA UINT8_C(0x08)
#define AX200_CCMP_FROM_DS UINT8_C(2)
#define AX200_CCMP_MORE_FRAG UINT8_C(4)

enum key_api { KEY_API_UNSUPPORTED, KEY_API_STA_V2, KEY_API_SEC_V1 };

static void require_worker(void)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
}

static struct ax200_ccmp_key *select_key(struct ax200_security *security, bool group)
{
  return group ? &security->group : &security->pairwise;
}

static enum key_api key_api(void)
{
  int newer = ax200_command_version(AX200_SEC_KEY_GROUP, AX200_SEC_KEY_OPCODE, false);
  if (newer == 1) {
    return KEY_API_SEC_V1;
  }
  if (newer != -1 && newer != 0) {
    return KEY_API_UNSUPPORTED;
  }
  int version = ax200_command_version(AX200_LONG, AX200_ADD_STA_KEY, false);
  /* The legacy command has no version-table entry on some firmware. Its API
   * bit is authoritative there; an advertised different layout is refused. */
  return ax200_firmware_api(AX200_API_STA_KEY_V2) &&
      (version == -1 || version == 2) ? KEY_API_STA_V2 : KEY_API_UNSUPPORTED;
}

bool ax200_security_supported(void)
{
  require_worker();
  return key_api() != KEY_API_UNSUPPORTED;
}

static enum call_status key_command(bool remove, bool group, unsigned key_id,
    const uint8_t key[AX200_CCMP_KEY_BYTES], uint64_t receive_pn, uint64_t deadline)
{
  union {
    struct ax200_sta_key_command_v2 station;
    struct ax200_sec_key_command_v1 security;
  } command = {0};
  uint32_t reply = 0;
  size_t received = 0;
  enum call_status result = CALL_BAD_OPERATION;
  enum key_api api = key_api();
  if (api == KEY_API_SEC_V1) {
    command.security = (struct ax200_sec_key_command_v1){
      .action = remove ? AX200_SEC_KEY_REMOVE : AX200_SEC_KEY_ADD,
      .station_mask = UINT32_C(1) << AX200_KEY_STATION,
      .key_id = key_id,
      .flags = AX200_SEC_KEY_CCMP | (group ? AX200_SEC_KEY_GROUP_FLAG : 0),
      .receive_sequence = receive_pn,
    };
    memcpy(command.security.key, key, AX200_CCMP_KEY_BYTES);
    /* Pinned iwx_mld_set_sta_key_cmd uses transport completion, not a
     * fabricated ADD_STA status payload, for SEC_KEY v1. */
    result = ax200_radio_command(AX200_SEC_KEY_GROUP, AX200_SEC_KEY_OPCODE,
        &command.security, sizeof(command.security), NULL, 0, NULL, deadline) ?
        CALL_OK : CALL_OUTCOME_UNKNOWN;
  } else if (api == KEY_API_STA_V2) {
    command.station = (struct ax200_sta_key_command_v2){
      .station = AX200_KEY_STATION,
      .slot = group ? AX200_KEY_GROUP_SLOT : AX200_KEY_PAIRWISE_SLOT,
      .flags = (remove ? AX200_KEY_NOT_VALID : AX200_KEY_CCMP) | AX200_KEY_MAP |
          (key_id << AX200_KEY_ID_SHIFT) | (!remove && group ? AX200_KEY_GROUP : 0),
    };
    memcpy(command.station.key, key, AX200_CCMP_KEY_BYTES);
    bool completed = ax200_radio_command(AX200_LONG, AX200_ADD_STA_KEY, &command.station,
        sizeof(command.station), &reply, sizeof(reply), &received, deadline);
    result = !completed || received != sizeof(reply) ? CALL_OUTCOME_UNKNOWN :
        (reply & AX200_KEY_STATUS_MASK) == AX200_KEY_SUCCESS ? CALL_OK : CALL_IO;
  }
  memzero_explicit(&command, sizeof(command));
  memzero_explicit(&reply, sizeof(reply));
  return result;
}

enum call_status ax200_security_install(struct ax200_security *security, bool group,
    unsigned key_id, const uint8_t bytes[AX200_CCMP_KEY_BYTES], uint64_t receive_pn,
    uint64_t deadline_ns)
{
  require_worker();
  if (!security || !bytes || key_id > 3 || (!group && key_id) || receive_pn >= AX200_CCMP_PN_MAX) {
    return CALL_BAD_REQUEST;
  }
  if (!ax200_security_supported()) {
    return CALL_BAD_OPERATION;
  }
  struct ax200_ccmp_key *key = select_key(security, group);
  if (key->installed && !memcmp(key->bytes, bytes, AX200_CCMP_KEY_BYTES)) {
    /* EAPOL retransmission must never reinstall a key/reset firmware TX PN. */
    return key->id == key_id ? CALL_OK : CALL_DENIED;
  }
  if (ax200_transport_tx_pending()) {
    return CALL_BUSY;
  }
  enum call_status result = key_command(false, group, key_id, bytes, receive_pn, deadline_ns);
  if (result != CALL_OK) {
    return result;
  }
  if (key->installed && group && key->id != key_id && key_api() == KEY_API_SEC_V1) {
    /* SEC_KEY addresses GTKs by ID. Keep the old slot until the new one has
     * acknowledged installation, then retire it before reopening the port. */
    result = key_command(true, true, key->id, key->bytes, 0, deadline_ns);
    if (result != CALL_OK) {
      /* The old tracked key remains for retirement. Roll back the acknowledged
       * new slot; if that cannot be confirmed, quarantine instead of losing
       * track of an installed firmware key. */
      enum call_status rollback = key_command(true, true, key_id, bytes, 0, deadline_ns);
      return rollback == CALL_OK ? result : CALL_OUTCOME_UNKNOWN;
    }
  }
  memzero_explicit(key, sizeof(*key));
  key->installed = true;
  key->id = key_id;
  memcpy(key->bytes, bytes, AX200_CCMP_KEY_BYTES);
  for (unsigned tid = 0; tid < AX200_CCMP_TIDS; ++tid) {
    key->receive_pn[tid] = receive_pn;
  }
  return CALL_OK;
}

enum call_status ax200_security_remove(struct ax200_security *security, bool group,
    uint64_t deadline_ns)
{
  require_worker();
  if (!security) {
    return CALL_BAD_REQUEST;
  }
  struct ax200_ccmp_key *key = select_key(security, group);
  if (!key->installed) {
    return CALL_OK;
  }
  if (!ax200_security_supported()) {
    return CALL_BAD_OPERATION;
  }
  if (ax200_transport_tx_pending()) {
    return CALL_BUSY;
  }
  enum call_status result = key_command(true, group, key->id, key->bytes, 0, deadline_ns);
  if (result == CALL_OK) {
    memzero_explicit(key, sizeof(*key));
  }
  return result;
}

void ax200_security_clear(struct ax200_security *security)
{
  require_worker();
  memzero_explicit(security, sizeof(*security));
}

enum ax200_security_rx_result ax200_security_receive(const struct ax200_security *security,
    const uint8_t *frame, size_t bytes, uint32_t status, bool padded, uint8_t mac_flags1,
    struct ax200_security_rx *view)
{
  require_worker();
  if (!security || !frame || !view || bytes < AX200_CCMP_MAC_BYTES + AX200_CCMP_IV_BYTES ||
      frame[0] != AX200_CCMP_DATA || (frame[1] & 3) != AX200_CCMP_FROM_DS ||
      !(frame[1] & AX200_FRAME_PROTECTED) || (frame[1] & AX200_CCMP_MORE_FRAG) || (frame[22] & 15) ||
      (status & (AX200_RX_CRC_OK | AX200_RX_OVERRUN_OK)) != (AX200_RX_CRC_OK | AX200_RX_OVERRUN_OK) ||
      (status & AX200_RX_RFKILL)) {
    return AX200_SECURITY_RX_INVALID;
  }
  const uint8_t *iv = frame + AX200_CCMP_MAC_BYTES;
  if (iv[2] || (iv[3] & 0x3f) != AX200_CCMP_EXT_IV) {
    return AX200_SECURITY_RX_INVALID;
  }
  bool group = frame[4] & 1;
  unsigned key_id = iv[3] >> AX200_CCMP_KEY_ID_SHIFT;
  const struct ax200_ccmp_key *key = group ? &security->group : &security->pairwise;
  if (!key->installed || key->id != key_id) {
    return AX200_SECURITY_RX_NO_KEY;
  }
  if ((status & AX200_RX_SECURITY_MASK) != AX200_RX_SECURITY_CCMP ||
      (status & (AX200_RX_DECRYPT_DONE | AX200_RX_MIC_OK)) != (AX200_RX_DECRYPT_DONE | AX200_RX_MIC_OK)) {
    return AX200_SECURITY_RX_INTEGRITY;
  }
  size_t payload = AX200_CCMP_MAC_BYTES + AX200_CCMP_IV_BYTES + (padded ? 2 : 0);
  /* Firmware reports the retained MIC/CRC extent; pinned iwx documents that
   * some firmware strips the MIC and some leaves it. Never assume eight. */
  size_t trailer = (mac_flags1 & AX200_RX_MIC_CRC_MASK) >> AX200_RX_MIC_CRC_SHIFT;
  if (payload > bytes || trailer > bytes - payload) {
    return AX200_SECURITY_RX_INVALID;
  }
  uint64_t pn = (uint64_t)iv[0] | (uint64_t)iv[1] << 8 | (uint64_t)iv[4] << 16 |
      (uint64_t)iv[5] << 24 | (uint64_t)iv[6] << 32 | (uint64_t)iv[7] << 40;
  if (pn <= key->receive_pn[0] || pn == AX200_CCMP_PN_MAX) {
    return AX200_SECURITY_RX_REPLAY;
  }
  *view = (struct ax200_security_rx){.payload_offset = payload, .payload_bytes = bytes - payload - trailer,
      .pn = pn, .key_id = key_id, .group = group};
  return AX200_SECURITY_RX_OK;
}

bool ax200_security_accept(struct ax200_security *security, const struct ax200_security_rx *view)
{
  require_worker();
  if (!security || !view || view->tid >= AX200_CCMP_TIDS || !view->pn || view->pn >= AX200_CCMP_PN_MAX) {
    return false;
  }
  struct ax200_ccmp_key *key = select_key(security, view->group);
  if (!key->installed || key->id != view->key_id || view->pn <= key->receive_pn[view->tid]) {
    return false;
  }
  key->receive_pn[view->tid] = view->pn;
  return true;
}

bool ax200_security_transmit(struct ax200_security *security, const uint8_t *frame,
    size_t bytes, bool explicit_rate, uint32_t rate)
{
  require_worker();
  if (!security || !security->pairwise.installed || security->pairwise.transmitted >= AX200_CCMP_PN_MAX) {
    return false;
  }
  if (!ax200_transport_transmit_ccmp(frame, bytes, explicit_rate, rate)) {
    return false;
  }
  ++security->pairwise.transmitted;
  return true;
}
