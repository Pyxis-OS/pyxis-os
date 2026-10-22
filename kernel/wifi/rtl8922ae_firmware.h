/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Format reference: Linux rtw89, BSD alternative; see kernel/wifi/NOTICE.
 */
#ifndef RTL8922AE_FIRMWARE_H
#define RTL8922AE_FIRMWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rtl8922ae_firmware_info {
  size_t bytes;
  size_t normal_suits;
  size_t wowlan_suits;
  size_t log_suits;
  size_t bbmcu_images;
  size_t sections;
  size_t security_sections;
  size_t formatted_mss_pools;
  size_t elements;
  uint8_t container_version[4];
  uint8_t mfw_entries;
};

/* The caller admits the pinned asset; this only checks its container spans.
 * Input remains immutable during this call and must not overlap info. The
 * result contains no borrowed pointers or device suitability/authorization.
 * Failure clears info; a null info pointer is rejected.
 */
bool rtl8922ae_firmware_inspect(const void *data, size_t bytes,
    struct rtl8922ae_firmware_info *info);

/* The section-count field is eight bits; v1 has the larger fixed header. */
#define RTL8922AE_FIRMWARE_MAX_SECTIONS 255u
#define RTL8922AE_FIRMWARE_MAX_HEADER_BYTES (48u + 255u * 16u)
#define RTL8922AE_FIRMWARE_CHUNK_BYTES 2020u

enum rtl8922ae_firmware_plan_result {
  RTL8922AE_FIRMWARE_PLAN_OK,
  RTL8922AE_FIRMWARE_PLAN_INVALID,
  RTL8922AE_FIRMWARE_PLAN_SECURITY_INVALID,
  RTL8922AE_FIRMWARE_PLAN_NO_IMAGE,
  RTL8922AE_FIRMWARE_PLAN_UNSUPPORTED,
  RTL8922AE_FIRMWARE_PLAN_NO_SECURITY,
  RTL8922AE_FIRMWARE_PLAN_BLACKLISTED,
};

struct rtl8922ae_firmware_security {
  uint32_t selector_magic;
  uint8_t device_type;
  uint8_t customer;
  uint8_t key_number;
  bool secure_boot;
};

struct rtl8922ae_firmware_section {
  size_t offset;
  size_t length;
  size_t key_offset;
  size_t key_length;
  uint32_t destination;
  uint8_t type;
};

struct rtl8922ae_firmware_plan {
  struct rtl8922ae_firmware_security security;
  size_t image_offset;
  size_t image_bytes;
  size_t header_bytes;
  size_t section_count;
  uint8_t cut;
  uint8_t header_version;
  uint8_t idmem_share_mode;
  uint8_t version[4];
  uint8_t header[RTL8922AE_FIRMWARE_MAX_HEADER_BYTES];
  struct rtl8922ae_firmware_section sections[RTL8922AE_FIRMWARE_MAX_SECTIONS];
};

/* security_bytes are four successfully read physical eFuse bytes at 0x1580.
 * A read failure must never reach this decoder as fabricated/default bytes.
 * Only selector 0xffff is nonsecure; malformed programmed selectors fail.
 */
bool rtl8922ae_firmware_decode_security(const uint8_t security_bytes[4],
    struct rtl8922ae_firmware_security *security);

/* Separate from inspection: selects normal type 1, mp 0, greatest cut <= live.
 * Caller admits the pinned asset and provides nonoverlapping static/BSP-owned
 * plan storage (too large for the kernel stack). No heap/device access occurs.
 * Failure clears plan. The compact owned header excludes the dynamic header;
 * offsets refer to the original immutable asset, retained through download.
 */
enum rtl8922ae_firmware_plan_result rtl8922ae_firmware_plan(const void *data,
    size_t bytes, uint8_t live_cut, const uint8_t security_bytes[4],
    struct rtl8922ae_firmware_plan *plan);

/* Copies one bounded transfer from a successful, unchanged plan and asset.
 * Selected key bytes replace the security section's tail in the copy only.
 * Destination must not overlap the asset or plan; failure writes no bytes.
 */
bool rtl8922ae_firmware_copy_section(const void *data, size_t bytes,
    const struct rtl8922ae_firmware_plan *plan, size_t index, size_t offset,
    void *destination, size_t length);

#endif
