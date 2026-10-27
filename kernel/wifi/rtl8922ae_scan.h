/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_SCAN_H
#define RTL8922AE_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sole worker, IF=1, one unassociated CMAC0/port0/MACID0 role. Reset only
 * before firmware startup. Setup follows the caller's NOLINK port
 * initialization. No probe/association/packet templates. */
void rtl8922ae_scan_reset(void);
bool rtl8922ae_scan_setup(const uint8_t mac[6], uint64_t deadline, const char **detail);
/* One already-authorized 2.4 GHz/20 MHz channel, with its power and calibration
 * established by the caller. Success includes START ACK and END_SCAN success.
 * On cancellation the caller must stop with its independent cleanup deadline;
 * any uncertain stop closes reuse and requires terminal transport retirement. */
bool rtl8922ae_scan_run(uint8_t channel, uint64_t deadline, const char **detail);
bool rtl8922ae_scan_stop(uint64_t deadline, const char **detail);
/* Last firmware ENTER_CH primary channel, zero until one has been reported.
 * Keep it through END/STOP so the caller can drain already queued RX. */
uint8_t rtl8922ae_scan_channel(void);
/* Checked C2H envelope in borrowed DMA storage; no pointer survives the call.
 * Unrelated reports are ignored. False reports a malformed relevant envelope. */
bool rtl8922ae_scan_receive(const void *data, size_t bytes);

#endif
