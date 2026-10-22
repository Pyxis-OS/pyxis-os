/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020 Realtek Corporation
 * Format reference: Linux rtw89 fw.c/fw.h at
 * 7d0a66e4bb9081d75c82ec4957c50034cb0ea449, BSD alternative.
 * See kernel/wifi/NOTICE for the complete notice.
 */
#include "rtl8922ae_firmware.h"

#define MFW_SIGNATURE UINT8_C(0xff)
#define MFW_HEADER_BYTES 16u
#define MFW_ENTRY_BYTES 16u
#define MFW_NORMAL 1u
#define MFW_WOWLAN 3u
#define MFW_LOGFMT 255u
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
  if (pool[20] != FW_MSS_POOL_REMAP_BITMASK ||
      read_le32(pool + 8) != FW_MSS_POOL_HEADER_BYTES) {
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
  if (bytes < FW_V1_HEADER_BYTES ||
      read_le32(data + 12) >> FW_HEADER_VERSION_SHIFT != 1) {
    return false;
  }

  uint32_t word6 = read_le32(data + 24);
  uint32_t word7 = read_le32(data + 28);
  size_t sections = (word6 >> FW_SECTION_COUNT_SHIFT) & UINT8_MAX;
  size_t base_bytes = FW_V1_HEADER_BYTES + sections * FW_SECTION_HEADER_BYTES;
  if (!sections || base_bytes > bytes) {
    return false;
  }

  size_t cursor = base_bytes;
  if (word7 & FW_DYNAMIC_HEADER) {
    size_t header_bytes = read_le32(data + 20) >> 16;
    if (header_bytes < base_bytes || header_bytes > bytes ||
        header_bytes - base_bytes < FW_DYNAMIC_HEADER_BYTES ||
        read_le32(data + base_bytes) != header_bytes - base_bytes) {
      return false;
    }
    cursor = header_bytes;
  }

  for (size_t i = 0; i < sections; ++i) {
    const uint8_t *header = data + FW_V1_HEADER_BYTES +
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
      uint8_t mssc = read_le32(header + 8) & UINT8_MAX;
      if (mssc == FW_FORMATTED_MSSC) {
        if (!inspect_mss_pool(data + cursor, bytes - cursor, section_bytes,
                (word6 & FW_DSP_CHECKSUM) != 0, &security_bytes)) {
          return false;
        }
        ++info->formatted_mss_pools;
      } else {
        size_t signature_bytes = FW_SECURITY_SIGNATURE_BYTES;
        if (word6 & FW_DSP_CHECKSUM) {
          signature_bytes += FW_CHECKSUM_BYTES;
        }
        security_bytes = mssc * signature_bytes;
        if (!span_valid(bytes - cursor, section_bytes, security_bytes)) {
          return false;
        }
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
  while (offset < bytes) {
    if (!span_valid(bytes, offset, FW_ELEMENT_HEADER_BYTES)) {
      return false;
    }
    const uint8_t *header = data + offset;
    uint32_t id = read_le32(header);
    size_t length = read_le32(header + 4);
    size_t payload = offset + FW_ELEMENT_HEADER_BYTES;
    if (!span_valid(bytes, payload, length)) {
      return false;
    }

    if (id == FW_ELEMENT_BBMCU0 || id == FW_ELEMENT_BBMCU1) {
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
    if (!align_element(end, &offset) || offset > bytes) {
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
    if (!length || offset < directory_bytes || !span_valid(bytes, offset, length)) {
      return false;
    }
    size_t end = offset + length;
    if (end > payload_end) {
      payload_end = end;
    }

    for (size_t j = 0; j < i; ++j) {
      const uint8_t *previous = firmware + MFW_HEADER_BYTES + j * MFW_ENTRY_BYTES;
      size_t previous_offset = read_le32(previous + 4);
      size_t previous_end = previous_offset + read_le32(previous + 8);
      if (offset < previous_end && previous_offset < end) {
        return false;
      }
    }

    switch (entry[1]) {
    case MFW_NORMAL:
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
      return false;
    }
  }

  if (payload_end != bytes) {
    size_t elements_offset;
    if (!align_element(payload_end, &elements_offset) || elements_offset > bytes ||
        !inspect_elements(firmware, bytes, elements_offset, &result)) {
      return false;
    }
  }
  *info = result;
  return true;
}
