/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_IO_H
#define RTL8922AE_IO_H

#include <stdbool.h>
#include <stdint.h>

/* Private, bounded BAR2 access. Preparation establishes the mapping before
 * the BSP worker starts; the mapping and owner survive terminal failure. */
uint8_t rtl8922ae_read8(unsigned offset);
uint16_t rtl8922ae_read16(unsigned offset);
uint32_t rtl8922ae_read32(unsigned offset);
void rtl8922ae_write8(unsigned offset, uint8_t value);
void rtl8922ae_write16(unsigned offset, uint16_t value);
void rtl8922ae_write32(unsigned offset, uint32_t value);

/* Worker context, IF=1; no allocator or borrowed scratch storage. Polling uses
 * the register-specific mask and expected value, without a sentinel filter.
 * Every local wait remains bounded by the operation's absolute deadline. */
uint64_t rtl8922ae_deadline(uint64_t outer, uint64_t nanoseconds);
bool rtl8922ae_poll32(unsigned offset, uint32_t mask, uint32_t expected,
    uint64_t deadline);
/* Same polling, with the last value actually sampled before return. sampled
 * is false if the deadline expired before any read; no diagnostic reread. */
bool rtl8922ae_poll32_observe(unsigned offset, uint32_t mask, uint32_t expected,
    uint64_t deadline, uint32_t *observed, bool *sampled);
bool rtl8922ae_sleep(uint64_t nanoseconds, uint64_t deadline);
/* Sole worker's current request/cancellation/rfkill checkpoint. Shutdown uses
 * its independent cleanup deadline and bypasses the operation gate. */
bool rtl8922ae_operation_continue(void);

#endif
