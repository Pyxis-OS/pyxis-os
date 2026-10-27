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

#ifndef WIFI_RSN_PRIVATE_H
#define WIFI_RSN_PRIVATE_H

#include <abi/wifi.h>
#include <kernel/memory.h>
#include <stddef.h>

#define AX_RSN_ELEMENT_ID 48u
#define AX_RSN_MFPR (UINT16_C(1) << 6)
#define AX_RSN_MFPC (UINT16_C(1) << 7)
#define AX_RSN_SPP_REQUIRED (UINT16_C(1) << 11)

static inline uint16_t ax200_rsn_get16(const uint8_t *bytes)
{
  return (uint16_t)bytes[0] | (uint16_t)bytes[1] << 8;
}

/* The scan and station paths use the same bounded RSN v1 interpretation.
 * Malformed elements expose PRESENT alone, never a partial supported profile. */
static inline uint32_t wifi_rsn_security(const uint8_t *ie, size_t bytes)
{
  static const uint8_t ccmp[4] = {0, 0x0f, 0xac, 4};
  static const uint8_t psk[4] = {0, 0x0f, 0xac, 2};
  static const uint8_t sae[4] = {0, 0x0f, 0xac, 8};
  uint32_t flags = WIFI_BSS_RSN_PRESENT;
  if (bytes < 20 || bytes > WIFI_AUTH_RSN_BYTES || ie[0] != AX_RSN_ELEMENT_ID ||
      (size_t)ie[1] + 2 != bytes || ax200_rsn_get16(ie + 2) != 1) {
    return WIFI_BSS_RSN_PRESENT;
  }
  if (!memcmp(ie + 4, ccmp, 4)) {
    flags |= WIFI_BSS_RSN_GROUP_CCMP;
  }
  size_t offset = 8;
  unsigned count = ax200_rsn_get16(ie + offset);
  offset += 2;
  if (!count || count > (bytes - offset) / 4) {
    return WIFI_BSS_RSN_PRESENT;
  }
  for (unsigned i = 0; i < count; ++i, offset += 4) {
    if (!memcmp(ie + offset, ccmp, 4)) {
      flags |= WIFI_BSS_RSN_PAIRWISE_CCMP;
    }
  }
  if (bytes - offset < 2) {
    return WIFI_BSS_RSN_PRESENT;
  }
  count = ax200_rsn_get16(ie + offset);
  offset += 2;
  if (!count || count > (bytes - offset) / 4) {
    return WIFI_BSS_RSN_PRESENT;
  }
  for (unsigned i = 0; i < count; ++i, offset += 4) {
    if (!memcmp(ie + offset, psk, 4)) {
      flags |= WIFI_BSS_RSN_AKM_PSK;
    } else if (!memcmp(ie + offset, sae, 4)) {
      flags |= WIFI_BSS_RSN_AKM_SAE;
    }
  }
  if (offset == bytes) {
    return flags | WIFI_BSS_RSN_VALID;
  }
  if (bytes - offset < 2) {
    return WIFI_BSS_RSN_PRESENT;
  }
  uint16_t capabilities = ax200_rsn_get16(ie + offset);
  if (capabilities & AX_RSN_MFPR) {
    flags |= WIFI_BSS_RSN_MFPR;
  }
  if (capabilities & AX_RSN_MFPC) {
    flags |= WIFI_BSS_RSN_MFPC;
  }
  if (capabilities & AX_RSN_SPP_REQUIRED) {
    flags |= WIFI_BSS_RSN_SPP_REQUIRED;
  }
  offset += 2;
  if (offset == bytes) {
    return flags | WIFI_BSS_RSN_VALID;
  }
  if (bytes - offset < 2) {
    return WIFI_BSS_RSN_PRESENT;
  }
  count = ax200_rsn_get16(ie + offset);
  offset += 2;
  if (count > (bytes - offset) / 16) {
    return WIFI_BSS_RSN_PRESENT;
  }
  offset += count * 16;
  /* Optional management-group cipher is not negotiated with PMF disabled. */
  if (offset != bytes && bytes - offset != 4) {
    return WIFI_BSS_RSN_PRESENT;
  }
  return flags | WIFI_BSS_RSN_VALID;
}

#endif
