/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright(c) 2019-2020 Realtek Corporation
 * Format reference: Linux rtw89, BSD alternative; see kernel/wifi/NOTICE.
 */
#ifndef RTL8922AE_FIRMWARE_H
#define RTL8922AE_FIRMWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct rtl8922ae_firmware_info {
  size_t bytes;
  size_t normal_suits;
  size_t wowlan_suits;
  size_t log_suits;
  size_t bbmcu_images;
  size_t sections;
  size_t security_sections;
  size_t formatted_mss_pools;
  size_t elements;
  uint8_t container_version[4];
  uint8_t mfw_entries;
};

/* The caller admits the pinned asset; this only checks its container spans.
 * Input remains immutable during this call and must not overlap info. The
 * result contains no borrowed pointers or device suitability/authorization.
 * Failure clears info; a null info pointer is rejected.
 */
bool rtl8922ae_firmware_inspect(const void *data, size_t bytes,
    struct rtl8922ae_firmware_info *info);

#endif
