/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_HW_H
#define RTL8922AE_HW_H

#include <stdbool.h>
#include <stdint.h>

struct rtl8922ae_board {
  uint8_t rfe, xtal, country[2];
  uint8_t tx_nss, rx_nss, tx_antennas, rx_antennas;
  uint8_t pa_bias[2], pad_bias[2];
  int8_t thermal_trim[2];
  bool thermal_trim_present, pa_bias_present, rfkill;
};

/* Sole BSP worker, IF=1, after standard PCI reset and BAR mapping. No
 * allocation, ring publication, calibration or network operation occurs here.
 * Output is cleared on failure; detail receives a static refusal string.
 * All waits are capped by the caller's absolute operation deadline. Failure
 * leaves hardware state for the caller's terminal standard PCI reset. */
bool rtl8922ae_hw_cut(uint8_t *cut, const char **detail);
bool rtl8922ae_hw_power_on(uint8_t cut, uint64_t deadline, const char **detail);
bool rtl8922ae_hw_security(uint8_t bytes[4], uint8_t cut, uint64_t deadline,
    const char **detail);
bool rtl8922ae_hw_download_prepare(uint64_t deadline, const char **detail);
/* Fixed PCIe profile around the parent-owned DMA stop/index/publish steps. */
bool rtl8922ae_hw_pci_prepare(uint64_t deadline, const char **detail);
bool rtl8922ae_hw_pci_mode(uint64_t deadline, const char **detail);
bool rtl8922ae_hw_pci_finish(uint8_t cut, uint64_t deadline, const char **detail);
/* First mailbox exchange after normal firmware readiness: raw board/trim eFuse, checked PHY mailbox
 * and GPIO9 observation. rfkill is an observation, not an RF-silence proof. */
bool rtl8922ae_hw_board(struct rtl8922ae_board *board, uint8_t cut,
    uint64_t deadline, const char **detail);

#endif
