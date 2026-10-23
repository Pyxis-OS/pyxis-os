/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020, 2023 Realtek Corporation
 * Format reference: Linux rtw89 fw.c/fw.h, efuse.c and efuse_be.c at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, BSD alternative.
 * See kernel/wifi/NOTICE for the complete notice.
 */
#include "rtl8922ae_firmware.h"

#define MFW_SIGNATURE UINT8_C(0xff)
#define MFW_HEADER_BYTES 16u
#define MFW_ENTRY_BYTES 16u
#define MFW_NORMAL 1u
#define MFW_WOWLAN 3u
#define MFW_NORMAL_CE 5u
#define MFW_LOGFMT 255u
#define FW_VERSION_HEADER_BYTES 16u
#define FW_V0_HEADER_BYTES 32u
#define FW_V1_HEADER_BYTES 48u
#define FW_SECTION_HEADER_BYTES 16u
#define FW_HEADER_VERSION_SHIFT 24u
#define FW_SECTION_COUNT_SHIFT 8u
#define FW_SECTION_TYPE_SHIFT 24u
#define FW_SECTION_TYPE_MASK UINT32_C(0xf)
#define FW_SECTION_SIZE_MASK UINT32_C(0xffffff)
#define FW_SECTION_CHECKSUM (UINT32_C(1) << 28)
#define FW_DSP_CHECKSUM (UINT32_C(1) << 24)
#define FW_DYNAMIC_HEADER (UINT32_C(1) << 16)
#define FW_DYNAMIC_HEADER_BYTES 8u
#define FW_SECURITY_SECTION 9u
#define FW_SECURITY_SIGNATURE_BYTES 512u
#define FW_CHECKSUM_BYTES 8u
#define FW_FORMATTED_MSSC UINT8_C(0xff)
#define FW_SECURITY_KEY_LENGTH_OFFSET 60u
#define FW_MSS_POOL_HEADER_BYTES 32u
#define FW_MSS_POOL_REMAP_BITMASK 0u
#define FW_MSS_POOL_DEFAULT_BYTES 8u
#define FW_ELEMENT_HEADER_BYTES 32u
#define FW_ELEMENT_ALIGNMENT 16u
#define FW_ELEMENT_BBMCU0 0u
#define FW_ELEMENT_BBMCU1 1u

static bool span_valid(size_t bytes, size_t offset, size_t length)
{
  return offset <= bytes && length <= bytes - offset;
}

/* Callers establish the complete containing span before decoding fields. */
static uint16_t read_le16(const uint8_t *data)
{
  return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
      ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool align_element(size_t offset, size_t *aligned)
{
  size_t padding = (FW_ELEMENT_ALIGNMENT - offset % FW_ELEMENT_ALIGNMENT) %
      FW_ELEMENT_ALIGNMENT;
  if (padding > SIZE_MAX - offset) {
    return false;
  }
  *aligned = offset + padding;
  return true;
}

static bool inspect_mss_pool(const uint8_t *section, size_t bytes, size_t section_bytes,
    bool dsp_checksum, size_t *pool_bytes)
{
  static const uint8_t signature[8] = {'M', 'S', 'S', 'K', 'P', 'O', 'O', 'L'};
  if (section_bytes < FW_SECURITY_KEY_LENGTH_OFFSET + sizeof(uint16_t) ||
      !span_valid(bytes, section_bytes, FW_MSS_POOL_HEADER_BYTES)) {
    return false;
  }

  const uint8_t *pool = section + section_bytes;
  for (size_t i = 0; i < sizeof(signature); ++i) {
    if (pool[i] != signature[i]) {
      return false;
    }
  }
  if (pool[20] != FW_MSS_POOL_REMAP_BITMASK) {
    return false;
  }

  uint16_t key_count = read_le16(pool + 26);
  uint16_t customer_count = read_le16(pool + 24);
  uint8_t device_count = pool[21];
  uint64_t remap_bytes = ((uint64_t)key_count * customer_count * device_count) >> 3;
  if (pool[16]) {
    remap_bytes += FW_MSS_POOL_DEFAULT_BYTES;
  }
  uint64_t raw_offset = FW_MSS_POOL_HEADER_BYTES + remap_bytes;
  if (raw_offset != read_le32(pool + 12)) {
    return false;
  }

  size_t key_bytes = read_le16(section + FW_SECURITY_KEY_LENGTH_OFFSET) >> 2;
  if (!key_bytes) {
    key_bytes = FW_SECURITY_SIGNATURE_BYTES;
  }
  if (dsp_checksum) {
    key_bytes += FW_CHECKSUM_BYTES;
  }
  uint64_t total = raw_offset + (uint64_t)read_le16(pool + 22) * key_bytes;
  if (total > SIZE_MAX || !span_valid(bytes, section_bytes, (size_t)total)) {
    return false;
  }
  *pool_bytes = (size_t)total;
  return true;
}

static bool inspect_image(const uint8_t *data, size_t bytes,
    struct rtl8922ae_firmware_info *info)
{
  if (bytes < FW_VERSION_HEADER_BYTES) {
    return false;
  }

  unsigned version = read_le32(data + 12) >> FW_HEADER_VERSION_SHIFT;
  /* No payload reaches hardware here. Leave other image versions opaque. */
  if (version != 0 && version != 1) {
    return true;
  }
  size_t fixed_bytes = version == 0 ? FW_V0_HEADER_BYTES : FW_V1_HEADER_BYTES;
  if (bytes < fixed_bytes) {
    return false;
  }
  uint32_t word6 = read_le32(data + 24);
  uint32_t word7 = read_le32(data + 28);
  bool dsp_checksum = version == 1 && (word6 & FW_DSP_CHECKSUM);
  size_t sections = (word6 >> FW_SECTION_COUNT_SHIFT) & UINT8_MAX;
  size_t base_bytes = fixed_bytes + sections * FW_SECTION_HEADER_BYTES;
  if (base_bytes > bytes) {
    return false;
  }

  size_t cursor = base_bytes;
  if (word7 & FW_DYNAMIC_HEADER) {
    size_t header_bytes = version == 0 ?
        (read_le32(data + 12) >> 16) & UINT8_MAX : read_le32(data + 20) >> 16;
    if (header_bytes < base_bytes || header_bytes > bytes ||
        header_bytes - base_bytes < FW_DYNAMIC_HEADER_BYTES ||
        read_le32(data + base_bytes) != header_bytes - base_bytes) {
      return false;
    }
    cursor = header_bytes;
  }

  for (size_t i = 0; i < sections; ++i) {
    const uint8_t *header = data + fixed_bytes +
        i * FW_SECTION_HEADER_BYTES;
    uint32_t word1 = read_le32(header + 4);
    uint8_t type = (word1 >> FW_SECTION_TYPE_SHIFT) & FW_SECTION_TYPE_MASK;
    size_t section_bytes = word1 & FW_SECTION_SIZE_MASK;
    if (word1 & FW_SECTION_CHECKSUM) {
      section_bytes += FW_CHECKSUM_BYTES;
    }
    if (!span_valid(bytes, cursor, section_bytes)) {
      return false;
    }

    size_t security_bytes = 0;
    if (type == FW_SECURITY_SECTION) {
      uint32_t mssc = read_le32(header + 8);
      if (version == 1) {
        mssc &= UINT8_MAX;
      }
      if ((mssc & UINT8_MAX) == FW_FORMATTED_MSSC) {
        if (!inspect_mss_pool(data + cursor, bytes - cursor, section_bytes,
                dsp_checksum, &security_bytes)) {
          return false;
        }
        ++info->formatted_mss_pools;
      } else {
        size_t signature_bytes = FW_SECURITY_SIGNATURE_BYTES;
        if (dsp_checksum) {
          signature_bytes += FW_CHECKSUM_BYTES;
        }
        uint64_t total = (uint64_t)mssc * signature_bytes;
        if (total > SIZE_MAX || !span_valid(bytes - cursor, section_bytes, (size_t)total)) {
          return false;
        }
        security_bytes = (size_t)total;
      }
      ++info->security_sections;
    }
    cursor += section_bytes + security_bytes;
  }
  if (cursor != bytes) {
    return false;
  }
  info->sections += sections;
  return true;
}

static bool inspect_elements(const uint8_t *data, size_t bytes, size_t offset,
    struct rtl8922ae_firmware_info *info)
{
  /* Linux leaves a terminal header-sized or shorter tail uninterpreted. */
  while (span_valid(bytes, offset, FW_ELEMENT_HEADER_BYTES + 1)) {
    const uint8_t *header = data + offset;
    uint32_t id = read_le32(header);
    size_t length = read_le32(header + 4);
    size_t payload = offset + FW_ELEMENT_HEADER_BYTES;
    if (!span_valid(bytes, payload, length)) {
      return false;
    }

    if (length && (id == FW_ELEMENT_BBMCU0 || id == FW_ELEMENT_BBMCU1)) {
      if (!inspect_image(data + payload, length, info)) {
        return false;
      }
      ++info->bbmcu_images;
    }
    ++info->elements;
    size_t end = payload + length;
    /* The final payload need not have padding to the next alignment. */
    if (end == bytes) {
      return true;
    }
    if (!align_element(end, &offset)) {
      return false;
    }
  }
  return true;
}

bool rtl8922ae_firmware_inspect(const void *data, size_t bytes,
    struct rtl8922ae_firmware_info *info)
{
  if (!info) {
    return false;
  }
  *info = (struct rtl8922ae_firmware_info){0};
  const uint8_t *firmware = data;
  if (!firmware || bytes < MFW_HEADER_BYTES || firmware[0] != MFW_SIGNATURE) {
    return false;
  }

  uint8_t entries = firmware[1];
  size_t directory_bytes = MFW_HEADER_BYTES + (size_t)entries * MFW_ENTRY_BYTES;
  if (!entries || directory_bytes > bytes) {
    return false;
  }

  struct rtl8922ae_firmware_info result = {.bytes = bytes, .mfw_entries = entries};
  for (size_t i = 0; i < sizeof(result.container_version); ++i)
    result.container_version[i] = firmware[4 + i];

  size_t payload_end = directory_bytes;
  for (size_t i = 0; i < entries; ++i) {
    const uint8_t *entry = firmware + MFW_HEADER_BYTES + i * MFW_ENTRY_BYTES;
    size_t offset = read_le32(entry + 4);
    size_t length = read_le32(entry + 8);
    if (!span_valid(bytes, offset, length)) {
      return false;
    }
    size_t end = offset + length;
    if (end > payload_end) {
      payload_end = end;
    }

    if (!length) {
      continue;
    }

    switch (entry[1]) {
    case MFW_NORMAL:
    case MFW_NORMAL_CE:
      if (!inspect_image(firmware + offset, length, &result)) {
        return false;
      }
      ++result.normal_suits;
      break;
    case MFW_WOWLAN:
      if (!inspect_image(firmware + offset, length, &result)) {
        return false;
      }
      ++result.wowlan_suits;
      break;
    case MFW_LOGFMT:
      ++result.log_suits;
      break;
    default:
      break;
    }
  }

  if (payload_end != bytes) {
    size_t elements_offset;
    if (!align_element(payload_end, &elements_offset) ||
        !inspect_elements(firmware, bytes, elements_offset, &result)) {
      return false;
    }
  }
  *info = result;
  return true;
}

#define FW_SECURITY_SELECTOR_OFFSET 58u
#define FW_SECURITY_BLACKLIST_OFFSET 32u
#define FW_SECURITY_DEFAULT_DEVICE UINT8_C(0xf)
#define FW_SECTION_COUNT_MASK UINT32_C(0xff00)
#define FW_PART_SIZE_MASK UINT32_C(0xffff)
#define FW_V0_DESTINATION_MASK UINT32_C(0x1fffffff)

static void write_le32(uint8_t *data, uint32_t value)
{
  for (size_t i = 0; i < sizeof(value); ++i) {
    data[i] = (uint8_t)(value >> (i * 8));
  }
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t bytes)
{
  for (size_t i = 0; i < bytes; ++i) {
    destination[i] = source[i];
  }
}

static unsigned bit_count(uint8_t value)
{
  unsigned count = 0;
  while (value) {
    count += value & 1;
    value >>= 1;
  }
  return count;
}

bool rtl8922ae_firmware_decode_security(const uint8_t security_bytes[4],
    struct rtl8922ae_firmware_security *security)
{
  if (!security) {
    return false;
  }
  *security = (struct rtl8922ae_firmware_security){0};
  if (!security_bytes) {
    return false;
  }
  uint16_t selector = read_le16(security_bytes + 2);
  if (selector == UINT16_MAX) {
    return true;
  }

  unsigned group = 0;
  unsigned zero_pairs = 0;
  uint8_t form = security_bytes[2];
  for (unsigned i = 0; i < 4; ++i) {
    unsigned low = (form >> i) & 1;
    if (low != ((form >> (7 - i)) & 1)) {
      return false;
    }
    if (!low) {
      group = i;
      ++zero_pairs;
    }
  }
  uint8_t index_low = security_bytes[3] & 0xf;
  uint8_t index_high = security_bytes[3] >> 4;
  if (zero_pairs > 1 || (index_low ^ index_high) != 0xf) {
    return false;
  }
  unsigned index = group * 16 + index_low;
  static const uint32_t selector_magic[] = {
    UINT32_C(0x08000100), UINT32_C(0x0c000180),
  };
  if (index >= sizeof(selector_magic) / sizeof(selector_magic[0])) {
    return false;
  }

  uint8_t device_type;
  switch (security_bytes[0] & 0xf) {
  case 0xc: device_type = 0; break;
  case 0xa: device_type = 1; break;
  case 0x9: device_type = 2; break;
  case 0x6: device_type = 3; break;
  case 0xf: device_type = FW_SECURITY_DEFAULT_DEVICE; break;
  default: return false;
  }
  *security = (struct rtl8922ae_firmware_security){
    .selector_magic = selector_magic[index],
    .device_type = device_type,
    .customer = 0x1f - ((security_bytes[0] >> 4) |
        (((security_bytes[1] >> 6) & 1) << 4)),
    .key_number = 0xf - (security_bytes[1] & 0xf),
    .secure_boot = true,
  };
  return true;
}

/* Only a selected secure section is checked against the chip's pinned default
 * blacklist: version zero, all 256 firmware bits clear (fw.c/rtw8922a.c).
 * Later list versions cannot be authorized by this reference.
 */
static bool blacklist_allows(const uint8_t *section)
{
  static const uint8_t list[32] = {0};
  uint8_t bit = section[FW_SECURITY_BLACKLIST_OFFSET];
  uint8_t version = section[FW_SECURITY_BLACKLIST_OFFSET + 1];
  return version == 0 && !(list[bit >> 3] & (1u << (bit & 7)));
}

static enum rtl8922ae_firmware_plan_result plan_security_section(
    const uint8_t *data, size_t bytes, size_t offset, size_t section_bytes,
    uint32_t mssc, bool dsp_checksum,
    const struct rtl8922ae_firmware_security *security,
    struct rtl8922ae_firmware_section *section, size_t *pool_bytes, bool *matches)
{
  *matches = false;
  *pool_bytes = 0;
  if (section_bytes < FW_SECURITY_BLACKLIST_OFFSET + 2) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  const uint8_t *content = data + offset;
  if ((mssc & UINT8_MAX) != FW_FORMATTED_MSSC) {
    size_t key_bytes = FW_SECURITY_SIGNATURE_BYTES +
        (dsp_checksum ? FW_CHECKSUM_BYTES : 0);
    uint64_t total = (uint64_t)mssc * key_bytes;
    if (total > SIZE_MAX || !span_valid(bytes - offset, section_bytes, (size_t)total)) {
      return RTL8922AE_FIRMWARE_PLAN_INVALID;
    }
    *pool_bytes = (size_t)total;
    /* BE v1 eFuse has no legacy mss_idx mapping. Never assume key index zero. */
    if (security->secure_boot) {
      return RTL8922AE_FIRMWARE_PLAN_UNSUPPORTED;
    }
    *matches = true;
    return RTL8922AE_FIRMWARE_PLAN_OK;
  }

  if (!inspect_mss_pool(content, bytes - offset, section_bytes, dsp_checksum,
          pool_bytes)) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  const uint8_t *pool = content + section_bytes;
  uint16_t key_max = read_le16(pool + 26);
  uint16_t customer_max = read_le16(pool + 24);
  uint16_t key_count = read_le16(pool + 22);
  uint8_t device_max = pool[21];
  uint64_t remap_bits = (uint64_t)key_max * customer_max * device_max;
  size_t raw_offset = read_le32(pool + 12);
  if (pool[16] > 1 || read_le32(pool + 8) != FW_MSS_POOL_HEADER_BYTES ||
      !key_max || !customer_max || !device_max || !key_count || (remap_bits & 7)) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  size_t remap_bytes = raw_offset - FW_MSS_POOL_HEADER_BYTES;
  size_t populated = 0;
  for (size_t i = 0; i < remap_bytes; ++i) {
    populated += bit_count(pool[FW_MSS_POOL_HEADER_BYTES + i]);
  }
  if (populated != key_count) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  if (!security->secure_boot) {
    *matches = true;
    return RTL8922AE_FIRMWARE_PLAN_OK;
  }

  uint32_t selector_magic = read_le32(content + FW_SECURITY_SELECTOR_OFFSET);
  if (selector_magic && selector_magic != security->selector_magic) {
    return RTL8922AE_FIRMWARE_PLAN_OK;
  }
  if (security->customer >= customer_max || security->key_number >= key_max) {
    return RTL8922AE_FIRMWARE_PLAN_OK;
  }
  uint64_t selection = (uint64_t)security->customer * key_max + security->key_number;
  if (security->device_type == FW_SECURITY_DEFAULT_DEVICE) {
    if (!pool[16] || selection >= FW_MSS_POOL_DEFAULT_BYTES * 8) {
      return RTL8922AE_FIRMWARE_PLAN_OK;
    }
  } else {
    if (security->device_type >= device_max) {
      return RTL8922AE_FIRMWARE_PLAN_OK;
    }
    selection += (uint64_t)security->device_type * key_max * customer_max;
    if (pool[16]) {
      selection += FW_MSS_POOL_DEFAULT_BYTES * 8;
    }
  }
  if (selection / 8 >= remap_bytes) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  const uint8_t *remap = pool + FW_MSS_POOL_HEADER_BYTES;
  uint8_t selected_mask = (uint8_t)(1u << (selection & 7));
  if (!(remap[selection / 8] & selected_mask)) {
    return RTL8922AE_FIRMWARE_PLAN_OK;
  }
  size_t key_index = bit_count(remap[selection / 8] & (selected_mask - 1));
  for (size_t i = 0; i < selection / 8; ++i) {
    key_index += bit_count(remap[i]);
  }
  size_t key_bytes = read_le16(content + FW_SECURITY_KEY_LENGTH_OFFSET) >> 2;
  if (!key_bytes) {
    key_bytes = FW_SECURITY_SIGNATURE_BYTES;
  }
  if (dsp_checksum) {
    key_bytes += FW_CHECKSUM_BYTES;
  }
  if (key_index >= key_count || section_bytes < key_bytes) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  if (!blacklist_allows(content)) {
    return RTL8922AE_FIRMWARE_PLAN_BLACKLISTED;
  }
  section->key_offset = offset + section_bytes + raw_offset + key_index * key_bytes;
  section->key_length = key_bytes;
  *matches = true;
  return RTL8922AE_FIRMWARE_PLAN_OK;
}

static enum rtl8922ae_firmware_plan_result build_plan(const uint8_t *data,
    size_t bytes, uint8_t live_cut, const uint8_t security_bytes[4],
    struct rtl8922ae_firmware_plan *plan)
{
  if (!rtl8922ae_firmware_decode_security(security_bytes, &plan->security)) {
    return RTL8922AE_FIRMWARE_PLAN_SECURITY_INVALID;
  }
  if (!data || bytes < MFW_HEADER_BYTES || data[0] != MFW_SIGNATURE || !data[1]) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  size_t directory_bytes = MFW_HEADER_BYTES + (size_t)data[1] * MFW_ENTRY_BYTES;
  if (directory_bytes > bytes) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  const uint8_t *selected = NULL;
  for (size_t i = 0; i < data[1]; ++i) {
    const uint8_t *entry = data + MFW_HEADER_BYTES + i * MFW_ENTRY_BYTES;
    size_t offset = read_le32(entry + 4);
    size_t length = read_le32(entry + 8);
    if (!span_valid(bytes, offset, length)) {
      return RTL8922AE_FIRMWARE_PLAN_INVALID;
    }
    if (entry[1] != MFW_NORMAL || entry[2] || entry[0] > live_cut) {
      continue;
    }
    if (!selected || entry[0] > selected[0]) {
      selected = entry;
    }
  }
  if (!selected) {
    return RTL8922AE_FIRMWARE_PLAN_NO_IMAGE;
  }
  plan->image_offset = read_le32(selected + 4);
  plan->image_bytes = read_le32(selected + 8);
  plan->cut = selected[0];
  const uint8_t *image = data + plan->image_offset;
  size_t image_bytes = plan->image_bytes;
  if (plan->image_offset < directory_bytes || image_bytes < FW_VERSION_HEADER_BYTES) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  unsigned version = image[15];
  if (version != 0 && version != 1) {
    return RTL8922AE_FIRMWARE_PLAN_UNSUPPORTED;
  }
  size_t fixed_bytes = version == 0 ? FW_V0_HEADER_BYTES : FW_V1_HEADER_BYTES;
  if (image_bytes < fixed_bytes) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  uint32_t word6 = read_le32(image + 24);
  uint32_t word7 = read_le32(image + 28);
  size_t section_count = (word6 >> FW_SECTION_COUNT_SHIFT) & UINT8_MAX;
  size_t base_bytes = fixed_bytes + section_count * FW_SECTION_HEADER_BYTES;
  size_t cursor = base_bytes;
  if (!section_count || base_bytes > image_bytes) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  if (word7 & FW_DYNAMIC_HEADER) {
    size_t header_bytes = version == 0 ?
        (read_le32(image + 12) >> 16) & UINT8_MAX : read_le32(image + 20) >> 16;
    if (header_bytes < base_bytes || header_bytes > image_bytes ||
        header_bytes - base_bytes < FW_DYNAMIC_HEADER_BYTES ||
        read_le32(image + base_bytes) != header_bytes - base_bytes) {
      return RTL8922AE_FIRMWARE_PLAN_INVALID;
    }
    cursor = header_bytes;
  }
  bool dsp_checksum = version == 1 && (word6 & FW_DSP_CHECKSUM);
  bool selected_security = false;
  copy_bytes(plan->header, image, fixed_bytes);
  for (size_t i = 0; i < section_count; ++i) {
    const uint8_t *header = image + fixed_bytes + i * FW_SECTION_HEADER_BYTES;
    uint32_t word1 = read_le32(header + 4);
    struct rtl8922ae_firmware_section section = {
      .offset = plan->image_offset + cursor,
      .length = word1 & FW_SECTION_SIZE_MASK,
      .destination = read_le32(header),
      .type = (word1 >> FW_SECTION_TYPE_SHIFT) & FW_SECTION_TYPE_MASK,
    };
    if (version == 0) {
      section.destination &= FW_V0_DESTINATION_MASK;
    }
    if (word1 & FW_SECTION_CHECKSUM) {
      section.length += FW_CHECKSUM_BYTES;
    }
    if (!section.length || !span_valid(image_bytes, cursor, section.length) ||
        (uint64_t)section.destination + section.length > (UINT64_C(1) << 32)) {
      return RTL8922AE_FIRMWARE_PLAN_INVALID;
    }
    bool include = true;
    size_t pool_bytes = 0;
    if (section.type == FW_SECURITY_SECTION) {
      uint32_t mssc = read_le32(header + 8);
      if (version == 1) {
        mssc &= UINT8_MAX;
      }
      bool matches;
      enum rtl8922ae_firmware_plan_result result = plan_security_section(data,
          plan->image_offset + image_bytes, section.offset, section.length,
          mssc, dsp_checksum, &plan->security, &section, &pool_bytes, &matches);
      if (result != RTL8922AE_FIRMWARE_PLAN_OK) {
        return result;
      }
      include = matches && !selected_security;
      selected_security |= include;
    }
    if (include) {
      size_t index = plan->section_count++;
      plan->sections[index] = section;
      copy_bytes(plan->header + fixed_bytes + index * FW_SECTION_HEADER_BYTES,
          header, FW_SECTION_HEADER_BYTES);
    }
    cursor += section.length + pool_bytes;
  }
  if (cursor != image_bytes) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  if (!selected_security) {
    return RTL8922AE_FIRMWARE_PLAN_NO_SECURITY;
  }
  plan->header_bytes = fixed_bytes + plan->section_count * FW_SECTION_HEADER_BYTES;
  plan->header_version = version;
  plan->idmem_share_mode = (word7 >> 18) & 0xf;
  copy_bytes(plan->version, image + 4, sizeof(plan->version));
  write_le32(plan->header + 24, (word6 & ~FW_SECTION_COUNT_MASK) |
      ((uint32_t)plan->section_count << FW_SECTION_COUNT_SHIFT));
  write_le32(plan->header + 28, (word7 & ~FW_PART_SIZE_MASK) |
      RTL8922AE_FIRMWARE_CHUNK_BYTES);
  return RTL8922AE_FIRMWARE_PLAN_OK;
}

enum rtl8922ae_firmware_plan_result rtl8922ae_firmware_plan(const void *data,
    size_t bytes, uint8_t live_cut, const uint8_t security_bytes[4],
    struct rtl8922ae_firmware_plan *plan)
{
  if (!plan) {
    return RTL8922AE_FIRMWARE_PLAN_INVALID;
  }
  *plan = (struct rtl8922ae_firmware_plan){0};
  enum rtl8922ae_firmware_plan_result result = build_plan(data, bytes, live_cut,
      security_bytes, plan);
  if (result != RTL8922AE_FIRMWARE_PLAN_OK) {
    *plan = (struct rtl8922ae_firmware_plan){0};
  }
  return result;
}

bool rtl8922ae_firmware_copy_section(const void *data, size_t bytes,
    const struct rtl8922ae_firmware_plan *plan, size_t index, size_t offset,
    void *destination, size_t length)
{
  if (!data || !plan || !destination || !length ||
      length > RTL8922AE_FIRMWARE_CHUNK_BYTES ||
      plan->section_count > RTL8922AE_FIRMWARE_MAX_SECTIONS ||
      index >= plan->section_count) {
    return false;
  }
  const struct rtl8922ae_firmware_section *section = &plan->sections[index];
  if (!span_valid(bytes, section->offset, section->length) ||
      !span_valid(section->length, offset, length) ||
      section->key_length > section->length ||
      !span_valid(bytes, section->key_offset, section->key_length)) {
    return false;
  }
  const uint8_t *asset = data;
  uint8_t *output = destination;
  size_t key_start = section->length - section->key_length;
  for (size_t i = 0; i < length; ++i) {
    size_t position = offset + i;
    output[i] = section->key_length && position >= key_start ?
        asset[section->key_offset + position - key_start] :
        asset[section->offset + position];
  }
  return true;
}
