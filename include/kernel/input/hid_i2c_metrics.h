/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_INPUT_HID_I2C_METRICS_H
#define KERNEL_INPUT_HID_I2C_METRICS_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel-config.h>

struct hid_i2c_metrics;
/* Optional, finite native qualification. No scheduler accounting. The BSP
 * transport worker owns the record and calls these outside sleeping sections. */
#ifdef CONFIG_HID_I2C_MEASURE
struct hid_i2c_metrics *hid_i2c_metrics_create(void);
uint32_t hid_i2c_metrics_interval(struct hid_i2c_metrics *metrics);
void hid_i2c_metrics_report(struct hid_i2c_metrics *metrics);
void hid_i2c_metrics_poll(struct hid_i2c_metrics *metrics, uint64_t start,
    uint64_t end, bool polled, unsigned frames, bool asserted, uint64_t malformed);
#else
static inline struct hid_i2c_metrics *hid_i2c_metrics_create(void)
{
  return NULL;
}
static inline uint32_t hid_i2c_metrics_interval(struct hid_i2c_metrics *metrics)
{
  (void)metrics;
  return CONFIG_HID_I2C_POLL_MS;
}
static inline void hid_i2c_metrics_report(struct hid_i2c_metrics *metrics)
{
  (void)metrics;
}
static inline void hid_i2c_metrics_poll(struct hid_i2c_metrics *metrics, uint64_t start,
    uint64_t end, bool polled, unsigned frames, bool asserted, uint64_t malformed)
{
  (void)metrics;
  (void)start;
  (void)end;
  (void)polled;
  (void)frames;
  (void)asserted;
  (void)malformed;
}
#endif
#endif
