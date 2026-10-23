/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_SAR_H
#define RTL8922AE_SAR_H

#include <kernel/acpi/rtl_wifi.h>

#define RTL8922AE_SAR_SOURCE_WRDS (1u << 0)
#define RTL8922AE_SAR_SOURCE_RWRD (1u << 1)

enum rtl8922ae_sar_path {
  RTL8922AE_SAR_PATH_A,
  RTL8922AE_SAR_PATH_B,
  RTL8922AE_SAR_PATH_COUNT,
};

struct rtl8922ae_sar_source {
  uint16_t cid;
  uint8_t revision, antennas, subbands, profiles;
};

struct rtl8922ae_sar_envelope {
  enum acpi_wifi_state state;
  uint32_t source_flags;
  struct rtl8922ae_sar_source wrds, rwrd;
  int16_t limits_1tx_eighth_dbm[RTL8922AE_SAR_PATH_COUNT];
  int16_t limits_2tx_eighth_dbm[RTL8922AE_SAR_PATH_COUNT];
};

/* CPU-only, bounded interpretation of a borrowed immutable completed capture.
 * VALID describes 2.4 GHz SAR input bounds only, with every retained RWRD
 * profile intersected with present WRDS. It is not a TX/RFK grant or an applied
 * regdb/board/antenna/EIRP limit. Sources are normalized independently.
 * Present RWGS is unsupported; its constraint is never silently dropped.
 * TAS is disabled and cannot alter these numbers. RWSI is deliberately unused:
 * no selector is consumed when every profile constrains the fixed envelope.
 * Non-VALID results contain no source metadata or numeric limits. */
struct rtl8922ae_sar_envelope rtl8922ae_sar_envelope(
    const struct acpi_rtl_wifi_policy *policy, enum acpi_wifi_state capture_state);

#endif
