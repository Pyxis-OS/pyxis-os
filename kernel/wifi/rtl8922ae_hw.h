/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_HW_H
#define RTL8922AE_HW_H

#include <stdbool.h>
#include <stdint.h>

struct rtl8922ae_board {
  uint8_t rfe, xtal, country[2];
  uint8_t mac[6];
  uint8_t tx_nss, rx_nss, tx_antennas, rx_antennas;
  uint8_t pa_bias[2], pad_bias[2];
  int8_t tssi_cck[2][6], tssi_ofdm_2g[2][5];
  uint8_t tssi_thermal[2];
  bool tssi_known;
  int8_t thermal_trim[2];
  int8_t gain_offset_2g_cck[2], gain_offset_2g_ofdm[2];
  bool gain_offset_known, gain_offset_present;
  bool thermal_trim_present, pa_bias_present, rfkill;
};

/* Sole BSP worker, IF=1, after standard PCI reset and BAR mapping. No
 * allocation, ring publication, calibration or network operation occurs here.
 * Output is cleared on failure; detail receives a static refusal string.
 * All waits are capped by the caller's absolute operation deadline. Failure
 * leaves hardware state for the caller's terminal standard PCI reset. */
uint8_t rtl8922ae_hw_cut(void);
bool rtl8922ae_hw_power_on(uint8_t cut, uint64_t deadline, const char **detail);
bool rtl8922ae_hw_xtal_read(uint8_t address, uint8_t *value, uint64_t deadline);
bool rtl8922ae_hw_xtal_write(uint8_t address, uint8_t value, uint8_t mask,
    uint64_t deadline);
/* After MAC power-on, before firmware/security and BB/RF startup. */
void rtl8922ae_hw_rfkill_prepare(void);
/* After GPIO setup, while the register domain is live.
 * Observe GPIO9 directly, as in the reference rfkill_get path. */
bool rtl8922ae_hw_rfkill(void);
/* Before startup DMA publication when an inherited MAC is active, or after
 * transport pre-deinit, while BAR/decode and DMA backing remain owned.
 * Attempt once even after partial power-on. No firmware power-save mode may
 * have been entered. Shutdown polls must remain usable after a transport fault.
 * True means the reference shutdown handshakes completed;
 * it does not replace the caller's unconditional terminal standard PCI reset. */
bool rtl8922ae_hw_power_off(uint64_t deadline, const char **detail);
bool rtl8922ae_hw_security(uint8_t bytes[4], uint8_t cut, uint64_t deadline,
    const char **detail);
bool rtl8922ae_hw_download_prepare(uint64_t deadline, const char **detail);
/* Fixed PCIe profile around the parent-owned DMA stop/index/publish steps. */
void rtl8922ae_hw_pci_prepare(void);
void rtl8922ae_hw_pci_mode(void);
void rtl8922ae_hw_pci_finish(uint8_t cut);
/* First mailbox exchange after normal firmware readiness: raw board/trim eFuse, checked PHY mailbox
 * and GPIO9 observation. rfkill is an observation, not an RF-silence proof. */
bool rtl8922ae_hw_board(struct rtl8922ae_board *board, uint8_t cut,
    uint64_t deadline, const char **detail);

#endif
