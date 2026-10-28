/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_PPDU_H
#define RTL8922AE_PPDU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rtl8922ae_ppdu_identity {
  uint8_t phy, counter, rate, bandwidth, gi;
  bool eht, long_rxdesc;
};

/* Decode an ordinary or PPDU RXD independently of its payload. Unknown GI
 * follows the reference's distinct ordinary-frame/report normalization.
 * The caller owns the copied identity and every pending-group lifetime. */
bool rtl8922ae_ppdu_identity(const uint8_t *rxd, size_t bytes,
    struct rtl8922ae_ppdu_identity *identity);
bool rtl8922ae_ppdu_match(const struct rtl8922ae_ppdu_identity *frame,
    const struct rtl8922ae_ppdu_identity *report);

/* Borrow the type-1 payload only for this call. Return its decoded 2.4 GHz
 * channel, or zero for absent, invalid or other-band metadata. A zero result
 * still requires the caller to retire the report's identified PHY group. */
uint8_t rtl8922ae_ppdu_channel(const uint8_t *payload, size_t bytes);

#endif
