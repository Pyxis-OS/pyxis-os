/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_MAC_H
#define RTL8922AE_MAC_H

#include <stdbool.h>
#include <stdint.h>

/* BSP worker, IF=1, after combined firmware readiness and BB/RF enable.
 * One CMAC, two RF paths, PCIe SCC layout. No station, association, packet
 * offload or data-TX admission is created; scheduler TX remains stopped. */
bool rtl8922ae_mac_init(uint64_t deadline, const char **detail);
/* CMAC0 2.4 GHz/20 MHz context only; caller programs BB/RF separately. */
bool rtl8922ae_mac_channel(uint8_t channel, uint64_t deadline, const char **detail);
/* Disables response eligibility and scheduler TX, then proves protocol idle.
 * Transport DMA/reset retirement remains the caller's responsibility. */
bool rtl8922ae_mac_quiesce(uint64_t deadline, const char **detail);

#endif
