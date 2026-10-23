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
/* Both plans are validated/staged in retained DMA storage before the single
 * CPU startup. BB preinitialization must already have completed. */
bool rtl8922ae_transport_download_onephy(const void *asset, size_t bytes,
    const struct rtl8922ae_firmware_plan *wcpu,
    const struct rtl8922ae_firmware_plan *bbmcu0, uint64_t deadline, const char **detail);

#define RTL8922AE_H2C_PAYLOAD_BYTES 2000u
/* BSP worker, IF=1, serialized by its caller. Copies payload before submission;
 * success proves device consumption of the DMA buffer, never a firmware ACK.
 * Failure after publication makes the transport unusable until terminal stop.
 * Sequence output is optional. No packet or operation authority is inferred. */
bool rtl8922ae_transport_h2c(uint8_t category, uint8_t class_id, uint8_t function,
    bool receive_ack, bool done_ack, const void *payload, size_t bytes,
    uint8_t *sequence, uint64_t deadline, const char **detail);
bool rtl8922ae_transport_poll(void);
/* Direct worker-only delivery of a checked complete C2H envelope. Borrowed DMA
 * storage is valid only for this call; false latches a transport fault. */
bool rtl8922ae_runtime_receive(const void *data, size_t bytes);
bool rtl8922ae_transport_stop(uint64_t deadline, const char **detail);
void rtl8922ae_transport_interrupt(void);
void rtl8922ae_transport_status(struct wifi_status_reply *status);

#endif
