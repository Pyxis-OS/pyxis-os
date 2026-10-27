/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Linux v6.18 rtw89 phy_be.c, phy.c, phy.h, rtw8922a.c and reg.h at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449; BSD alternative, see NOTICE.
 * Native bounded immutable power images and checked BAR2 publication.
 */
#include "rtl8922ae_power.h"
#include "rtl8922ae_io.h"
#include <kernel/memory.h>

#define POWER_BB_BASE 0x20000u
#define POWER_BYRATE_BASE 0x11e00u
#define POWER_NORMAL_BASE 0x11facu
#define POWER_RU_BASE 0x12048u
#define POWER_REFERENCE 0x11a20u
#define POWER_RATE_CONTROL 0x11a2cu
#define POWER_RATE_OFFSETS 0x11a30u
#define POWER_SHAPE (POWER_BB_BASE + 0x6c04u)
#define POWER_PATH_MAX (POWER_BB_BASE + 0xe61cu)
#define POWER_PATH_REFERENCE (POWER_BB_BASE + 0xe628u)
#define POWER_PATH_TSSI_K (POWER_BB_BASE + 0xe6a0u)
#define POWER_PATH_STRIDE 0x100u
#define POWER_SAR_MAX_MASK 0x001ff000u
#define POWER_PATH_REFERENCE_MASK 0x0003ffffu
#define POWER_TSSI_K_MASK 0x3ff00000u
#define POWER_TSSI_K 0x12u
#define POWER_REGION_WW 0u
#define POWER_BAND_2G 0u
#define POWER_RFE 1u
#define POWER_CUT 2u
#define POWER_MAX_RECORDS 65536u

/* Controls owned by this profile: no forced limit/rate/RU/MACID/coex value,
 * and no additive limit, beamforming, by-rate, RU or software offset. */
struct power_control {
  uint32_t address, mask, value;
};

static const struct power_control power_controls[] = {
  {0x11a28, 1u << 6, 0},
  {0x11a40, (1u << 29) | (1u << 16), 0},
  {0x11a44, (1u << 28) | (1u << 18) | 0x0003fe00u, 0},
  {0x11a48, 0x000ffe00u, 0},
  {0x11a54, 0x38000000u, 0},
  {POWER_RATE_CONTROL, (1u << 19) | 0x1ffu, 0},
  {POWER_REFERENCE, 0x0ffffffeu, 0},
  {0x11a24, 0x1ffu, 0},
  {0x11ae8, 0x0f000000u, 0},
  {0x12044, UINT32_MAX, 0},
  {0x11b00, UINT32_MAX, 0x00e4e431},
  {0x11b04, 0x7u, 0},
  {0x11a84, UINT32_MAX, 0x0201fe00},
  {0x11a78, UINT32_MAX, 0x00ffec7e},
};

static int floor_divide(int value, int divisor)
{
  if (value >= 0) {
    return value / divisor;
  }
  return -((-value + divisor - 1) / divisor);
}

static int smaller(int a, int b)
{
  return a < b ? a : b;
}

static uint32_t pack_bytes(const uint8_t *p)
{
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
      (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static enum rtl8922ae_power_result entry_at(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, unsigned id, size_t index,
    union rtl8922ae_table_entry *entry)
{
  enum rtl8922ae_tables_result result = rtl8922ae_firmware_table_entry(asset,
      bytes, tables, id, index, entry);
  if (result == RTL8922AE_TABLES_OK) {
    return RTL8922AE_POWER_OK;
  }
  return result == RTL8922AE_TABLES_MISSING ? RTL8922AE_POWER_MISSING :
      RTL8922AE_POWER_INVALID;
}

/* A sparse numeric record is mandatory, even when the numeric value is zero.
 * Linux's zero-filled backing arrays are not a missing-record authority. */
static enum rtl8922ae_power_result byrate_value(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, unsigned width,
    unsigned nss, unsigned section, unsigned ofdma, unsigned rate, int *value)
{
  bool found = false;
  for (size_t i = 0; i < tables->table[RTL8922AE_ELEMENT_BYRATE].records; ++i) {
    union rtl8922ae_table_entry entry;
    enum rtl8922ae_power_result result = entry_at(asset, bytes, tables,
        RTL8922AE_ELEMENT_BYRATE, i, &entry);
    if (result != RTL8922AE_POWER_OK) {
      return result;
    }
    const struct rtl8922ae_table_byrate *e = &entry.byrate;
    if (e->band != POWER_BAND_2G || e->bandwidth != width ||
        e->section != section || rate < e->first_rate ||
        rate >= (unsigned)e->first_rate + e->count) {
      continue;
    }
    if ((section == 2 || section == 3) &&
        (e->nss != nss || e->ofdma != ofdma)) {
      continue;
    }
    if (found) {
      return RTL8922AE_POWER_INVALID;
    }
    found = true;
    *value = floor_divide(e->value[rate - e->first_rate], 2);
  }
  return found ? RTL8922AE_POWER_OK : RTL8922AE_POWER_MISSING;
}

struct power_rate_group {
  uint8_t section, ofdma, first, count;
  bool no_wide, no_multi;
};

static const struct power_rate_group power_rate_groups[] = {
  {0, 0, 0, 4, true, true},
  {1, 0, 0, 8, false, true},
  {2, 0, 14, 2, false, true},
  {2, 1, 14, 2, false, true},
  {2, 0, 0, 14, false, false},
  {3, 0, 0, 4, false, false},
  {2, 1, 0, 14, false, false},
  {3, 1, 0, 4, false, false},
};

static enum rtl8922ae_power_result prepare_byrate(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    struct rtl8922ae_power_image *image)
{
  size_t cursor = 0;
  for (unsigned width = 0; width < 5; ++width) {
    for (unsigned nss = 0; nss < 2; ++nss) {
      for (size_t group = 0; group < sizeof(power_rate_groups) /
          sizeof(power_rate_groups[0]); ++group) {
        const struct power_rate_group *g = &power_rate_groups[group];
        if ((width > 1 && g->no_wide) || (nss && g->no_multi)) {
          continue;
        }
        for (unsigned rate = g->first; rate < (unsigned)g->first + g->count;
            ++rate) {
          if (cursor >= sizeof(image->byrate)) {
            return RTL8922AE_POWER_INVALID;
          }
          /* 2.4 GHz has no 80/160/320 MHz operation. Preserve those inactive
           * reference-layout bytes as zero; never read them as rate data. */
          int value = 0;
          if (width <= 1) {
            enum rtl8922ae_power_result result = byrate_value(asset, bytes,
                tables, width, nss, g->section, g->ofdma, rate, &value);
            if (result != RTL8922AE_POWER_OK) {
              return result;
            }
          }
          image->byrate[cursor++] = (uint8_t)value;
        }
      }
    }
  }
  if (cursor != sizeof(image->byrate)) {
    return RTL8922AE_POWER_INVALID;
  }
  /* Source ordering differs from the eight hardware offset nibbles. Missing
   * additive adjustments mean no adjustment, never a zero-dBm target/limit. */
  static const uint8_t offset_source[] = {4, 3, 2, 1, 0, 7, 6, 5};
  for (unsigned rate = 0; rate < sizeof(offset_source); ++rate) {
    int value = 0;
    enum rtl8922ae_power_result result = byrate_value(asset, bytes, tables,
        0, 0, 4, 0, offset_source[rate], &value);
    if (result != RTL8922AE_POWER_OK && result != RTL8922AE_POWER_MISSING) {
      return result;
    }
    if (value < -8 || value > 7) {
      return RTL8922AE_POWER_UNSUPPORTED;
    }
    image->rate_offsets |= ((uint32_t)value & 0xfu) << (rate * 4);
  }
  return RTL8922AE_POWER_OK;
}

static enum rtl8922ae_power_result limit_value(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, unsigned id,
    unsigned width, unsigned ntx, unsigned section, unsigned bf,
    unsigned channel, unsigned region, int sar_half_dbm, uint8_t *value)
{
  bool found = false;
  for (size_t i = 0; i < tables->table[id].records; ++i) {
    union rtl8922ae_table_entry entry;
    enum rtl8922ae_power_result result = entry_at(asset, bytes, tables, id, i,
        &entry);
    if (result != RTL8922AE_POWER_OK) {
      return result;
    }
    const struct rtl8922ae_table_limit *e = &entry.limit;
    if (e->region != region || e->bandwidth != width ||
        e->transmitters != ntx || e->section != section ||
        e->beamforming != bf || e->channel_index != channel - 1) {
      continue;
    }
    if (found) {
      return RTL8922AE_POWER_INVALID;
    }
    found = true;
    *value = (uint8_t)smaller(floor_divide(e->value, 2), sar_half_dbm);
  }
  return found ? RTL8922AE_POWER_OK : RTL8922AE_POWER_MISSING;
}

static enum rtl8922ae_power_result prepare_limits(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_sar_envelope *sar,
    struct rtl8922ae_power_image *image)
{
  for (unsigned ntx = 0; ntx < 2; ++ntx) {
    const int16_t *limits = ntx ? sar->limits_2tx_eighth_dbm :
        sar->limits_1tx_eighth_dbm;
    int sar_half = floor_divide(smaller(limits[0], limits[1]), 4);
    for (unsigned slot = 0; slot < 4; ++slot) {
      unsigned width = slot == 1 ? 1 : 0;
      unsigned section = slot < 2 ? 0 : slot - 1;
      for (unsigned bf = 0; bf < 2; ++bf) {
        /* CCK/OFDM have no BF record; one-transmitter MCS has no BF path.
         * These inactive reference slots remain zero. */
        if (bf && (section != 2 || !ntx)) {
          continue;
        }
        enum rtl8922ae_power_result result = limit_value(asset, bytes, tables,
            RTL8922AE_ELEMENT_LIMIT_2G, width, ntx, section, bf,
            image->channel, image->region, sar_half, &image->normal[ntx][slot * 2 + bf]);
        if (result != RTL8922AE_POWER_OK) {
          return result;
        }
      }
    }
    for (unsigned ru = 0; ru < 5; ++ru) {
      enum rtl8922ae_power_result result = limit_value(asset, bytes, tables,
          RTL8922AE_ELEMENT_RU_2G, ru, ntx, 0, 0, image->channel, image->region, sar_half,
          &image->ru[ntx][ru * 16]);
      if (result != RTL8922AE_POWER_OK) {
        return result;
      }
    }
  }
  return RTL8922AE_POWER_OK;
}

static enum rtl8922ae_power_result prepare_region(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, const char country[2],
    enum acpi_wifi_state antenna_state, struct rtl8922ae_power_image *image)
{
  /* The pinned reference defines country00 separately from firmware REGD. */
  if (country[0] == '0' && country[1] == '0') {
    image->region = POWER_REGION_WW;
    return RTL8922AE_POWER_OK;
  }
  bool found = false;
  uint32_t functions = 0;
  for (size_t i = 0; i < tables->table[RTL8922AE_ELEMENT_REGD].records; ++i) {
    union rtl8922ae_table_entry entry;
    enum rtl8922ae_power_result result = entry_at(asset, bytes, tables,
        RTL8922AE_ELEMENT_REGD, i, &entry);
    if (result != RTL8922AE_POWER_OK) {
      return result;
    }
    if (entry.regd.country[0] != country[0] ||
        entry.regd.country[1] != country[1]) {
      continue;
    }
    if (found) {
      return RTL8922AE_POWER_INVALID;
    }
    found = true;
    image->region = entry.regd.region_2g;
    functions = entry.regd.functions;
  }
  if (!found) {
    return RTL8922AE_POWER_MISSING;
  }
  if (functions & ~3u) {
    return RTL8922AE_POWER_UNSUPPORTED;
  }
  /* REGD bit1 permits optional dynamic antenna-gain adjustment. Its absence
   * cannot fabricate a physical gain or select the higher DA ceilings. */
  if ((functions & (1u << 1)) && antenna_state != ACPI_WIFI_ABSENT) {
    return RTL8922AE_POWER_UNSUPPORTED;
  }
  return RTL8922AE_POWER_OK;
}

static enum rtl8922ae_power_result prepare_shape(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    struct rtl8922ae_power_image *image)
{
  bool found = false;
  for (size_t i = 0; i < tables->table[RTL8922AE_ELEMENT_SHAPE].records; ++i) {
    union rtl8922ae_table_entry entry;
    enum rtl8922ae_power_result result = entry_at(asset, bytes, tables,
        RTL8922AE_ELEMENT_SHAPE, i, &entry);
    if (result != RTL8922AE_POWER_OK) {
      return result;
    }
    if (entry.shape.band != POWER_BAND_2G || entry.shape.section != 1 ||
        entry.shape.region != image->region) {
      continue;
    }
    if (found) {
      return RTL8922AE_POWER_INVALID;
    }
    found = true;
    image->shape = entry.shape.value != 0;
  }
  /* Linux's WW shape mode is disabled in its zero-initialized mode array;
   * there is no WW shape record in the pinned asset. This is a named mode,
   * not a synthesized numeric power ceiling. */
  if (!found && image->region != POWER_REGION_WW) {
    return RTL8922AE_POWER_MISSING;
  }
  return RTL8922AE_POWER_OK;
}

enum rtl8922ae_power_result rtl8922ae_power_prepare(const void *asset,
    size_t bytes, const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_sar_envelope *sar,
    enum acpi_wifi_state antenna_state, const char country[2],
    const struct wifi_regdb_profile *rules, uint8_t channel,
    struct rtl8922ae_power_image *image)
{
  if (!image) {
    return RTL8922AE_POWER_INVALID;
  }
  memset(image, 0, sizeof(*image));
  if (!asset || !tables || !sar || !country || !rules ||
      !channel || channel > 13 || sar->state != ACPI_WIFI_VALID ||
      !sar->source_flags) {
    return RTL8922AE_POWER_INVALID;
  }
  if (tables->live_cut != POWER_CUT || tables->live_rfe != POWER_RFE) {
    return RTL8922AE_POWER_UNSUPPORTED;
  }
  static const unsigned required[] = {
    RTL8922AE_ELEMENT_BYRATE, RTL8922AE_ELEMENT_LIMIT_2G,
    RTL8922AE_ELEMENT_RU_2G, RTL8922AE_ELEMENT_SHAPE,
    RTL8922AE_ELEMENT_REGD,
  };
  for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
    unsigned id = required[i];
    const struct rtl8922ae_firmware_table *table = &tables->table[id];
    if (!table->selected || !table->records) {
      return RTL8922AE_POWER_MISSING;
    }
    if (table->records > POWER_MAX_RECORDS ||
        (tables->unsupported_mask & RTL8922AE_ELEMENT_BIT(id))) {
      return RTL8922AE_POWER_UNSUPPORTED;
    }
    union rtl8922ae_table_entry entry;
    if (entry_at(asset, bytes, tables, id, 0, &entry) != RTL8922AE_POWER_OK) {
      return RTL8922AE_POWER_INVALID;
    }
    if (id != RTL8922AE_ELEMENT_REGD &&
        (table->rfe != RTL8922AE_TABLES_DEFAULT_RFE ||
        !(tables->default_power_mask & RTL8922AE_ELEMENT_BIT(id)))) {
      return RTL8922AE_POWER_UNSUPPORTED;
    }
  }
  struct wifi_regdb_profile expected_rules;
  if (!wifi_regdb_profile(country, &expected_rules) ||
      rules->channels != expected_rules.channels) {
    return RTL8922AE_POWER_INVALID;
  }
  for (size_t i = 0; i < WIFI_CHANNEL_24_COUNT; ++i) {
    if (rules->channel[i].flags != expected_rules.channel[i].flags ||
        rules->channel[i].max_eirp_mbm !=
        expected_rules.channel[i].max_eirp_mbm) {
      return RTL8922AE_POWER_INVALID;
    }
  }
  if (!(rules->channels & (1u << (channel - 1)))) {
    return RTL8922AE_POWER_UNSUPPORTED;
  }
  /* This image enables all ordinary 2.4 GHz modulation types. NO_IR/DFS and
   * specialized-use rules require a different calibration admission contract. */
  uint32_t allowed_flags = WIFI_REGDB_AUTO_BW | WIFI_REGDB_NO_HT40;
  if (rules->channel[channel - 1].flags & ~allowed_flags) {
    return RTL8922AE_POWER_UNSUPPORTED;
  }
  image->channel = channel;
  enum rtl8922ae_power_result result = prepare_region(asset, bytes, tables,
      country, antenna_state, image);
  if (result != RTL8922AE_POWER_OK) {
    goto fail;
  }
  for (unsigned path = 0; path < 2; ++path) {
    int limit = floor_divide(smaller(sar->limits_1tx_eighth_dbm[path],
        sar->limits_2tx_eighth_dbm[path]), 4);
    if (limit < -64 || limit > 63) {
      result = RTL8922AE_POWER_UNSUPPORTED;
      goto fail;
    }
    image->sar_quarter_dbm[path] = limit * 2;
  }
  result = prepare_byrate(asset, bytes, tables, image);
  if (result != RTL8922AE_POWER_OK) {
    goto fail;
  }
  result = prepare_limits(asset, bytes, tables, sar, image);
  if (result != RTL8922AE_POWER_OK) {
    goto fail;
  }
  result = prepare_shape(asset, bytes, tables, image);
  if (result != RTL8922AE_POWER_OK) {
    goto fail;
  }
  image->valid = true;
  return RTL8922AE_POWER_OK;
fail:
  memset(image, 0, sizeof(*image));
  return result;
}

static bool power_checkpoint(uint64_t deadline, const char **detail)
{
  if (rtl8922ae_sleep(0, deadline)) {
    return true;
  }
  *detail = "RTL8922AE power operation interrupted";
  return false;
}

static void power_write_field(unsigned address, uint32_t mask, uint32_t value)
{
  if (mask != UINT32_MAX) {
    uint32_t previous = rtl8922ae_read32(address);
    value = (previous & ~mask) | (value & mask);
  }
  rtl8922ae_write32(address, value);
}

static bool power_verify_field(unsigned address, uint32_t mask, uint32_t value,
    const char *failure, const char **detail)
{
  if ((rtl8922ae_read32(address) & mask) != (value & mask)) {
    *detail = failure;
    return false;
  }
  return true;
}

static bool power_write_words(unsigned address, const uint8_t *data, size_t bytes,
    uint64_t deadline, const char **detail)
{
  for (size_t offset = 0; offset < bytes; offset += 4) {
    if (!(offset % 64) && !power_checkpoint(deadline, detail)) {
      return false;
    }
    power_write_field(address + offset, UINT32_MAX, pack_bytes(data + offset));
  }
  return true;
}

static bool power_verify_words(unsigned address, const uint8_t *data, size_t bytes,
    uint64_t deadline, const char *failure, const char **detail)
{
  for (size_t offset = 0; offset < bytes; offset += 4) {
    if (!(offset % 64) && !power_checkpoint(deadline, detail)) {
      return false;
    }
    if (!power_verify_field(address + offset, UINT32_MAX, pack_bytes(data + offset),
        failure, detail)) {
      return false;
    }
  }
  return true;
}

enum rtl8922ae_power_result rtl8922ae_power_apply(
    const struct rtl8922ae_power_image *image, uint64_t deadline,
    const char **detail)
{
  if (!detail) {
    return RTL8922AE_POWER_INVALID;
  }
  *detail = NULL;
  if (!image || !image->valid || !image->channel || image->channel > 13 ||
      image->region >= 16 || image->shape > 1) {
    *detail = "RTL8922AE power image invalid";
    return RTL8922AE_POWER_INVALID;
  }
  if (!power_checkpoint(deadline, detail)) {
    return RTL8922AE_POWER_READBACK;
  }
  for (size_t i = 0; i < sizeof(power_controls) / sizeof(power_controls[0]); ++i) {
    const struct power_control *control = &power_controls[i];
    power_write_field(control->address, control->mask, control->value);
  }
  /* Pinned onePHY BB-wrap defaults, 32 MACID entries. No MACID is allocated
   * or made eligible for packet TX here. */
  for (unsigned i = 0; i < 32; ++i) {
    if (!(i % 8) && !power_checkpoint(deadline, detail)) {
      return RTL8922AE_POWER_READBACK;
    }
    power_write_field(0x0ed00u + i * 4, UINT32_MAX, 0x03007f7f);
    power_write_field(0x0e500u + i * 4, UINT32_MAX, 0x03c86000);
  }
  if (!power_write_words(POWER_BYRATE_BASE, image->byrate, sizeof(image->byrate),
      deadline, detail)) {
    return RTL8922AE_POWER_READBACK;
  }
  power_write_field(POWER_RATE_OFFSETS, UINT32_MAX, image->rate_offsets);
  power_write_field(POWER_RATE_OFFSETS + 4, UINT32_MAX, 0);
  power_write_field(POWER_RATE_OFFSETS + 8, UINT32_MAX, 0);
  power_write_field(POWER_SHAPE, 3u, image->shape);
  for (unsigned ntx = 0; ntx < 2; ++ntx) {
    if (!power_write_words(POWER_NORMAL_BASE + ntx * RTL8922AE_POWER_LIMIT_BYTES,
        image->normal[ntx], sizeof(image->normal[ntx]), deadline, detail) ||
        !power_write_words(POWER_RU_BASE + ntx * RTL8922AE_POWER_RU_BYTES,
        image->ru[ntx], sizeof(image->ru[ntx]), deadline, detail)) {
      return RTL8922AE_POWER_READBACK;
    }
  }
  for (unsigned path = 0; path < 2; ++path) {
    if (!power_checkpoint(deadline, detail)) {
      return RTL8922AE_POWER_READBACK;
    }
    unsigned offset = path * POWER_PATH_STRIDE;
    uint32_t sar = (uint32_t)(uint16_t)image->sar_quarter_dbm[path] & 0x1ffu;
    power_write_field(POWER_PATH_REFERENCE + offset, POWER_PATH_REFERENCE_MASK, 0);
    power_write_field(POWER_PATH_TSSI_K + offset, POWER_TSSI_K_MASK,
        POWER_TSSI_K << 20);
    power_write_field(POWER_PATH_MAX + offset, POWER_SAR_MAX_MASK, sar << 12);
  }
  return RTL8922AE_POWER_OK;
}

/* Linux does not perform these readback comparisons. Pyxis retains this narrow
 * RF-safety gate: TX-capable RFK must not bypass the admitted SAR/regdb ceiling.
 * phy_be.c:323-344,361-378 defines the additive offsets and force selectors;
 * rtw8922a.c:2238-2274 installs the numeric normal/RU and per-path SAR limits.
 * rtw8922a.c:2147-2207 separately installs power references and TSSI K values.
 * Only enforcement fields are compared before and after RFK. Disabled
 * override values are irrelevant, so MACID forced-value bits are not checked. */
static const struct power_control power_limit_controls[] = {
  {0x11a28, 1u << 6, 0},
  {0x11a40, (1u << 29) | (1u << 16), 0},
  {0x11a44, (1u << 28) | (1u << 18) | 0x0003fe00u, 0},
  {0x11a48, (1u << 9) | (1u << 19), 0},
  {0x11a54, 0x38000000u, 0},
  {POWER_RATE_CONTROL, (1u << 19) | 0x1ffu, 0},
  {POWER_REFERENCE, 0x0ff80000u, 0},
  {0x11a24, 0x1ffu, 0},
  {0x11ae8, 0x0f000000u, 0},
};

enum rtl8922ae_power_result rtl8922ae_power_verify_limits(
    const struct rtl8922ae_power_image *image, uint64_t deadline,
    const char **detail)
{
  if (!detail) {
    return RTL8922AE_POWER_INVALID;
  }
  *detail = NULL;
  if (!image || !image->valid || !image->channel || image->channel > 13 ||
      image->region >= 16 || image->shape > 1) {
    *detail = "RTL8922AE power image invalid";
    return RTL8922AE_POWER_INVALID;
  }
  if (!power_checkpoint(deadline, detail)) {
    return RTL8922AE_POWER_READBACK;
  }
  for (size_t i = 0; i < sizeof(power_limit_controls) /
      sizeof(power_limit_controls[0]); ++i) {
    const struct power_control *control = &power_limit_controls[i];
    if (!power_verify_field(control->address, control->mask, control->value,
        "RTL8922AE power limit control readback mismatch", detail)) {
      return RTL8922AE_POWER_READBACK;
    }
  }
  if (!power_verify_field(POWER_RATE_OFFSETS, UINT32_MAX, image->rate_offsets,
      "RTL8922AE modulation power offset readback mismatch", detail)) {
    return RTL8922AE_POWER_READBACK;
  }
  for (unsigned ntx = 0; ntx < 2; ++ntx) {
    if (!power_verify_words(POWER_NORMAL_BASE + ntx * RTL8922AE_POWER_LIMIT_BYTES,
        image->normal[ntx], sizeof(image->normal[ntx]), deadline,
        "RTL8922AE normal power limit readback mismatch", detail) ||
        !power_verify_words(POWER_RU_BASE + ntx * RTL8922AE_POWER_RU_BYTES,
        image->ru[ntx], sizeof(image->ru[ntx]), deadline,
        "RTL8922AE RU power limit readback mismatch", detail)) {
      return RTL8922AE_POWER_READBACK;
    }
  }
  for (unsigned path = 0; path < 2; ++path) {
    if (!power_checkpoint(deadline, detail)) {
      return RTL8922AE_POWER_READBACK;
    }
    uint32_t sar = (uint32_t)(uint16_t)image->sar_quarter_dbm[path] & 0x1ffu;
    if (!power_verify_field(POWER_PATH_MAX + path * POWER_PATH_STRIDE,
        POWER_SAR_MAX_MASK, sar << 12,
        "RTL8922AE path SAR readback mismatch", detail)) {
      return RTL8922AE_POWER_READBACK;
    }
  }
  return RTL8922AE_POWER_OK;
}
