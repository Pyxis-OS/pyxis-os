/* SPDX-License-Identifier: MPL-2.0 */
#include "regdb.h"
#include <kernel/memory.h>

struct wifi_regdb_rule {
  uint32_t start_khz, end_khz, max_bandwidth_khz;
  int32_t max_eirp_mbm, max_antenna_gain_mbi;
  uint32_t flags;
  bool has_antenna_gain;
};

struct wifi_regdb_country {
  char alpha2[2];
  uint16_t first_rule, rule_count;
};

#include "regdb_data.inc"

#define CHANNEL_BANDWIDTH_KHZ 20000u
#define CHANNEL_FIRST_CENTER_KHZ 2412000u
#define CHANNEL_SPACING_KHZ 5000u
#define CHANNEL_COUNT 13u

bool wifi_regdb_profile(const char country[2], struct wifi_regdb_profile *profile)
{
  memset(profile, 0, sizeof(*profile));
  const struct wifi_regdb_country *domain = NULL;
  for (size_t i = 0; i < WIFI_REGDB_COUNTRY_COUNT; ++i) {
    if (wifi_regdb_countries[i].alpha2[0] == country[0] &&
        wifi_regdb_countries[i].alpha2[1] == country[1]) {
      domain = &wifi_regdb_countries[i];
      break;
    }
  }
  if (!domain) {
    return false;
  }
  for (size_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    uint32_t center = CHANNEL_FIRST_CENTER_KHZ + channel * CHANNEL_SPACING_KHZ;
    for (size_t i = 0; i < domain->rule_count; ++i) {
      const struct wifi_regdb_rule *rule = &wifi_regdb_rules[domain->first_rule + i];
      if (rule->max_bandwidth_khz < CHANNEL_BANDWIDTH_KHZ ||
          center - CHANNEL_BANDWIDTH_KHZ / 2 < rule->start_khz ||
          center + CHANNEL_BANDWIDTH_KHZ / 2 > rule->end_khz) {
        continue;
      }
      /* The selected source has no supplied antenna-gain bound. Refuse a
       * future incompatible generated table rather than drop that bound. */
      if (rule->has_antenna_gain) {
        return false;
      }
      profile->channels |= UINT32_C(1) << channel;
      profile->channel[channel] = (struct wifi_regdb_channel){
        .flags = rule->flags, .max_eirp_mbm = rule->max_eirp_mbm,
      };
      break;
    }
  }
  return true;
}
