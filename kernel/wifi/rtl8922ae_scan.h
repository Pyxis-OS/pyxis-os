/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_SCAN_H
#define RTL8922AE_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wifi_status_reply;

/* Sole worker, IF=1, one unassociated CMAC0/port0/MACID0 role. Reset only
 * before firmware startup. Setup follows the caller's NOLINK port
 * initialization. No probe/association/packet templates. */
void rtl8922ae_scan_reset(void);
/* Snapshot only: observed OFLD_CFG DONE_ACK, without waiting or changing admission. */
void rtl8922ae_scan_status(struct wifi_status_reply *status);
bool rtl8922ae_scan_setup(const uint8_t mac[6], uint64_t deadline, const char **detail);
/* Reissue the retained same-MAC CAM at core_scan_start/complete boundaries.
 * No caller storage is retained. Call only before START or after confirmed END. */
bool rtl8922ae_scan_refresh_cam(uint64_t deadline, const char **detail);
/* Up to two already-authorized 2.4 GHz/20 MHz channels, with power/calibration
 * established by the caller. Success includes START ACK and END_SCAN success.
 * On cancellation the caller must stop with its independent cleanup deadline;
 * any uncertain stop closes reuse and requires terminal transport retirement. */
bool rtl8922ae_scan_run(uint32_t channels, uint64_t deadline, const char **detail);
bool rtl8922ae_scan_stop(uint64_t deadline, const char **detail);
/* An unconfirmed command ACK, failed END, or transport/STOP failure permanently
 * closes reuse until reset before firmware startup. STOP remains available for
 * cleanup and never clears that uncertainty. A cancelled confirmed START can
 * return to reuse after confirmed END/STOP. The caller must refresh CAM before
 * starting another request. */
bool rtl8922ae_scan_reusable(void);
/* Observed ENTER_CH channels for the current request, retained through END.
 * Diagnostic only; never per-frame attribution or an extra completion gate. */
uint32_t rtl8922ae_scan_channels(void);
/* Checked C2H envelope in borrowed DMA storage; no pointer survives the call.
 * Unrelated reports are ignored. False reports a malformed relevant envelope. */
bool rtl8922ae_scan_receive(const void *data, size_t bytes);

#endif
