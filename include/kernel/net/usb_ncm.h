#ifndef KERNEL_NET_USB_NCM_H
#define KERNEL_NET_USB_NCM_H

#include <kernel/net/interface.h>

struct usb_ncm_nic;

/* BSP network worker, IF=1. Registry records and identity remain until reboot.
 * Discovery includes bounded initial USB retries, independently of unrelated
 * USB observation incompleteness. Queries never issue USB operations. */
bool usb_ncm_inventory_complete(void);
struct usb_ncm_nic *usb_ncm_first(void);
struct usb_ncm_nic *usb_ncm_next(const struct usb_ncm_nic *nic);
uint32_t usb_ncm_controller_id(const struct usb_ncm_nic *nic);
const uint8_t *usb_ncm_identity_mac(const struct usb_ncm_nic *nic);
const uint8_t *usb_ncm_mac(const struct usb_ncm_nic *nic);
bool usb_ncm_prepared(const struct usb_ncm_nic *nic);
bool usb_ncm_carrier(const struct usb_ncm_nic *nic, bool *up);
bool usb_ncm_ready(const struct usb_ncm_nic *nic);
bool usb_ncm_available(const struct usb_ncm_nic *nic);

/* Binding publishes activation to the owning USB worker. Service delivers only
 * copied software frames. A claimed RX frame pins its epoch until delivery
 * returns; replacement readiness stays closed until an old claim is released.
 * TX copies into a bounded queue; every return preserves
 * caller storage and OK means queued. No DMA or USB helper crosses this boundary. */
void usb_ncm_start(struct usb_ncm_nic *nic);
bool usb_ncm_service(const struct net_interface *iface,
    struct usb_ncm_nic *nic);
enum net_result usb_ncm_transmit(struct usb_ncm_nic *nic,
    const void *frame, size_t length);

#endif
