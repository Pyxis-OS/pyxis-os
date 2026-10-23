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

/* First eligible embedded BBMCU0 image, matching the pinned reference's element
 * selection. The same checked section/security rules apply as for WCPU. */
enum rtl8922ae_firmware_plan_result rtl8922ae_firmware_plan_bbmcu0(const void *data,
    size_t bytes, uint8_t live_cut, const uint8_t security_bytes[4],
    struct rtl8922ae_firmware_plan *plan);

/* Copies one bounded transfer from a successful, unchanged plan and asset.
 * Selected key bytes replace the security section's tail in the copy only.
 * Destination must not overlap the asset or plan; failure writes no bytes.
 */
bool rtl8922ae_firmware_copy_section(const void *data, size_t bytes,
    const struct rtl8922ae_firmware_plan *plan, size_t index, size_t offset,
    void *destination, size_t length);

/* Pinned rtw89 element identifiers; unknown identifiers remain opaque. */
enum rtl8922ae_firmware_element_id {
  RTL8922AE_ELEMENT_BBMCU0 = 0,
  RTL8922AE_ELEMENT_BBMCU1 = 1,
  RTL8922AE_ELEMENT_BB = 2,
  RTL8922AE_ELEMENT_BB_GAIN = 3,
  RTL8922AE_ELEMENT_RADIO_A = 4,
  RTL8922AE_ELEMENT_RADIO_B = 5,
  RTL8922AE_ELEMENT_RADIO_C = 6,
  RTL8922AE_ELEMENT_RADIO_D = 7,
  RTL8922AE_ELEMENT_NCTL = 8,
  RTL8922AE_ELEMENT_BYRATE = 9,
  RTL8922AE_ELEMENT_LIMIT_2G = 10,
  RTL8922AE_ELEMENT_LIMIT_5G = 11,
  RTL8922AE_ELEMENT_LIMIT_6G = 12,
  RTL8922AE_ELEMENT_RU_2G = 13,
  RTL8922AE_ELEMENT_RU_5G = 14,
  RTL8922AE_ELEMENT_RU_6G = 15,
  RTL8922AE_ELEMENT_SHAPE = 16,
  RTL8922AE_ELEMENT_SHAPE_RU = 17,
  RTL8922AE_ELEMENT_TRACKING = 18,
  RTL8922AE_ELEMENT_RFK_LOG = 19,
  RTL8922AE_ELEMENT_REGD = 20,
  RTL8922AE_ELEMENT_DA_LIMIT_2G = 21,
  RTL8922AE_ELEMENT_DA_LIMIT_5G = 22,
  RTL8922AE_ELEMENT_DA_LIMIT_6G = 23,
  RTL8922AE_ELEMENT_DA_RU_2G = 24,
  RTL8922AE_ELEMENT_DA_RU_5G = 25,
  RTL8922AE_ELEMENT_DA_RU_6G = 26,
  RTL8922AE_ELEMENT_AFE = 27,
  RTL8922AE_ELEMENT_COUNT = 28,
};

#define RTL8922AE_ELEMENT_BIT(id) (UINT32_C(1) << (id))
#define RTL8922AE_TABLES_POWER_2G_MASK ( \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_BYRATE) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_LIMIT_2G) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_DA_LIMIT_2G) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_RU_2G) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_DA_RU_2G) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_SHAPE) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_SHAPE_RU))
#define RTL8922AE_TABLES_PHY_MASK ( \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_BB) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_RADIO_A) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_RADIO_B))
#define RTL8922AE_TABLES_REQUIRED_MASK (RTL8922AE_TABLES_POWER_2G_MASK | \
    RTL8922AE_TABLES_PHY_MASK | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_BBMCU0) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_NCTL) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_TRACKING) | \
    RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_REGD))
#define RTL8922AE_TABLES_DEFAULT_RFE 0u
#define RTL8922AE_TRACKING_TYPES 16u
#define RTL8922AE_TRACKING_POINTS 30u

enum rtl8922ae_tables_result {
  RTL8922AE_TABLES_OK,
  RTL8922AE_TABLES_MALFORMED,
  RTL8922AE_TABLES_MISSING,
  RTL8922AE_TABLES_UNSUPPORTED,
};

struct rtl8922ae_firmware_table {
  size_t offset;
  size_t bytes;
  uint32_t records;
  uint32_t headline_records;
  uint32_t headline_target;
  uint8_t stride;
  uint8_t radio_index;
  uint8_t rfe;
  uint8_t cut;
  uint8_t image_header_version;
  uint8_t image_sections;
  uint8_t image_security_sections;
  uint8_t version[4];
  bool selected;
  bool headline_exact;
};

struct rtl8922ae_firmware_tables {
  struct rtl8922ae_firmware_table table[RTL8922AE_ELEMENT_COUNT];
  size_t elements;
  size_t opaque_elements;
  uint32_t observed_mask;
  uint32_t selected_mask;
  uint32_t selected_power_mask;
  uint32_t default_power_mask;
  uint32_t missing_mask;
  uint32_t unsupported_mask;
  uint32_t exact_phy_mask;
  uint32_t tracking_bitmap;
  uint8_t live_cut;
  uint8_t live_rfe;
};

struct rtl8922ae_table_byrate {
  uint8_t band, nss, section, first_rate, count, bandwidth, ofdma;
  int8_t value[4];
};

/* channel_index is the reference table index, not a channel number. Values
 * remain signed reference units; selection alone supplies no effective cap. */
struct rtl8922ae_table_limit {
  uint8_t bandwidth, transmitters, section, beamforming, region;
  uint8_t power_6g, channel_index;
  int8_t value;
};

struct rtl8922ae_table_shape {
  uint8_t band, section, region, value;
};

struct rtl8922ae_table_regd {
  char country[2];
  uint8_t region_2g, region_5g, region_6g;
  uint32_t functions;
};

union rtl8922ae_table_entry {
  struct rtl8922ae_table_byrate byrate;
  struct rtl8922ae_table_limit limit;
  struct rtl8922ae_table_shape shape;
  struct rtl8922ae_table_regd regd;
};

/* CPU-only data qualification of the admitted immutable asset. Caller owns
 * nonoverlapping output storage; offsets borrow the retained asset. No heap,
 * MMIO, download or RF authorization. Exact power RFE wins; otherwise the
 * reference-defined RFE0 is explicitly recorded in default_power_mask.
 * No country, regional limit, SAR or calibration permission is inferred.
 * MALFORMED clears output; MISSING/UNSUPPORTED retain diagnostic selections.
 */
enum rtl8922ae_tables_result rtl8922ae_firmware_tables(const void *data,
    size_t bytes, uint8_t live_cut, uint8_t live_rfe,
    struct rtl8922ae_firmware_tables *tables);

/* Optional, RFE-independent AFE instructions needed before firmware can expose
 * the live board. MISSING is validated absence; no placeholder RFE is selected.
 * Full board/power selection still follows the successful live eFuse read. */
enum rtl8922ae_tables_result rtl8922ae_firmware_afe_table(const void *data,
    size_t bytes, struct rtl8922ae_firmware_table *table);

/* Read one sparse record; MISSING never means zero power. Caller interprets
 * the union by element id. No country/WW fallback or missing-entry synthesis.
 * Asset and successful tables result must remain unchanged and nonoverlapping
 * with output. All failures clear output; unsupported IDs are explicit.
 */
enum rtl8922ae_tables_result rtl8922ae_firmware_table_entry(const void *data,
    size_t bytes, const struct rtl8922ae_firmware_tables *tables,
    enum rtl8922ae_firmware_element_id id, size_t index,
    union rtl8922ae_table_entry *entry);

enum rtl8922ae_tables_result rtl8922ae_firmware_tracking_delta(const void *data,
    size_t bytes, const struct rtl8922ae_firmware_tables *tables,
    uint8_t type, uint8_t subband, uint8_t point, int8_t *delta);

#endif
