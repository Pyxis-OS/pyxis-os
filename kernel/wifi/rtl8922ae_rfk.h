/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_RFK_H
#define RTL8922AE_RFK_H

#include "rtl8922ae_firmware.h"
#include "rtl8922ae_hw.h"

#define RTL8922AE_RFK_PRE_NOTIFY (1u << 0)
#define RTL8922AE_RFK_DACK (1u << 1)
#define RTL8922AE_RFK_RXDCK (1u << 2)
#define RTL8922AE_RFK_TSSI (1u << 3)

struct rtl8922ae_rfk_commands {
  uint8_t pre_notify[84], tssi[300], channel, thermal_default_paths;
  bool valid;
};

struct rtl8922ae_rfk_observations {
  uint32_t completed;
  uint8_t state, version;
  bool state_valid;
};

/* CPU-only command construction from the retained pinned asset and validated
 * board. No RF or register access. The caller owns immutable command storage. */
bool rtl8922ae_rfk_prepare(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_board *board, uint8_t cut, uint8_t channel,
    struct rtl8922ae_rfk_commands *commands, const char **detail);
/* Sole worker, before firmware startup. Completion is serialized; a failed or
 * uncertain command poisons this one attempt until the caller's terminal stop. */
void rtl8922ae_rfk_reset(void);
bool rtl8922ae_rfk_pre_notify(const struct rtl8922ae_rfk_commands *commands,
    uint64_t deadline, const char **detail);
bool rtl8922ae_rfk_dack(uint64_t deadline, const char **detail);
bool rtl8922ae_rfk_rxdck(const struct rtl8922ae_rfk_commands *commands,
    uint64_t deadline, const char **detail);
bool rtl8922ae_rfk_tssi(const struct rtl8922ae_rfk_commands *commands,
    uint64_t deadline, const char **detail);
void rtl8922ae_rfk_observe(struct rtl8922ae_rfk_observations *out);

#endif
