/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Copyright(c) 2019-2020, 2021-2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89/acpi.c, acpi.h and phy.c at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, selected BSD alternative.
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
#ifndef KERNEL_ACPI_RTL_WIFI_H
#define KERNEL_ACPI_RTL_WIFI_H
#include <kernel/acpi/wifi.h>

#define ACPI_RTL_WIFI_BYTES 512
#define ACPI_RTL_WIFI_CID_HP 0x5048u
#define ACPI_RTL_WIFI_CID_RT 0x5452u

struct acpi_rtl_wifi_data {
  enum acpi_wifi_state state;
  uint16_t length;
  /* Exact flattened bytes, including headers; no normalized power or fallback. */
  uint8_t bytes[ACPI_RTL_WIFI_BYTES];
};

struct acpi_rtl_wifi_sar {
  struct acpi_rtl_wifi_data data;
  uint16_t cid;
  uint8_t revision, antennas, subbands, tables;
  uint8_t table_bytes, header_bytes;
};

enum acpi_rtl_wifi_tas_stage {
  ACPI_RTL_WIFI_TAS_STAGE_NONE,
  ACPI_RTL_WIFI_TAS_STAGE_QUERY_0,
  ACPI_RTL_WIFI_TAS_STAGE_FUNCTION_5,
  ACPI_RTL_WIFI_TAS_STAGE_ALIAS,
};

enum acpi_rtl_wifi_tas_reason {
  ACPI_RTL_WIFI_TAS_REASON_NONE,
  ACPI_RTL_WIFI_TAS_REASON_LOOKUP,
  ACPI_RTL_WIFI_TAS_REASON_EVALUATION,
  ACPI_RTL_WIFI_TAS_REASON_ALLOCATION,
  ACPI_RTL_WIFI_TAS_REASON_TYPE,
  ACPI_RTL_WIFI_TAS_REASON_LENGTH,
  ACPI_RTL_WIFI_TAS_REASON_SUPPORT_MASK,
  ACPI_RTL_WIFI_TAS_REASON_SIGNATURE,
  ACPI_RTL_WIFI_TAS_REASON_REVISION,
  ACPI_RTL_WIFI_TAS_REASON_ENABLE,
  ACPI_RTL_WIFI_TAS_REASON_ALIAS,
};

enum acpi_rtl_wifi_tas_object_type {
  ACPI_RTL_WIFI_TAS_OBJECT_UNOBSERVED,
  ACPI_RTL_WIFI_TAS_OBJECT_INTEGER,
  ACPI_RTL_WIFI_TAS_OBJECT_BUFFER,
  ACPI_RTL_WIFI_TAS_OBJECT_PACKAGE,
  ACPI_RTL_WIFI_TAS_OBJECT_STRING,
  ACPI_RTL_WIFI_TAS_OBJECT_OTHER,
};

struct acpi_rtl_wifi_tas_diagnostic {
  /* First rejection, or the final evaluated stage with reason NONE. */
  enum acpi_rtl_wifi_tas_stage stage;
  enum acpi_rtl_wifi_tas_reason reason;
  /* Observed DSM result; an alias-only rejection retains the first result. */
  enum acpi_rtl_wifi_tas_object_type object_type;
  uint64_t length;
  /* One alias's bounded function-0 mask, not the aggregate union below. */
  uint64_t supported;
  bool object_observed, length_observed, support_observed;
  /* Sticky; preserves the first concrete rejection when aliases disagree. */
  bool alias_conflict;
};

struct acpi_rtl_wifi_policy {
  struct pci_address address;
  struct acpi_rtl_wifi_sar wrds, rwrd;
  struct acpi_rtl_wifi_data rwsi, rwgs, rtag, dsm5;
  /* Union of bounded revision-0 Realtek function-0 support queries. */
  uint64_t dsm_supported;
  /* Descriptive only: no TAS applicability, normalization or RF permission. */
  struct acpi_rtl_wifi_tas_diagnostic tas;
  /* RWGS interpretation follows validated WRDS, otherwise validated RWRD.
   * HP: 3 regions, max plus four signed half-dB deltas per band.
   * RT: 6 regions, max plus one signed half-dB delta per band. */
  uint16_t geo_cid;
  uint8_t geo_revision, geo_regions, geo_bands;
};

/* BSP, IF=0. Completed owned immutable RTL8922AE capture; PENDING leaves POLICY
 * untouched. VALID reports completed discovery only. Each source must be
 * checked independently. No AML, power normalization, live RWSI refresh or TX
 * permission is provided. RTAG retains its tag, revision, LE domain and signed
 * quarter-dB gains. DSM5 retains its RTK/0x05 signature and format revision. */
enum acpi_wifi_state acpi_rtl_wifi_snapshot(struct acpi_rtl_wifi_policy *policy);
#endif
