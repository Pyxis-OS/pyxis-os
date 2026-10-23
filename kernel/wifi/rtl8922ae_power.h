/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_POWER_H
#define RTL8922AE_POWER_H

#include "rtl8922ae_firmware.h"
#include "rtl8922ae_sar.h"
#include "regdb.h"

#define RTL8922AE_POWER_BYRATE_BYTES 428u
#define RTL8922AE_POWER_LIMIT_BYTES 76u
#define RTL8922AE_POWER_RU_BYTES 80u

enum rtl8922ae_power_result {
  RTL8922AE_POWER_OK,
  RTL8922AE_POWER_INVALID,
  RTL8922AE_POWER_UNSUPPORTED,
  RTL8922AE_POWER_MISSING,
  RTL8922AE_POWER_READBACK,
};

/* Owned immutable register image after prepare. No pointers into the asset.
 * Keep this in caller-owned static storage, separate from every input. */
struct rtl8922ae_power_image {
  uint8_t byrate[RTL8922AE_POWER_BYRATE_BYTES];
  uint8_t normal[2][RTL8922AE_POWER_LIMIT_BYTES];
  uint8_t ru[2][RTL8922AE_POWER_RU_BYTES];
  uint32_t rate_offsets;
  int16_t sar_quarter_dbm[2];
  uint8_t channel, region, shape;
  bool valid;
};

/* CPU-only: admitted immutable asset/tables and normalized SAR. Supports cut2/RFE1
 * with explicit RFE0 power fallback, 20 MHz 2.4 GHz and exact regional records.
 * Native input qualification currently covers country 00 only. Image
 * construction and actual programming are not yet hardware-qualified; this
 * function grants no calibration authority.
 * RTAG absence removes optional DAG correction; it says nothing about physical
 * antenna gain. When country rules enable DAG, only qualified absent
 * RTAG is supported. Inapplicable RTAG does not block these reference modes.
 * Rules must match the pinned regdb lookup.
 * No invented EIRP-to-path conversion, TAS or DA-table boost.
 * Failure clears image. No RF or device operations occur. */
enum rtl8922ae_power_result rtl8922ae_power_prepare(const void *asset,
    size_t bytes, const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_sar_envelope *sar,
    enum acpi_wifi_state antenna_state, const char country[2],
    const struct wifi_regdb_profile *rules, uint8_t channel,
    struct rtl8922ae_power_image *image);

/* MAC0/PHY0 only, with the full first 0x40000 of BAR2 mapped and the MAC/BB
 * clocks enabled by the caller. Does not tune or request RFK. Apply writes the
 * image; verify checks every owned register field without modifying hardware.
 * Caller retains scheduler/RF admission control across both calls. */
enum rtl8922ae_power_result rtl8922ae_power_apply(
    const struct rtl8922ae_power_image *image, uint64_t deadline,
    const char **detail);
enum rtl8922ae_power_result rtl8922ae_power_verify(
    const struct rtl8922ae_power_image *image, uint64_t deadline,
    const char **detail);

#endif
