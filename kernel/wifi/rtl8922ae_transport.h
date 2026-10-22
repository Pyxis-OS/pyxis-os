/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef RTL8922AE_TRANSPORT_H
#define RTL8922AE_TRANSPORT_H

#include <kernel/boot.h>
#include <kernel/pci.h>
#include "rtl8922ae_firmware.h"
#include <abi/wifi.h>

bool rtl8922ae_transport_prepare(const struct boot_info *boot, struct pci_device *device,
    size_t firmware_bytes, const char **detail);
bool rtl8922ae_transport_begin(uint8_t cut, uint64_t deadline, const char **detail);
bool rtl8922ae_transport_download(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_plan *plan, uint64_t deadline, const char **detail);
bool rtl8922ae_transport_stop(uint64_t deadline);
void rtl8922ae_transport_interrupt(void);
void rtl8922ae_transport_status(struct wifi_status_reply *status);

#endif
