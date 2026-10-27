/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Native adaptation of Linux v6.18 rtw89 phy.c, phy_be.c, rtw8922a.c and
 * rtw8922a_rfk.c at 7d0a66e4bb9081d75c82ec4957c50034cb0ea449,
 * BSD alternative. Complete notice: kernel/wifi/NOTICE.
 */
#include "rtl8922ae_phy.h"
#include "rtl8922ae_io.h"
#include "rtl8922ae_regs_phy.h"
#include "rtl8922ae_transport.h"

#define PHY_BASE 0x20000u
#define BBMCU_BASE 0x30000u
#define BAR_BYTES 0x40000u
#define RF_MASK UINT32_C(0xfffff)
#define RF_ADSEL PHY_BIT(16)
#define PHY_TARGET_MASK UINT32_C(0x0fffffff)
#define PHY_BYPASS_DATA UINT32_C(0xbabecafe)
#define NS_PER_US UINT64_C(1000)
#define RF_PAGE_WORDS 500u
#define PHY_EDCCA_SCAN_MAX 249u
#define RF_MAX_PAGES 3u
#define RF_H2C_CATEGORY 2u
#define RF_H2C_CLASS_A 8u
#define RF_H2C_CLASS_B 9u
#define AFE_RECORD_BYTES 24u
#define TABLE_RECORD_BYTES 8u

struct phy_register {
  uint32_t address, mask, value;
};

struct phy_gain {
  uint8_t lna[2][7], tia[2][2], lna_op[2][7], tia_op[2][8];
  uint8_t rpl[2][15];
};

static bool channel_resume(uint64_t deadline, const char **detail);

/* The caller serializes all PHY operations with runtime firmware commands. */
static uint8_t rf_page[RF_PAGE_WORDS * sizeof(uint32_t)];

static bool fail(const char **detail, const char *message)
{
  if (detail) {
    *detail = message;
  }
  return false;
}

static uint32_t get32(const uint8_t *p)
{
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value)
{
  for (unsigned i = 0; i < 4; ++i) {
    p[i] = value >> (8 * i);
  }
}

static bool address_valid(uint32_t address)
{
  return !(address & 3) && address <= BAR_BYTES - sizeof(uint32_t);
}

static bool checkpoint(uint64_t deadline, const char **detail)
{
  return rtl8922ae_sleep(0, deadline) ||
      fail(detail, "RTL8922AE PHY operation interrupted or deadline expired");
}

static bool write_mask(uint32_t address, uint32_t mask, uint32_t value,
    const char **detail)
{
  if (!address_valid(address) || !mask) {
    return fail(detail, "RTL8922AE PHY register address or mask invalid");
  }
  uint32_t prior = 0;
  if (mask != UINT32_MAX) {
    prior = rtl8922ae_read32(address);
  }
  unsigned shift = __builtin_ctz(mask);
  rtl8922ae_write32(address, (prior & ~mask) | (value << shift & mask));
  return true;
}

static bool bb_mask(uint32_t address, uint32_t mask, uint32_t value,
    const char **detail)
{
  return address < PHY_BASE && write_mask(PHY_BASE + address, mask, value, detail);
}

static bool registers(const struct phy_register *registers, size_t count,
    bool bb, uint64_t deadline, const char **detail)
{
  for (size_t i = 0; i < count; ++i) {
    if (!(i % 32) && !checkpoint(deadline, detail)) {
      return false;
    }
    uint32_t address = registers[i].address;
    if (bb) {
      if (address >= PHY_BASE) {
        return fail(detail, "RTL8922AE PHY register offset invalid");
      }
      address += PHY_BASE;
    }
    if (!write_mask(address, registers[i].mask, registers[i].value, detail)) {
      return false;
    }
  }
  return true;
}

static bool table_span(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, unsigned id,
    const uint8_t **data, size_t *records, const char **detail)
{
  if (!asset || !tables || id >= RTL8922AE_ELEMENT_COUNT) {
    return fail(detail, "RTL8922AE PHY table input invalid");
  }
  const struct rtl8922ae_firmware_table *table = &tables->table[id];
  if (tables->live_cut != 2 || tables->live_rfe != 1 ||
      !table->selected || table->offset > bytes || table->bytes > bytes - table->offset ||
      !table->bytes || table->bytes % TABLE_RECORD_BYTES ||
      table->records != table->bytes / TABLE_RECORD_BYTES ||
      table->headline_records > table->records) {
    return fail(detail, "RTL8922AE PHY table missing or malformed");
  }
  if ((id == RTL8922AE_ELEMENT_BB || id == RTL8922AE_ELEMENT_RADIO_A ||
      id == RTL8922AE_ELEMENT_RADIO_B) &&
      (!table->headline_exact || table->headline_target != UINT32_C(0x10002))) {
    return fail(detail, "RTL8922AE exact PHY headline for cut/RFE unavailable");
  }
  *data = (const uint8_t *)asset + table->offset;
  *records = table->records;
  return true;
}

static bool delay_record(uint32_t address, uint64_t *nanoseconds)
{
  switch (address) {
  case 0xfe: *nanoseconds = UINT64_C(50000) * NS_PER_US; return true;
  case 0xfd: *nanoseconds = UINT64_C(5000) * NS_PER_US; return true;
  case 0xfc: *nanoseconds = UINT64_C(1000) * NS_PER_US; return true;
  case 0xfb: *nanoseconds = UINT64_C(50) * NS_PER_US; return true;
  case 0xfa: *nanoseconds = UINT64_C(5) * NS_PER_US; return true;
  case 0xf9: *nanoseconds = NS_PER_US; return true;
  default: return false;
  }
}

enum table_operation { TABLE_BB, TABLE_RADIO, TABLE_UPLOAD, TABLE_GAIN };

static bool gain_record(struct phy_gain *gain, uint32_t address, uint32_t value,
    const char **detail)
{
  unsigned type = address & 0xff;
  unsigned path = address >> 8 & 0xf;
  unsigned bandwidth = address >> 12 & 0xf;
  unsigned band = address >> 16 & 0xff;
  unsigned configuration = address >> 24;
  if (path >= 2 || bandwidth >= 2 || band >= 12 ||
      (address >= 0xf9 && address <= 0xfe)) {
    return fail(detail, "RTL8922AE BB gain record unsupported");
  }
  /* Other bands and wide-band LNA/TIA data are not used by this profile.
   * RPL offsets for all widths are still programmed by the reference. */
  if (band || (configuration != 1 && bandwidth)) {
    return true;
  }
  uint8_t *destination;
  unsigned offset, count;
  switch (configuration) {
  case 0:
    if (type > 2) {
      return fail(detail, "RTL8922AE BB gain error type unsupported");
    }
    destination = type == 2 ? gain->tia[path] : gain->lna[path];
    offset = type == 1 ? 4 : 0;
    count = type == 0 ? 4 : type == 1 ? 3 : 2;
    break;
  case 1: {
    unsigned width = type >> 4, part = type & 0xf;
    if (width > 3 || (width == 3 && part > 1)) {
      return fail(detail, "RTL8922AE BB RPL offset type unsupported");
    }
    static const uint8_t offsets[] = {0, 1, 3, 7};
    destination = gain->rpl[path];
    offset = offsets[width] + (width == 3 && part ? 4 : 0);
    count = width == 0 ? 1 : width == 1 ? 2 : 4;
    break;
  }
  case 2:
  case 4:
    /* Reference ignores gain bypass and RFE<50 external-FEM data. */
    return true;
  case 3:
    if (type > 3) {
      return fail(detail, "RTL8922AE BB gain OP1dB type unsupported");
    }
    destination = type >= 2 ? gain->tia_op[path] : gain->lna_op[path];
    offset = type == 1 || type == 3 ? 4 : 0;
    count = type == 1 ? 3 : 4;
    break;
  default:
    return fail(detail, "RTL8922AE BB gain configuration unsupported");
  }
  for (unsigned i = 0; i < count; ++i) {
    destination[offset + i] = value >> (8 * i);
  }
  return true;
}

static bool apply_table(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, unsigned id,
    enum table_operation operation, unsigned path, struct phy_gain *gain,
    uint64_t deadline, const char **detail)
{
  const uint8_t *data;
  size_t count;
  if (!table_span(asset, bytes, tables, id, &data, &count, detail)) {
    return false;
  }
  const struct rtl8922ae_firmware_table *table = &tables->table[id];
  bool matched = true, found = false;
  uint32_t target = 0;
  unsigned page = 0, page_words = 0;
  /* Linux stops this path's upload on error but only warns. Still validate
   * later records; checkpoint any sticky transport fault before buffer reuse. */
  bool upload_failed = false;
  for (size_t i = table->headline_records; i < count; ++i) {
    if (!(i % 32) && !checkpoint(deadline, detail)) {
      return false;
    }
    uint32_t address = get32(data + i * TABLE_RECORD_BYTES);
    uint32_t value = get32(data + i * TABLE_RECORD_BYTES + 4);
    switch (address >> 28) {
    case 8:
    case 9:
      target = address & PHY_TARGET_MASK;
      continue;
    case 10:
      matched = false;
      if (!found) {
        return fail(detail, "RTL8922AE PHY conditional table has no matching branch");
      }
      continue;
    case 11:
      matched = true;
      found = false;
      continue;
    case 4:
      matched = !found && target == table->headline_target;
      if (matched) {
        found = true;
      }
      continue;
    default:
      break;
    }
    if (!matched) {
      continue;
    }
    uint64_t delay;
    if (delay_record(address, &delay)) {
      if (operation != TABLE_UPLOAD && operation != TABLE_GAIN &&
          !rtl8922ae_sleep(delay, deadline)) {
        return fail(detail, "RTL8922AE PHY table delay interrupted");
      }
      if (operation == TABLE_GAIN) {
        return fail(detail, "RTL8922AE BB gain table contains delay directive");
      }
      continue;
    }
    switch (operation) {
    case TABLE_BB:
      if (value != PHY_BYPASS_DATA && !bb_mask(address, UINT32_MAX, value, detail)) {
        return false;
      }
      break;
    case TABLE_RADIO:
      if (path >= 2 || (address & ~(RF_ADSEL | UINT32_C(0xff)))) {
        return fail(detail, "RTL8922AE RF write input invalid");
      }
      /* The reference RF wrapper discards the primitive write result. */
      rtl8922ae_phy_rf_write(path, address, RF_MASK, value, deadline, detail);
      if (!checkpoint(deadline, detail)) return false;
      break;
    case TABLE_UPLOAD:
      /* Firmware-derived tables use the reference v1 executor: analog
       * entries are written directly, only digital entries are uploaded. */
      if (address < 0x100u) break;
      if (page >= RF_MAX_PAGES) {
        return fail(detail, "RTL8922AE RF table exceeds firmware upload capacity");
      }
      put32(rf_page + page_words * sizeof(uint32_t), address << 20 | value);
      if (++page_words == RF_PAGE_WORDS) {
        if (!upload_failed) {
          upload_failed = !rtl8922ae_transport_h2c(RF_H2C_CATEGORY,
              path ? RF_H2C_CLASS_B : RF_H2C_CLASS_A, page, false, false,
              rf_page, sizeof(rf_page), NULL, deadline, detail);
          if (!checkpoint(deadline, detail)) return false;
        }
        ++page;
        page_words = 0;
      }
      break;
    case TABLE_GAIN:
      if (!gain_record(gain, address, value, detail)) {
        return false;
      }
      break;
    }
  }
  if (operation == TABLE_UPLOAD && page_words && !upload_failed) {
    rtl8922ae_transport_h2c(RF_H2C_CATEGORY, path ? RF_H2C_CLASS_B : RF_H2C_CLASS_A,
        page, false, false,
        rf_page, page_words * sizeof(uint32_t), NULL, deadline, detail);
  }
  return checkpoint(deadline, detail);
}

bool rtl8922ae_phy_rf_read(unsigned path, uint32_t address, uint32_t mask,
    uint32_t *value, uint64_t deadline, const char **detail)
{
  if (path >= 2 || !value || !mask || (mask & ~RF_MASK) ||
      (address & ~(RF_ADSEL | UINT32_C(0xff))) || !checkpoint(deadline, detail)) {
    return fail(detail, "RTL8922AE RF read input invalid or interrupted");
  }
  if (address & RF_ADSEL) {
    uint32_t raw = rtl8922ae_read32(PHY_BASE + (path ? 0xf000 : 0xe000) +
        (address & 0xff) * 4);
    *value = (raw & mask) >> __builtin_ctz(mask);
    return true;
  }
  uint32_t control = PHY_BASE + (path ? R_HWSI_ADD1 : R_HWSI_ADD0);
  uint32_t status = PHY_BASE + (path ? R_HWSI_VAL1 : R_HWSI_VAL0);
  uint64_t local = rtl8922ae_deadline(deadline, UINT64_C(3800) * NS_PER_US);
  if (!write_mask(control, B_HWSI_ADD_CTL_MASK, 1, detail) ||
      !rtl8922ae_poll32(status, B_HWSI_VAL_BUSY, 0, local) ||
      !write_mask(control, B_HWSI_ADD_MASK, address, detail) ||
      !write_mask(control, B_HWSI_ADD_RD, 1, detail) ||
      !rtl8922ae_sleep(2 * NS_PER_US, deadline)) {
    return fail(detail, "RTL8922AE RF HWSI read launch failed");
  }
  local = rtl8922ae_deadline(deadline, UINT64_C(3800) * NS_PER_US);
  bool ready = rtl8922ae_poll32(status, B_HWSI_VAL_RDONE, B_HWSI_VAL_RDONE, local);
  uint32_t raw = rtl8922ae_read32(status);
  bool cleared = write_mask(control, B_HWSI_ADD_POLL_MASK, 0, detail);
  if (!ready || !cleared) {
    return fail(detail, "RTL8922AE RF HWSI read completion unconfirmed");
  }
  *value = (raw & mask) >> __builtin_ctz(mask);
  return true;
}

bool rtl8922ae_phy_rf_write(unsigned path, uint32_t address, uint32_t mask,
    uint32_t value, uint64_t deadline, const char **detail)
{
  if (path >= 2 || !mask || (mask & ~RF_MASK) ||
      (address & ~(RF_ADSEL | UINT32_C(0xff))) || !checkpoint(deadline, detail)) {
    return fail(detail, "RTL8922AE RF write input invalid or interrupted");
  }
  if (address & RF_ADSEL) {
    if (!write_mask(PHY_BASE + (path ? 0xf000 : 0xe000) +
        (address & 0xff) * 4, mask, value, detail)) {
      return false;
    }
    return rtl8922ae_sleep(NS_PER_US, deadline) ||
        fail(detail, "RTL8922AE direct RF write delay interrupted");
  }
  uint32_t raw = value;
  if (mask != RF_MASK) {
    /* Linux merges INV_RF_DATA on a timed-out read before the busy-gated write. */
    raw = UINT32_MAX;
    rtl8922ae_phy_rf_read(path, address, RF_MASK, &raw, deadline, detail);
    if (!checkpoint(deadline, detail)) return false;
    raw = (raw & ~mask) | (value << __builtin_ctz(mask) & mask);
  }
  uint32_t status = PHY_BASE + (path ? R_HWSI_VAL1 : R_HWSI_VAL0);
  uint64_t local = rtl8922ae_deadline(deadline, UINT64_C(3800) * NS_PER_US);
  if (!rtl8922ae_poll32(status, B_HWSI_VAL_BUSY, 0, local)) {
    return fail(detail, "RTL8922AE RF HWSI write busy");
  }
  rtl8922ae_write32(PHY_BASE + R_HWSI_DATA + path * 0x100,
      (raw & RF_MASK) << 8 | (address & 0xff));
  return true;
}

bool rtl8922ae_phy_afe(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, uint64_t deadline,
    const char **detail)
{
  if (!asset || !tables) {
    return fail(detail, "RTL8922AE AFE input invalid");
  }
  const struct rtl8922ae_firmware_table *table = &tables->table[RTL8922AE_ELEMENT_AFE];
  if (!(tables->observed_mask & RTL8922AE_ELEMENT_BIT(RTL8922AE_ELEMENT_AFE))) {
    return checkpoint(deadline, detail);
  }
  if (!table->selected || table->offset > bytes || table->bytes > bytes - table->offset ||
      table->bytes % AFE_RECORD_BYTES) {
    return fail(detail, "RTL8922AE AFE table unsupported or malformed");
  }
  for (size_t offset = 0; offset < table->bytes; offset += AFE_RECORD_BYTES) {
    if (!checkpoint(deadline, detail)) {
      return false;
    }
    const uint8_t *record = (const uint8_t *)asset + table->offset + offset;
    uint32_t action = get32(record), category = get32(record + 4);
    uint32_t class = get32(record + 8), address = get32(record + 12);
    uint32_t mask = get32(record + 16), value = get32(record + 20);
    if (action > 2 || category > 5 || class > 5) {
      return fail(detail, "RTL8922AE AFE directive unsupported");
    }
    if (class >= 2 && class <= 4) {
      continue;
    }
    if (action == 1) {
      if (!rtl8922ae_sleep((uint64_t)address * NS_PER_US, deadline)) {
        return fail(detail, "RTL8922AE AFE delay interrupted");
      }
    } else if (action == 0) {
      /* With DBCC disabled, the reference BB1 helper also targets PHY0. */
      if (category <= 1) {
        if (address >= PHY_BASE) {
          return fail(detail, "RTL8922AE AFE BB offset invalid");
        }
        address += PHY_BASE;
      }
      if (!write_mask(address, mask, value, detail)) {
        return false;
      }
    } else {
      /* AFE polling intentionally uses the direct BAR address for every cat. */
      if (!address_valid(address) || !mask) {
        return fail(detail, "RTL8922AE AFE poll address or mask invalid");
      }
      for (unsigned attempt = 0; attempt <= 10; ++attempt) {
        uint32_t raw = rtl8922ae_read32(address);
        if ((raw & mask) >> __builtin_ctz(mask) == value) {
          break;
        }
        if (!rtl8922ae_sleep(NS_PER_US, deadline)) {
          return fail(detail, "RTL8922AE AFE poll interrupted");
        }
      }
      /* Linux rtw89_phy_init_bb_afe only warns after these attempts. */
    }
  }
  return true;
}

bool rtl8922ae_phy_preinit(uint64_t deadline, const char **detail)
{
  static const struct phy_register controls[] = {
    {R_BE_DMAC_SYS_CR32B, B_BE_DMAC_BB_PHY0_MASK, 0x7ff9},
    {R_BE_FEN_RST_ENABLE, B_BE_FEN_BB_IP_RSTN, 0},
    {R_BE_FEN_RST_ENABLE, B_BE_FEN_BBPLAT_RSTB, 0},
    {R_BE_FEN_RST_ENABLE, B_BE_FEN_BB_IP_RSTN, 1},
    {R_BE_FEN_RST_ENABLE, B_BE_BOOT_RDY0, 0},
    {R_BE_MEM_PWR_CTRL, B_BE_MEM_BBMCU0_DS_V1, 0},
  };
  static const struct phy_register mcu[] = {
    {BBMCU_BASE + 0x6990, UINT32_MAX, 0},
    {BBMCU_BASE + 0x6994, UINT32_MAX, 0},
    {BBMCU_BASE + 0x6998, UINT32_MAX, 0},
    {BBMCU_BASE + 0x6820, UINT32_MAX, 0xfffffffe},
    {BBMCU_BASE + 0x6800, UINT32_MAX, 0xc0000ffe},
    {BBMCU_BASE + 0x6808, UINT32_MAX, 0x76543210},
    {BBMCU_BASE + 0x6814, UINT32_MAX, 0xbfbfb000},
    {BBMCU_BASE + 0x6818, UINT32_MAX, 0x0478c009},
    {BBMCU_BASE + 0x6800, UINT32_MAX, 0xc0000fff},
    {BBMCU_BASE + 0x6820, UINT32_MAX, 0xffffffff},
  };
  return registers(controls, sizeof(controls) / sizeof(*controls), false, deadline, detail) &&
      rtl8922ae_sleep(NS_PER_US, deadline) &&
      registers(mcu, sizeof(mcu) / sizeof(*mcu), false, deadline, detail);
}

bool rtl8922ae_phy_enable(uint64_t deadline, const char **detail)
{
  /* rtw8922a_mac_enable_bb_rf: the post-download full write is distinct
   * from the partial PHY0 pre-download setup. */
  if (!checkpoint(deadline, detail)) return false;
  uint8_t reset = rtl8922ae_read8(R_BE_FEN_RST_ENABLE);
  reset |= B_BE_FEN_BBPLAT_RSTB | B_BE_FEN_BB_IP_RSTN;
  rtl8922ae_write8(R_BE_FEN_RST_ENABLE, reset);
  return write_mask(R_BE_DMAC_SYS_CR32B, UINT32_MAX, UINT32_C(0x7ff97ff9), detail);
}

bool rtl8922ae_phy_bb(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, uint64_t deadline,
    const char **detail)
{
  struct phy_gain gain = {0};
  return apply_table(asset, bytes, tables, RTL8922AE_ELEMENT_BB,
      TABLE_BB, 0, NULL, deadline, detail) &&
      apply_table(asset, bytes, tables, RTL8922AE_ELEMENT_BB_GAIN,
          TABLE_GAIN, 0, &gain, deadline, detail);
}

bool rtl8922ae_phy_postinit(uint64_t deadline, const char **detail)
{
  if (!checkpoint(deadline, detail) ||
      !write_mask(R_BE_FEN_RST_ENABLE, B_BE_BOOT_RDY0, 1, detail) ||
      !write_mask(R_BE_FEN_RST_ENABLE, B_BE_FEN_BBPLAT_RSTB, 1, detail)) {
    return false;
  }
  static const struct phy_register settings[] = {
    {R_BBCLK, B_CLK_640M, 1},
    {R_TXSCALE, B_TXFCTR_EN, 0},
    {R_TXFCTR, B_TXFCTR_THD, 0x200},
    {R_SLOPE, B_EHT_RATE_TH, 0xa},
    {R_BEDGE, B_HE_RATE_TH, 0xa},
    {R_BEDGE2, B_HT_VHT_TH, 0xaaa},
    {R_BEDGE, B_EHT_MCS14, 1},
    {R_BEDGE2, B_EHT_MCS15, 1},
    {R_BEDGE3, B_EHTTB_EN, 0},
    {R_BEDGE3, B_HEERSU_EN, 0},
    {R_BEDGE3, B_HEMU_EN, 0},
    {R_BEDGE3, B_TB_EN, 0},
    {R_SU_PUNC, B_SU_PUNC_EN, 1},
    {R_BEDGE5, B_HWGEN_EN, 1},
    {R_BEDGE5, B_PWROFST_COMP, 1},
    {R_MAG_AB, B_BY_SLOPE, 1},
    {R_MAG_A, B_MGA_AEND, 0xe0},
    {R_MAG_AB, B_MAG_AB, 0xe0c000},
    {R_SLOPE, B_SLOPE_A, 0x3fe0},
    {R_SLOPE, B_SLOPE_B, 0x3fe0},
    {R_SC_CORNER, B_SC_CORNER, 0x200},
    {R_UDP_COEEF, B_UDP_COEEF, 0},
    {R_UDP_COEEF, B_UDP_COEEF, 1},
  };
  if (!registers(settings, sizeof(settings) / sizeof(*settings), true, deadline, detail)) {
    return false;
  }
  return true;
}

bool rtl8922ae_phy_radio(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables, uint64_t deadline,
    const char **detail)
{
  unsigned a = tables->table[RTL8922AE_ELEMENT_RADIO_A].radio_index;
  unsigned b = tables->table[RTL8922AE_ELEMENT_RADIO_B].radio_index;
  if (a > 1 || b > 1 || a == b) {
    return fail(detail, "RTL8922AE RF table execution order unsupported");
  }
  for (unsigned index = 0; index < 2; ++index) {
    unsigned path = index == a ? 0 : 1;
    unsigned id = path ? RTL8922AE_ELEMENT_RADIO_B : RTL8922AE_ELEMENT_RADIO_A;
    if (!apply_table(asset, bytes, tables, id, TABLE_RADIO, path, NULL, deadline, detail) ||
        !apply_table(asset, bytes, tables, id, TABLE_UPLOAD, path, NULL, deadline, detail)) {
      return false;
    }
  }
  return true;
}

static bool configure_paths(const struct rtl8922ae_board *board,
    uint64_t deadline, const char **detail)
{
  if (board->tx_nss != 2 || board->rx_nss != 2 ||
      board->tx_antennas != 2 || board->rx_antennas != 2) {
    return fail(detail, "RTL8922AE one-PHY two-path capability profile unsupported");
  }
  static const struct phy_register paths[] = {
    {R_MAC_SEL, B_MAC_SEL, 0},
  };
  static const struct phy_register tmac[] = {
    {0x11a00, UINT32_MAX, 0x21c86900},
    {0x11a04, UINT32_MAX, 0x00e4e433},
    {0x11a08, UINT32_MAX, 0x39390cc9},
    {0x11a0c, UINT32_MAX, 0x4e433240},
    {0x11a10, UINT32_MAX, 0x90cc900e},
    {0x11a14, UINT32_MAX, 0x00240393},
    {0x11a18, UINT32_MAX, 0x201c8600},
  };
  static const struct phy_register receive[] = {
    {R_ANT_CHBW, B_ANT_RX_SG0, 0},
    {R_ANT_CHBW, B_ANT_RX_SG0, 3},
    {R_FC0INV_SBW, B_RX_1RCCA, 3},
    {R_BRK_R, B_HTMCS_LMT, 1},
    {R_BRK_R, B_VHTMCS_LMT, 1},
    {R_BRK_HE, B_N_USR_MAX, 4},
    {R_BRK_HE, B_NSS_MAX, 1},
    {R_BRK_HE, B_TB_NSS_MAX, 1},
    {R_BRK_EHT, B_RXEHT_NSS_MAX, 1},
    {R_BRK_RXEHT, B_RXEHTTB_NSS_MAX, 1},
    {R_BRK_RXEHT, B_RXEHT_N_USER_MAX, 4},
    {R_TXPWR_RSTA, B_TXPWR_RSTA, 0},
    {R_TXPWR_RSTA, B_TXPWR_RSTA, 1},
    {R_TXPWR_RSTB, B_TXPWR_RSTB, 0},
    {R_TXPWR_RSTB, B_TXPWR_RSTB, 1},
  };
  return registers(paths, sizeof(paths) / sizeof(*paths), true, deadline, detail) &&
      registers(tmac, sizeof(tmac) / sizeof(*tmac), false, deadline, detail) &&
      registers(receive, sizeof(receive) / sizeof(*receive), true, deadline, detail);
}

static bool static_receive_init(const struct rtl8922ae_board *board,
    uint64_t deadline, const char **detail)
{
  /* CCX's initial software unit is 32 us: 64,256,1024,4096 us become
   * 2,8,32,128. Low thresholds are the previous high threshold plus one. */
  static const struct phy_register environment[] = {
    {R_CCX, B_CCX_EN_MSK, 1},
    {R_CCX, B_CCX_TRIG_OPT_MSK, 1},
    {R_CCX, B_MEASUREMENT_TRIG_MSK, 1},
    {R_CCX, B_CCX_EDCCA_OPT_MSK_V1, 0},
    {R_IFS_T1, B_IFS_T1_TH_LOW_MSK, 0},
    {R_IFS_T2, B_IFS_T2_TH_LOW_MSK, 3},
    {R_IFS_T3, B_IFS_T3_TH_LOW_MSK, 9},
    {R_IFS_T4, B_IFS_T4_TH_LOW_MSK, 33},
    {R_IFS_T1, B_IFS_T1_TH_HIGH_MSK, 2},
    {R_IFS_T2, B_IFS_T2_TH_HIGH_MSK, 8},
    {R_IFS_T3, B_IFS_T3_TH_HIGH_MSK, 32},
    {R_IFS_T4, B_IFS_T4_TH_HIGH_MSK, 128},
    {R_IFS_COUNTER, B_IFS_COLLECT_EN, 1},
    {R_IFS_T1, B_IFS_T1_EN_MSK, 1},
    {R_IFS_T2, B_IFS_T2_EN_MSK, 1},
    {R_IFS_T3, B_IFS_T3_EN_MSK, 1},
    {R_IFS_T4, B_IFS_T4_EN_MSK, 1},
    {R_PLCP_HISTOGRAM, B_STS_DIS_TRIG_BY_FAIL, 1},
    {R_PLCP_HISTOGRAM, B_STS_DIS_TRIG_BY_BRK, 1},
  };
  if (!registers(environment, sizeof(environment) / sizeof(*environment), true, deadline, detail)) {
    return false;
  }
  enum {
    STATUS_HE_MU = 6, STATUS_VHT_MU = 7, STATUS_RESERVED = 9,
    STATUS_TRIGGER = 10, STATUS_CCK = 11, STATUS_HT = 13, STATUS_EHT = 16,
    STATUS_IE_OFDM = 1, STATUS_IE_PATH_A = 4, STATUS_IE_PATH_D = 7,
    STATUS_IE_DL_MU = 13, STATUS_IE_DEBUG_OFDM = 20,
  };
  for (unsigned page = 0; page <= STATUS_EHT; ++page) {
    if (page == STATUS_RESERVED) {
      continue;
    }
    uint32_t address = page == STATUS_EHT ? R_PHY_STS_BITMAP_EHT :
        R_PHY_STS_BITMAP_ADDR_START + (page > STATUS_RESERVED ? page - 1 : page) * 4;
    uint32_t value = rtl8922ae_read32(PHY_BASE + address);
    if (page == STATUS_HE_MU || page == STATUS_VHT_MU) {
      value |= PHY_BIT(STATUS_IE_DL_MU);
    } else if (page == STATUS_TRIGGER) {
      value |= PHY_BIT(STATUS_IE_DL_MU) | PHY_BIT(STATUS_IE_OFDM);
    } else if (page >= STATUS_CCK) {
      value &= ~PHY_MASK(STATUS_IE_PATH_D, STATUS_IE_PATH_A);
      if (page == STATUS_CCK) {
        value |= PHY_BIT(STATUS_IE_OFDM);
      } else if (page >= STATUS_HT) {
        value |= PHY_BIT(STATUS_IE_DEBUG_OFDM);
      }
    }
    if (!bb_mask(address, UINT32_MAX, value, detail) || !checkpoint(deadline, detail)) {
      return false;
    }
  }
  /* RTL8922A has no host-controlled IGI. DIG still resets PD thresholds and
   * disables SDAGC following PAGC. Initial unlinked igi_rssi is zero; the
   * disabled CCK threshold is therefore -16 - 110 = -126 in reference units. */
  static const struct phy_register dig[] = {
    {R_SEG0R_PD_V2, B_SEG0R_PD_LOWER_BOUND_MSK, 0},
    {R_SEG0R_PD_V2, B_SEG0R_PD_SPATIAL_REUSE_EN_MSK_V1, 0},
    {R_BMODE_PDTH_EN_V2, B_BMODE_PDTH_LIMIT_EN_MSK_V1, 0},
    {R_BMODE_PDTH_V2, B_BMODE_PDTH_LOWER_BOUND_MSK_V1, (uint32_t)-126},
    {R_PATH0_P20_FOLLOW_BY_PAGCUGC_V3, B_PATH0_P20_FOLLOW_BY_PAGCUGC_EN_MSK, 0},
    {R_PATH0_S20_FOLLOW_BY_PAGCUGC_V3, B_PATH0_S20_FOLLOW_BY_PAGCUGC_EN_MSK, 0},
    {R_PATH1_P20_FOLLOW_BY_PAGCUGC_V3, B_PATH1_P20_FOLLOW_BY_PAGCUGC_EN_MSK, 0},
    {R_PATH1_S20_FOLLOW_BY_PAGCUGC_V3, B_PATH1_S20_FOLLOW_BY_PAGCUGC_EN_MSK, 0},
  };
  if (!registers(dig, sizeof(dig) / sizeof(*dig), true, deadline, detail)) {
    return false;
  }
  uint8_t cap = board->xtal & B_AX_XTAL_SC_MASK;
  /* Linux CFO initialization ignores serial-write errors for these caps. */
  rtl8922ae_hw_xtal_write(XTAL_SI_XTAL_SC_XO, cap, 0xff, deadline);
  if (!checkpoint(deadline, detail)) return false;
  rtl8922ae_hw_xtal_write(XTAL_SI_XTAL_SC_XI, cap, 0xff, deadline);
  if (!checkpoint(deadline, detail)) return false;
  static const struct phy_register receive[] = {
    {R_DCFO_OPT_V1, B_DCFO_OPT_EN_V1, 1},
    {R_DCFO_WEIGHT_V1, B_DCFO_WEIGHT_MSK_V1, 8},
    {R_TX_COLLISION_T2R_ST_BE, B_TX_COLLISION_T2R_ST_BE_M, 0x29},
    {R_CHINFO_SEG, B_CHINFO_SEG_LEN, 0},
    {R_CHINFO_SEG, B_CHINFO_SEG, 0xf},
    {R_CHINFO_DATA, B_CHINFO_DATA_BITMAP, 1},
    {R_CHINFO_ELM_SRC, B_CHINFO_ELM_BITMAP, 0x40303},
    {R_CHINFO_ELM_SRC, B_CHINFO_SRC, 0},
    {R_CHINFO_TYPE_SCAL, B_CHINFO_TYPE, 3},
    {R_CHINFO_TYPE_SCAL, B_CHINFO_SCAL, 0},
  };
  return registers(receive, sizeof(receive) / sizeof(*receive), true, deadline, detail);
}

/* Like rtw8922a_rfk_pll_init, a serial failure ends only this local sequence. */
static void pll_init(uint64_t deadline)
{
  uint8_t value;
  if (!rtl8922ae_hw_xtal_read(XTAL_SI_PLL_1, &value, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_PLL_1, value | 0xf8, 0xff, deadline) ||
      !rtl8922ae_hw_xtal_read(XTAL_SI_APBT, &value, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_APBT, value & ~0x60, 0xff, deadline) ||
      !rtl8922ae_hw_xtal_read(XTAL_SI_XTAL_PLL, &value, deadline) ||
      !rtl8922ae_hw_xtal_write(XTAL_SI_XTAL_PLL, value | 0x38, 0xff, deadline)) {
    return;
  }
}

bool rtl8922ae_phy_hardware(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_board *board, uint64_t deadline, const char **detail)
{
  if (!board || !tables || board->rfe != tables->live_rfe ||
      tables->live_cut != 2 || board->rfe != 1) {
    return fail(detail, "RTL8922AE PHY board/cut profile unsupported");
  }
  static const struct phy_register hardware[] = {
    {R_EN_SND_WO_NDP, B_EN_SND_WO_NDP, 0},
    {R_EN_SND_WO_NDP_C1, B_EN_SND_WO_NDP, 0},
    {R_DBCC, B_DBCC_EN, 0},
    {R_DBCC_FA, B_DBCC_FA, 1},
    {R_AFEDAC0, B_AFEDAC0, 0xe},
    {R_AFEDAC1, B_AFEDAC1, 7},
    {R_AFEDAC0 + 0x100, B_AFEDAC0, 0xe},
    {R_AFEDAC1 + 0x100, B_AFEDAC1, 7},
    /* Reference single active PHY0 mode MLO_2_PLUS_0_1RF. */
    {R_EMLSR, B_EMLSR_PARM, 0x6180},
    {R_EMLSR, B_EMLSR_PARM, 0xbbab},
    {R_EMLSR, B_EMLSR_PARM, 0xaba9},
    {R_EMLSR, B_EMLSR_PARM, 0xeba9},
    {R_EMLSR, B_EMLSR_PARM, 0xeaa9},
  };
  if (!registers(hardware, sizeof(hardware) / sizeof(*hardware), true, deadline, detail) ||
      !write_mask(R_BE_PWR_BOOST, B_BE_PWR_CTRL_SEL, 0, detail) ||
      !static_receive_init(board, deadline, detail)) {
    return false;
  }
  pll_init(deadline);
  if (!checkpoint(deadline, detail)) return false;
  static const struct phy_register nctl[] = {
    {R_GOTX_IQKDPK_C0, B_GOTX_IQKDPK, 3},
    {R_GOTX_IQKDPK_C1, B_GOTX_IQKDPK, 3},
    {R_IQKDPK_HC, B_IQKDPK_HC, 1},
    {R_CLK_GCK, B_CLK_GCK, 0x00fffff},
    {R_IOQ_IQK_DPK, B_IOQ_IQK_DPK_CLKEN, 3},
    {R_IQK_DPK_RST, B_IQK_DPK_RST, 1},
    {R_IQK_DPK_PRST, B_IQK_DPK_PRST, 1},
    {R_IQK_DPK_PRST_C1, B_IQK_DPK_PRST, 1},
    {R_TXRFC, B_TXRFC_RST, 1},
  };
  if (!registers(nctl, sizeof(nctl) / sizeof(*nctl), true, deadline, detail) ||
      !apply_table(asset, bytes, tables, RTL8922AE_ELEMENT_NCTL,
          TABLE_BB, 0, NULL, deadline, detail)) {
    return false;
  }
  if (board->pa_bias_present) {
    for (unsigned path = 0; path < 2; ++path) {
      rtl8922ae_phy_rf_write(path, RR_BIASA, RR_BIASA_TXG_V1,
          board->pa_bias[path] & 0xf, deadline, detail);
      if (!checkpoint(deadline, detail)) return false;
      rtl8922ae_phy_rf_write(path, RR_BIASA, RR_BIASA_TXA_V1,
          board->pa_bias[path] >> 4, deadline, detail);
      if (!checkpoint(deadline, detail)) return false;
    }
    for (unsigned path = 0; path < 2; ++path) {
      rtl8922ae_phy_rf_write(path, RR_BIASA, RR_BIASD_TXG_V1,
          board->pad_bias[path] & 0xf, deadline, detail);
      if (!checkpoint(deadline, detail)) return false;
      rtl8922ae_phy_rf_write(path, RR_BIASA, RR_BIASD_TXA_V1,
          board->pad_bias[path] >> 4, deadline, detail);
      if (!checkpoint(deadline, detail)) return false;
    }
  }
  return rtl8922ae_phy_channel_enter(deadline, detail) &&
      configure_paths(board, deadline, detail) && channel_resume(deadline, detail);
}

static bool apply_gain(const struct phy_gain *gain, uint64_t deadline,
    const char **detail)
{
  static const uint32_t lna_address[] = {0x409c, 0x409c, 0x40a0, 0x40a0,
      0x40a4, 0x40a4, 0x40a8};
  static const uint32_t lna_mask[] = {0xff00, 0xff000000, 0xff00, 0xff000000,
      0xff00, 0xff000000, 0xff00};
  static const uint32_t lna_op_address[] = {0x40ac, 0x40ac, 0x40ac, 0x40b0,
      0x40b0, 0x40b0, 0x40b0};
  static const uint32_t lna_op_mask[] = {0xff00, 0xff0000, 0xff000000, 0xff,
      0xff00, 0xff0000, 0xff000000};
  static const uint32_t tia_op_address[] = {0x40b4, 0x40b4, 0x40b8, 0x40b8,
      0x40b8, 0x40b8, 0x40bc, 0x40bc};
  static const uint32_t tia_op_mask[] = {0xff0000, 0xff000000, 0xff, 0xff00,
      0xff0000, 0xff000000, 0xff, 0xff00};
  /* RPL order matches the packed BE table: 20, 40[2], 80[4], 160[8]. */
  static const uint32_t rpl_address[] = {0x41f0, 0x41f0, 0x41f0,
      0x41f4, 0x41f4, 0x41f4, 0x41f4, 0x41e8, 0x41e8, 0x41e8,
      0x41ec, 0x41ec, 0x41ec, 0x41ec, 0x41f0};
  static const uint32_t rpl_mask[] = {0xff00, 0xff0000, 0xff000000,
      0xff, 0xff00, 0xff0000, 0xff000000, 0xff00, 0xff0000, 0xff000000,
      0xff, 0xff00, 0xff0000, 0xff000000, 0xff};
  for (unsigned path = 0; path < 2; ++path) {
    if (!checkpoint(deadline, detail)) {
      return false;
    }
    uint32_t offset = path * 0x400;
    for (unsigned i = 0; i < 7; ++i) {
      if (!bb_mask(lna_address[i] + offset, lna_mask[i], gain->lna[path][i], detail)) {
        return false;
      }
    }
    if (!bb_mask(0x4054 + offset, 0x7fc0000, (uint32_t)(int8_t)gain->tia[path][0], detail) ||
        !bb_mask(0x4058 + offset, 0x1ff, (uint32_t)(int8_t)gain->tia[path][1], detail)) {
      return false;
    }
    for (unsigned i = 0; i < 7; ++i) {
      if (!bb_mask(lna_op_address[i] + offset, lna_op_mask[i], gain->lna_op[path][i], detail)) {
        return false;
      }
    }
    for (unsigned i = 0; i < 8; ++i) {
      if (!bb_mask(tia_op_address[i] + offset, tia_op_mask[i], gain->tia_op[path][i], detail)) {
        return false;
      }
    }
    /* The reference writes widest RPL compensation first. */
    static const uint8_t order[] = {7, 8, 9, 10, 11, 12, 13, 14, 3, 4, 5, 6, 1, 2, 0};
    for (unsigned i = 0; i < sizeof(order); ++i) {
      unsigned index = order[i];
      if (!bb_mask(rpl_address[index] + offset, rpl_mask[index], gain->rpl[path][index], detail)) {
        return false;
      }
    }
  }
  return true;
}

static int clamp_s8(int value)
{
  return value < -128 ? -128 : value > 127 ? 127 : value;
}

static bool apply_gain_offsets(const struct rtl8922ae_board *board,
    const char **detail)
{
  if (!board->gain_offset_known) {
    return fail(detail, "RTL8922AE receive gain offset provenance unavailable");
  }
  if (!board->gain_offset_present) {
    return true;
  }
  for (unsigned path = 0; path < 2; ++path) {
    int cck = -board->gain_offset_2g_cck[path];
    unsigned fraction = (unsigned)cck & 3;
    if (!bb_mask(R_MGAIN_BIAS, B_MGAIN_BIAS_BW20, fraction ? (4 - fraction) << 1 : 0, detail) ||
        !bb_mask(R_MGAIN_BIAS, B_MGAIN_BIAS_BW40, fraction ? (4 - fraction) << 1 : 0, detail) ||
        !bb_mask(R_CCK_RPL_OFST, B_CCK_RPL_OFST,
            (uint32_t)((cck >> 2) + (fraction ? 1 : 0) + 0xdc), detail)) {
      return false;
    }
    uint32_t offset = path * 0x400;
    int value = board->gain_offset_2g_ofdm[path];
    if (!bb_mask(0x40c8 + offset, 0xff000000, (uint32_t)(value + 0xf8), detail)) {
      return false;
    }
    value *= -4;
    int v1 = clamp_s8(value);
    value -= v1;
    int v2 = clamp_s8(value);
    int v3 = clamp_s8(value - v2);
    if (!bb_mask(0x41e8 + offset, 0xff, (uint32_t)v1, detail) ||
        !bb_mask(0x41f8 + offset, 0xff, (uint32_t)v2, detail) ||
        !bb_mask(0x41f8 + offset, 0xff00, (uint32_t)v3, detail) ||
        !bb_mask(0x41f8 + offset, 0xff0000, (uint32_t)v1, detail) ||
        !bb_mask(0x4208 + offset, 0xff0000, (uint32_t)v2, detail) ||
        !bb_mask(0x4208 + offset, 0xff000000, (uint32_t)v3, detail)) {
      return false;
    }
  }
  return true;
}

bool rtl8922ae_phy_channel_enter(uint64_t deadline, const char **detail)
{
  static const struct phy_register settings[] = {
    {0x2800, PHY_BIT(1), 0},
    {0x2900, PHY_BIT(1), 0},
    {R_TSSI_PWR_P0, B_TSSI_CONT_EN, 1},
    {R_TSSI_PWR_P1, B_TSSI_CONT_EN, 1},
  };
  if (!registers(settings, sizeof(settings) / sizeof(*settings), true, deadline, detail)) {
    return false;
  }
  uint32_t raw = rtl8922ae_read32(PHY_BASE + R_ADC_FIFO_V1);
  if (!bb_mask(R_ADC_FIFO_V1, B_ADC_FIFO_EN_V1,
      ((raw & B_ADC_FIFO_EN_V1) >> __builtin_ctz(B_ADC_FIFO_EN_V1)) | 3, detail) ||
      !rtl8922ae_sleep(40 * NS_PER_US, deadline) ||
      !bb_mask(R_RXCCA_BE1, B_RXCCA_BE1_DIS, 1, detail) ||
      !bb_mask(R_PD_CTRL, B_PD_HIT_DIS, 1, detail) ||
      !rtl8922ae_sleep(NS_PER_US, deadline) ||
      !bb_mask(R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 0, detail)) {
    return fail(detail, "RTL8922AE channel PHY reset entry failed");
  }
  return true;
}

bool rtl8922ae_phy_channel_tune(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_tables *tables,
    const struct rtl8922ae_board *board, uint8_t channel, uint64_t deadline,
    const char **detail)
{
  if (!board || !tables || channel < 1 || channel > 13 ||
      !board->gain_offset_known || tables->live_cut != 2 || board->rfe != 1) {
    return fail(detail, "RTL8922AE channel or receive gain profile unsupported");
  }
  if (!checkpoint(deadline, detail)) return false;
  uint8_t mode = rtl8922ae_read8(R_BE_WMAC_RFMOD);
  uint8_t rate = rtl8922ae_read8(R_BE_TXRATE_CHK);
  rtl8922ae_write8(R_BE_WMAC_RFMOD, mode & ~B_BE_WMAC_RFMOD_MASK);
  rate &= ~(B_BE_BAND_MODE | B_BE_CHECK_CCK_EN | B_BE_RTS_LIMIT_IN_OFDM6);
  rtl8922ae_write8(R_BE_TXRATE_CHK, rate | B_BE_BAND_MODE);
  static const struct phy_register mac[] = {
    {R_BE_TX_SUB_BAND_VALUE, UINT32_MAX, PHY_BIT(16)},
    {R_BE_PREBKF_CFG_1, B_BE_SIFS_MACTXEN_T1_MASK, 0x3f},
    {R_BE_MUEDCA_EN, B_BE_SIFS_MACTXEN_TB_T1_MASK, 0x3e},
  };
  if (!registers(mac, sizeof(mac) / sizeof(*mac), false, deadline, detail)) {
    return false;
  }
  static const uint32_t barker[] = {0x1fe4f, 0x1ff5e, 0x2006c, 0x2017b,
      0x2028a, 0x20399, 0x204a8, 0x205b6, 0x206c5, 0x207d4, 0x208e3,
      0x209f2, 0x20b00};
  static const uint32_t cck[] = {0x2bdac, 0x2bf21, 0x2c095, 0x2c209,
      0x2c37e, 0x2c4f2, 0x2c666, 0x2c7db, 0x2c94f, 0x2cac3, 0x2cc38,
      0x2cdac, 0x2cf21};
  static const uint32_t coefficients[] = {0x3a63ca, 0x2a833f, 0x1491f8,
      0x03c0b0, 0xfccff1, 0xfccfc3, 0xfebfdc, 0xffdff7};
  struct phy_gain gain = {0};
  if (!bb_mask(R_BK_FC0INV, B_BK_FC0INV, barker[channel - 1], detail) ||
      !bb_mask(R_CCK_FC0INV, B_CCK_FC0INV, cck[channel - 1], detail) ||
      !apply_table(asset, bytes, tables, RTL8922AE_ELEMENT_BB_GAIN,
          TABLE_GAIN, 0, &gain, deadline, detail) ||
      !apply_gain(&gain, deadline, detail) ||
      !bb_mask(0x4160, PHY_BIT(26), 1, detail) ||
      !bb_mask(0x4560, PHY_BIT(26), 1, detail) ||
      !apply_gain_offsets(board, detail)) {
    return false;
  }
  unsigned frequency = 2407 + channel * 5;
  if (!bb_mask(R_FC0, B_FC0, frequency, detail) ||
      !bb_mask(R_FC0INV_SBW, B_FC0_INV, ((1u << 18) + frequency / 2) / frequency, detail)) {
    return false;
  }
  for (unsigned i = 0; i < 8; ++i) {
    if (!bb_mask(R_PCOEFF01 + i * 4, B_PCOEFF01, coefficients[i], detail)) {
      return false;
    }
  }
  static const struct phy_register bandwidth[] = {
    {R_ANT_CHBW, B_CHBW_BW, 0},
    {R_FC0INV_SBW, B_SMALLBW, 0},
    {R_ANT_CHBW, B_CHBW_PRICH, 0},
    {R_DAC_CLK, B_DAC_CLK, 1},
    {R_GAIN_MAP0, B_GAIN_MAP0_EN, 0},
    {R_GAIN_MAP1, B_GAIN_MAP1_EN, 0},
    {R_FC0, B_BW40_2XFFT, 0},
    {R_RXCCA_BE1, B_RXCCA_BE1_DIS, 0},
    {R_UPD_CLK_ADC, B_ENABLE_CCK, 1},
    {R_PD_ARBITER_OFF, B_PD_ARBITER_OFF, 0},
    {R_S0S1_CSI_WGT, B_S0S1_CSI_WGT_EN, 0},
    {0x41a0, PHY_BIT(12), 0},
    {0x41ac, PHY_BIT(12), 0},
    {0x45a0, PHY_BIT(12), 0},
    {0x45ac, PHY_BIT(12), 0},
    {R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1},
    {R_TXPWR_RSTA, B_TXPWR_RSTA, 0},
    {R_TXPWR_RSTA, B_TXPWR_RSTA, 1},
    {R_TXPWR_RSTB, B_TXPWR_RSTB, 0},
    {R_TXPWR_RSTB, B_TXPWR_RSTB, 1},
  };
  if (!bb_mask(R_MAC_PIN_SEL, B_CH_IDX_SEG0, channel, detail) ||
      !registers(bandwidth, sizeof(bandwidth) / sizeof(*bandwidth), true, deadline, detail)) {
    return false;
  }
  const uint32_t rf_addresses[] = {RR_CFGCH, RR_CFGCH_V1};
  uint32_t rf[2][2];
  for (unsigned path = 0; path < 2; ++path) {
    for (unsigned i = 0; i < 2; ++i) {
      if (!rtl8922ae_phy_rf_read(path, rf_addresses[i], RF_MASK, &rf[path][i], deadline, detail)) {
        return false;
      }
    }
  }
  /* In MLO_2_PLUS_0_1RF the reference synthesizer selection is RF_PATH_A. */
  uint32_t synthesizer;
  if (!rtl8922ae_phy_rf_read(0, RR_CFGCH, RF_MASK, &synthesizer, deadline, detail)) {
    return false;
  }
  for (unsigned path = 0; path < 2; ++path) {
    for (unsigned i = 0; i < 2; ++i) {
      uint32_t value = rf[path][i] &
          ~(RR_CFGCH_BAND1 | RR_CFGCH_BW_V2 | RR_CFGCH_BAND0 | RR_CFGCH_CH);
      value |= channel;
      rtl8922ae_phy_rf_write(path, rf_addresses[i], RF_MASK, value, deadline, detail);
      if (!rtl8922ae_sleep(100 * NS_PER_US, deadline)) {
        return fail(detail, "RTL8922AE RF channel tuning interrupted");
      }
    }
  }
  return checkpoint(deadline, detail);
}

static bool channel_resume(uint64_t deadline, const char **detail)
{
  uint32_t raw = rtl8922ae_read32(PHY_BASE + R_ADC_FIFO_V1);
  if (!bb_mask(R_ADC_FIFO_V1, B_ADC_FIFO_EN_V1,
      ((raw & B_ADC_FIFO_EN_V1) >> __builtin_ctz(B_ADC_FIFO_EN_V1)) & ~3u, detail)) {
    return fail(detail, "RTL8922AE channel ADC resume unconfirmed");
  }
  static const struct phy_register settings[] = {
    {0x2800, PHY_BIT(1), 1},
    {0x2900, PHY_BIT(1), 1},
    {R_TSSI_PWR_P0, B_TSSI_CONT_EN, 0},
    {R_TSSI_PWR_P1, B_TSSI_CONT_EN, 0},
    {R_RSTB_ASYNC, B_RSTB_ASYNC_ALL, 1},
    {R_RXCCA_BE1, B_RXCCA_BE1_DIS, 0},
    {R_PD_CTRL, B_PD_HIT_DIS, 0},
  };
  if (!registers(settings, sizeof(settings) / sizeof(*settings), true, deadline, detail)) {
    return false;
  }
  return true;
}

bool rtl8922ae_phy_channel_leave(const uint8_t slots[2], uint64_t deadline,
    const char **detail)
{
  if (!slots || slots[0] > 2 || slots[1] > 2 || !checkpoint(deadline, detail)) {
    return fail(detail, "RTL8922AE channel RFK slot invalid or interrupted");
  }
  if (!channel_resume(deadline, detail)) {
    return false;
  }
  for (unsigned path = 0; path < 2; ++path) {
    uint32_t coefficient = path ? R_COEF_SEL_C1 : R_COEF_SEL;
    uint32_t lut = path ? R_CFIR_LUT_C1 : R_CFIR_LUT;
    if (!bb_mask(coefficient, B_COEF_SEL_EN, 1, detail) ||
        !bb_mask(coefficient, B_COEF_SEL_IQC_V1, slots[path], detail) ||
        !bb_mask(coefficient, B_COEF_SEL_MDPD_V1, slots[path], detail)) {
      return false;
    }
    rtl8922ae_phy_rf_write(path, RR_MODOPT, RR_TXG_SEL,
        4 | slots[path], deadline, detail);
    if (!checkpoint(deadline, detail)) return false;
    uint32_t selection = rtl8922ae_read32(PHY_BASE + coefficient);
    if (!bb_mask(lut, B_CFIR_LUT_G3, selection & 1, detail) ||
        !bb_mask(lut, B_CFIR_LUT_G5, selection >> 1 & 1, detail)) {
      return fail(detail, "RTL8922AE channel RFK coefficient selection unconfirmed");
    }
  }
  return checkpoint(deadline, detail);
}

/* Linux rtw89_phy_dig_ctrl/config_edcca, unassociated PHY0. These are
 * temporary receive thresholds, not RF power or regulatory substitutions. */
static uint32_t scan_edcca, scan_ppdu;

void rtl8922ae_phy_scan_dig(void)
{
  /* No associated DIG watchdog runs in this profile. Linux's saved lower
   * bound is zero, both when suspending DIG and when resuming it. */
  (void)bb_mask(R_SEG0R_PD_V2, B_SEG0R_PD_LOWER_BOUND_MSK, 0, NULL);
  (void)bb_mask(R_SEG0R_PD_V2, B_SEG0R_PD_SPATIAL_REUSE_EN_MSK_V1, 0, NULL);
}

void rtl8922ae_phy_scan_edcca(bool scanning)
{
  uint32_t edcca = rtl8922ae_read32(PHY_BASE + R_SEG0R_EDCCA_LVL_BE);
  uint32_t ppdu = rtl8922ae_read32(PHY_BASE + R_SEG0R_PPDU_LVL_BE);
  if (scanning) {
    scan_edcca = edcca & (B_EDCCA_LVL_MSK0 | B_EDCCA_LVL_MSK1);
    scan_ppdu = ppdu & B_EDCCA_LVL_MSK1;
    (void)bb_mask(R_SEG0R_EDCCA_LVL_BE, B_EDCCA_LVL_MSK0, PHY_EDCCA_SCAN_MAX, NULL);
    (void)bb_mask(R_SEG0R_EDCCA_LVL_BE, B_EDCCA_LVL_MSK1, PHY_EDCCA_SCAN_MAX, NULL);
    (void)bb_mask(R_SEG0R_PPDU_LVL_BE, B_EDCCA_LVL_MSK1, PHY_EDCCA_SCAN_MAX, NULL);
  } else {
    rtl8922ae_write32(PHY_BASE + R_SEG0R_EDCCA_LVL_BE,
        (edcca & ~(B_EDCCA_LVL_MSK0 | B_EDCCA_LVL_MSK1)) | scan_edcca);
    rtl8922ae_write32(PHY_BASE + R_SEG0R_PPDU_LVL_BE,
        (ppdu & ~B_EDCCA_LVL_MSK1) | scan_ppdu);
  }
}
