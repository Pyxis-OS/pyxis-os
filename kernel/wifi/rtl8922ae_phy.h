/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2023 Realtek Corporation
 * Derived from Linux rtw89 at 7d0a66e4bb9081d75c82ec4957c50034cb0ea449;
 * BSD alternative, see kernel/wifi/NOTICE.
 */
#ifndef RTL8922AE_PHY_H
#define RTL8922AE_PHY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rtl8922ae_firmware.h"
#include "rtl8922ae_hw.h"

/* Sole BSP worker; borrowed asset/tables/board remain immutable. These calls
 * allocate nothing and never issue RF calibration or scan commands. BAR2's
 * first 0x60000 bytes must remain mapped and owned through checked stop.
 * Every failure leaves the partially configured device to its caller's stop.
 * Initialization follows reference warning-only RF/XTAL error handling; a
 * true result does not verify successful RF programming or calibration.
 */
bool rtl8922ae_phy_afe(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, uint64_t deadline,
    const char **detail);
/* Set up both BB MCU register contexts before WCPU/BBMCU0 download.
 * The RTL8922A reference downloads firmware only to BBMCU0. */
bool rtl8922ae_phy_preinit(uint64_t deadline, const char **detail);
/* After combined firmware readiness, before full MAC initialization. */
bool rtl8922ae_phy_enable(uint64_t deadline, const char **detail);
bool rtl8922ae_phy_bb(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, uint64_t deadline,
    const char **detail);
bool rtl8922ae_phy_postinit(uint64_t deadline, const char **detail);
/* Direct RF A/B table writes, followed by their required firmware uploads. */
bool rtl8922ae_phy_radio(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, uint64_t deadline,
    const char **detail);
/* Static receive defaults, PLL, NCTL, physical trims and DBCC PHY0/A, PHY1/B
 * hardware setup. Caller first stops scheduler TX and PPDU reporting; this
 * call does not resume them. Dynamic monitoring/tracking is not enabled. */
bool rtl8922ae_phy_hardware(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_board *board, uint64_t deadline, const char **detail);

/* Caller stops scheduler TX and PPDU reporting before enter, then programs
 * and verifies effective limits after tune, before leave or any RFK command.
 * These helpers do not resume scheduler TX or enable automatic responses.
 * Complete enter/tune/leave for PHY0 before doing the same for PHY1.
 * Both contexts use the same previously admitted 2.4 GHz, 20 MHz channel.
 */
bool rtl8922ae_phy_channel_enter(unsigned phy, uint64_t deadline, const char **detail);
bool rtl8922ae_phy_channel_tune(unsigned phy, const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_board *board, uint8_t channel, uint64_t deadline,
    const char **detail);
/* RFK slots are caller-owned current-channel state, each in reference range
 * 0..2. This module has no calibration cache or channel-reuse policy. */
bool rtl8922ae_phy_channel_leave(unsigned phy, const uint8_t slots[2], uint64_t deadline,
    const char **detail);

/* RF v2 accesses include the reference HWSI completion waits. Values are
 * unshifted mask fields; addresses retain the reference ADSEL bit (bit 16).
 */
bool rtl8922ae_phy_rf_read(unsigned path, uint32_t address, uint32_t mask,
    uint32_t *value, uint64_t deadline, const char **detail);
bool rtl8922ae_phy_rf_write(unsigned path, uint32_t address, uint32_t mask,
    uint32_t value, uint64_t deadline, const char **detail);

/* Reference scan-time DIG/EDCCA setup on the unassociated scan PHY0. */
void rtl8922ae_phy_scan_edcca(bool scanning);
void rtl8922ae_phy_scan_dig(void);

#endif
