/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright(c) 2019-2020, 2021-2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89/acpi.c, acpi.h and sar.c at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, selected BSD alternative.
 * Path applicability checked against rtw89/rtw8922a.c at the same revision.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "rtl8922ae_sar.h"

#define SAR_STATIC_HEADER_BYTES 3u
#define SAR_DYNAMIC_HEADER_BYTES 4u
#define SAR_MAX_PROFILES 6u
#define SAR_REVISION_LEGACY 1u
#define SAR_REVISION_6GHZ 2u
#define SAR_LEGACY_SUBBANDS 5u
#define SAR_6GHZ_SUBBANDS 11u
#define SAR_STANDARD_ANTENNAS 4u
#define SAR_SMALL_ANTENNAS 2u
#define SAR_2GHZ_SUBBAND 0u
#define SAR_HP_BIAS_EIGHTH_DBM 80
#define SAR_HP_STEP_EIGHTH_DBM 4
#define SAR_HP_2TX_REDUCTION_EIGHTH_DBM 24

static enum acpi_wifi_state checked_state(enum acpi_wifi_state state)
{
  switch (state) {
  case ACPI_WIFI_PENDING:
  case ACPI_WIFI_ABSENT:
  case ACPI_WIFI_DISABLED:
  case ACPI_WIFI_VALID:
  case ACPI_WIFI_UNSUPPORTED:
  case ACPI_WIFI_MALFORMED:
  case ACPI_WIFI_FAILED:
    return state;
  }
  return ACPI_WIFI_FAILED;
}

static enum acpi_wifi_state validate_source(const struct acpi_rtl_wifi_sar *sar,
    bool dynamic, struct rtl8922ae_sar_source *source)
{
  const struct acpi_rtl_wifi_data *data = &sar->data;
  if (data->state != ACPI_WIFI_VALID) {
    return checked_state(data->state);
  }

  unsigned header = dynamic ? SAR_DYNAMIC_HEADER_BYTES : SAR_STATIC_HEADER_BYTES;
  if (data->length > sizeof(data->bytes) || data->length <= header) {
    return ACPI_WIFI_MALFORMED;
  }
  uint16_t cid = data->bytes[0] | ((uint16_t)data->bytes[1] << 8);
  unsigned revision = data->bytes[2];
  unsigned profiles = dynamic ? data->bytes[3] : 1;
  if (!profiles || profiles > SAR_MAX_PROFILES) {
    return ACPI_WIFI_MALFORMED;
  }
  if ((cid != ACPI_RTL_WIFI_CID_HP && cid != ACPI_RTL_WIFI_CID_RT) ||
      (revision != SAR_REVISION_LEGACY && revision != SAR_REVISION_6GHZ)) {
    return ACPI_WIFI_UNSUPPORTED;
  }

  unsigned subbands = revision == SAR_REVISION_LEGACY ?
      SAR_LEGACY_SUBBANDS : SAR_6GHZ_SUBBANDS;
  unsigned bytes = data->length - header;
  if (bytes % profiles) {
    return ACPI_WIFI_MALFORMED;
  }
  unsigned table_bytes = bytes / profiles;
  unsigned antennas;
  if (table_bytes == SAR_STANDARD_ANTENNAS * subbands) {
    antennas = SAR_STANDARD_ANTENNAS;
  } else if (cid == ACPI_RTL_WIFI_CID_RT &&
      table_bytes == SAR_SMALL_ANTENNAS * subbands) {
    antennas = SAR_SMALL_ANTENNAS;
  } else {
    return ACPI_WIFI_MALFORMED;
  }

  /* Do not use a VALID label to trust stale or inconsistent span metadata. */
  if (sar->cid != cid || sar->revision != revision || sar->tables != profiles ||
      sar->subbands != subbands || sar->antennas != antennas ||
      sar->header_bytes != header || sar->table_bytes != table_bytes) {
    return ACPI_WIFI_MALFORMED;
  }
  *source = (struct rtl8922ae_sar_source){
    .cid = cid, .revision = revision, .antennas = antennas,
    .subbands = subbands, .profiles = profiles,
  };
  return ACPI_WIFI_VALID;
}

static void intersect_source(struct rtl8922ae_sar_envelope *envelope,
    const struct acpi_rtl_wifi_sar *sar, uint32_t flag)
{
  bool hp = sar->cid == ACPI_RTL_WIFI_CID_HP;
  for (unsigned profile = 0; profile < sar->tables; ++profile) {
    unsigned table = sar->header_bytes + profile * sar->table_bytes;
    for (unsigned path = 0; path < RTL8922AE_SAR_PATH_COUNT; ++path) {
      unsigned antenna = hp ? 1 - path : path;
      unsigned offset = table + antenna * sar->subbands + SAR_2GHZ_SUBBAND;
      int16_t one = sar->data.bytes[offset];
      if (hp) {
        one = SAR_HP_BIAS_EIGHTH_DBM + SAR_HP_STEP_EIGHTH_DBM * one;
      }
      int16_t two = one - (hp ? SAR_HP_2TX_REDUCTION_EIGHTH_DBM : 0);
      bool first = !envelope->source_flags && !profile;
      if (first || one < envelope->limits_1tx_eighth_dbm[path]) {
        envelope->limits_1tx_eighth_dbm[path] = one;
      }
      if (first || two < envelope->limits_2tx_eighth_dbm[path]) {
        envelope->limits_2tx_eighth_dbm[path] = two;
      }
    }
  }
  envelope->source_flags |= flag;
}

struct rtl8922ae_sar_envelope rtl8922ae_sar_envelope(
    const struct acpi_rtl_wifi_policy *policy, enum acpi_wifi_state capture_state)
{
  struct rtl8922ae_sar_envelope result = {0};
  result.state = checked_state(capture_state);
  if (result.state != ACPI_WIFI_VALID) {
    return result;
  }
  if (!policy) {
    result.state = ACPI_WIFI_FAILED;
    return result;
  }

  struct rtl8922ae_sar_source wrds = {0}, rwrd = {0};
  enum acpi_wifi_state state = validate_source(&policy->wrds, false, &wrds);
  if (state != ACPI_WIFI_VALID && state != ACPI_WIFI_ABSENT) {
    result.state = state;
    return result;
  }
  state = validate_source(&policy->rwrd, true, &rwrd);
  if (state != ACPI_WIFI_VALID && state != ACPI_WIFI_ABSENT) {
    result.state = state;
    return result;
  }
  state = checked_state(policy->rwgs.state);
  if (state != ACPI_WIFI_ABSENT) {
    result.state = state == ACPI_WIFI_VALID ? ACPI_WIFI_UNSUPPORTED : state;
    return result;
  }
  if (policy->wrds.data.state == ACPI_WIFI_VALID) {
    intersect_source(&result, &policy->wrds, RTL8922AE_SAR_SOURCE_WRDS);
    result.wrds = wrds;
  }
  if (policy->rwrd.data.state == ACPI_WIFI_VALID) {
    intersect_source(&result, &policy->rwrd, RTL8922AE_SAR_SOURCE_RWRD);
    result.rwrd = rwrd;
  }
  result.state = result.source_flags ? ACPI_WIFI_VALID : ACPI_WIFI_ABSENT;
  return result;
}
