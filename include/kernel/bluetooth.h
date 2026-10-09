#ifndef KERNEL_BLUETOOTH_H
#define KERNEL_BLUETOOTH_H

#include <stdbool.h>
#include <stdint.h>

struct usb_host_controller;
struct usb_host_device;

/* Owning USB worker, BSP/IF=1. Count each identified AX200 before transport
 * admission and select its host. A second candidate makes the singleton
 * unavailable, including when its transport cannot be configured. */
void bluetooth_hci_candidate(struct usb_host_controller *host);
/* Bind after both retained receives start. Seal inventory before initialization. */
void bluetooth_hci_attach(struct usb_host_controller *host,
    struct usb_host_device *device, uint8_t interface_number);
/* BSP/IF=0, after final USB publication. Require complete discovery on the
 * selected host; the global inventory can retain unsupported controllers. */
void bluetooth_hci_inventory_sealed(void);
/* Owning USB worker, BSP/IF=1. Both hooks run one bounded nonblocking tick:
 * collect/parse/account copied packets, service bounded queued request loans,
 * check deadlines and publish at most one command and one ACL packet.
 * No waits, allocation, recovery or receive replacement. Recursive drains
 * during physical failure do not reenter the active tick. */
void bluetooth_hci_progress(struct usb_host_controller *host);
void bluetooth_hci_drain_progress(struct usb_host_controller *host);
/* BSP, either IF state. Latch failure without performing USB operations. */
void bluetooth_hci_transport_failed(struct usb_host_controller *host);

#endif
