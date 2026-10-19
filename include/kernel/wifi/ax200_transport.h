#ifndef KERNEL_WIFI_AX200_TRANSPORT_H
#define KERNEL_WIFI_AX200_TRANSPORT_H

#include <kernel/wifi.h>

/* Single uniquely selected AX200, permanent boot ownership. All I/O helpers
 * execute on the sole BSP Wi-Fi worker (IF=1), except prepare/status/notify. */
bool ax200_transport_prepare(const struct boot_info *boot);
bool ax200_transport_boot(uint64_t deadline_ns);
bool ax200_transport_stop(uint64_t deadline_ns);
void ax200_transport_disable(const char *reason);
void ax200_transport_status(struct wifi_status_reply *status);
bool ax200_command(uint8_t group, uint8_t opcode, const void *payload, size_t bytes,
    void *reply, size_t capacity, size_t *received, uint64_t deadline_ns);
/* Drains bounded owned RX; calls ax200_firmware_notify synchronously. */
bool ax200_transport_poll(void);
bool ax200_transport_drained(void);
/* BSP interrupt path, IF=0; acknowledge and wake only. */
bool ax200_transport_interrupt(void);
/* BSP reschedule entry, IF=0: collect a durable remote cancellation wake. */
void ax200_transport_remote_interrupt(void);
bool ax200_transport_address(uint8_t address[6]);
bool ax200_transport_ltr_enabled(void);
uint64_t ax200_transport_event(void);
void ax200_transport_wait(uint64_t observed, uint64_t deadline_ns);
void ax200_transport_notify(void);
bool ax200_transport_rfkill(void);
bool ax200_firmware_api(unsigned bit);
bool ax200_firmware_capability(unsigned bit);
int ax200_command_version(uint8_t group, uint8_t opcode, bool notification);
uint32_t ax200_firmware_phy(void);
/* Receiver only borrows payload until return; never logs raw identifiers. */
void ax200_firmware_notify(uint8_t group, uint8_t opcode,
    const uint8_t *payload, size_t bytes);

#endif
