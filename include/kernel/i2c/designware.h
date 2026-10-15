/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_I2C_DESIGNWARE_H
#define KERNEL_I2C_DESIGNWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum dw_i2c_result {
  DW_I2C_OK,
  DW_I2C_BUSY,
  DW_I2C_ADDRESS_NACK,
  DW_I2C_DATA_NACK,
  DW_I2C_ARBITRATION_LOST,
  DW_I2C_TIMEOUT,
  DW_I2C_IO,
  DW_I2C_UNSUPPORTED,
};

struct dw_i2c_master {
  uintptr_t base;
  uint16_t tx_depth, rx_depth;
  uint32_t component_version, component_parameters, abort_source, last_status;
  bool configured, failed;
};

struct dw_i2c_timing {
  uint32_t speed_hz, high_count, low_count, sda_hold;
};

/* The caller owns a retained, uncached mapping covering at least 0x100 bytes.
 * One BSP owner serializes all calls; identify/halt also work at boot with
 * IF=0. Configure/transfer run on the owning worker with IF=1. Deadlines are
 * absolute initialized arch-clock nanoseconds, including cleanup time.
 * A failed cleanup latches failed: retain ownership until reboot. */
enum dw_i2c_result dw_i2c_identify(struct dw_i2c_master *master, uintptr_t base);
enum dw_i2c_result dw_i2c_halt(struct dw_i2c_master *master, uint64_t deadline);
/* Requires disabled, idle hardware. Counts/hold come from the attachment's
 * checked clock/timing configuration; this layer invents no timing defaults. */
enum dw_i2c_result dw_i2c_configure(struct dw_i2c_master *master,
    const struct dw_i2c_timing *timing, uint64_t deadline);
/* One write, one read, or write followed by repeated START/read, ending in STOP.
 * A failed read leaves RX partially written; no partial result is successful.
 * Return leaves the controller disabled, or failed latched if not proven. */
enum dw_i2c_result dw_i2c_transfer(struct dw_i2c_master *master,
    uint16_t address, bool ten_bit, const uint8_t *tx, size_t tx_size,
    uint8_t *rx, size_t rx_size, uint64_t deadline);
const char *dw_i2c_result_name(enum dw_i2c_result result);

#endif
