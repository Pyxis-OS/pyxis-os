/* SPDX-License-Identifier: MPL-2.0 */
#ifndef KERNEL_WIFI_REGDB_H
#define KERNEL_WIFI_REGDB_H

#include <abi/wifi.h>
#include <stdbool.h>

/* These retain the pinned database's rule flags, not controller permissions. */
enum wifi_regdb_flags {
  WIFI_REGDB_NO_OFDM = WIFI_RULE_NO_OFDM,
  WIFI_REGDB_NO_CCK = WIFI_RULE_NO_CCK,
  WIFI_REGDB_NO_INDOOR = WIFI_RULE_NO_INDOOR,
  WIFI_REGDB_NO_OUTDOOR = WIFI_RULE_NO_OUTDOOR,
  WIFI_REGDB_DFS = WIFI_RULE_DFS,
  WIFI_REGDB_PTP_ONLY = WIFI_RULE_PTP_ONLY,
  WIFI_REGDB_PTMP_ONLY = WIFI_RULE_PTMP_ONLY,
  WIFI_REGDB_NO_IR = WIFI_RULE_NO_IR,
  WIFI_REGDB_NO_HT40 = WIFI_RULE_NO_HT40,
  WIFI_REGDB_AUTO_BW = WIFI_RULE_AUTO_BW,
};

struct wifi_regdb_channel {
  uint32_t flags;
  int32_t max_eirp_mbm;
};

struct wifi_regdb_profile {
  uint32_t channels;
  struct wifi_regdb_channel channel[WIFI_CHANNEL_24_COUNT];
};

/* Pure lookup of the first rule containing the entire 20 MHz channel. The
 * initial native Realtek profile covers channels 1-13. No matching country is
 * an error, never an implicit world fallback. The caller selects "00" when
 * host country is absent. A matching rule supplies no board or RFK authority. */
bool wifi_regdb_profile(const char country[2], struct wifi_regdb_profile *profile);

#endif
