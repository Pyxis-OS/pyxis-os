#ifndef USB_NCM_PROVIDER_H
#define USB_NCM_PROVIDER_H

#include <kernel/net/usb_ncm.h>

#define USB_NCM_CANDIDATES 2u
#define USB_NCM_FRAME_QUEUE 16u
#define USB_NCM_SERVICE_BUDGET 8u

struct usb_host_controller;
struct usb_ncm_pool;

/* BSP/IF=0 before AP startup. Register every USB controller index once, including
 * sources without a pool. Empty/default-disabled discovery is complete. */
bool usb_ncm_inventory_prepare(size_t controller_count);
struct usb_ncm_pool *usb_ncm_prepare(struct usb_host_controller *host, size_t index);
void usb_ncm_release_prepared(struct usb_ncm_pool *pool);
/* BSP/IF=0 after bounded initial discovery/retries, including failed sources.
 * Finish is idempotent. inspected=false records uncertainty relevant to NIC
 * matching, not unrelated unsupported USB classes. */
void usb_ncm_discovery_finish(struct usb_ncm_pool *pool, bool inspected);
void usb_ncm_discovery_finish_at(size_t index, bool inspected);

/* Owning USB worker, IF=0. Admit once with checked unicast MAC; the class retains
 * branch/function identity and reuses the same logical NIC on accepted recovery.
 * A new physical generation must withdraw before any hardware waits. */
struct usb_ncm_nic *usb_ncm_admit(struct usb_ncm_pool *pool, const uint8_t mac[6]);
void usb_ncm_publish(struct usb_ncm_nic *nic,
    bool prepared, bool ready, bool known, bool up);
void usb_ncm_withdraw(struct usb_ncm_nic *nic, bool retry_pending);

/* Owning USB worker, IF=1. Take copies one queued TX frame without USB submission;
 * false means no frame or unavailable, with length zero. Loss during the copy
 * may have changed destination bytes; they must not be submitted. Receive
 * copies one checked Ethernet frame; full/lost admission drops it. No retained
 * caller pointers. Ordinary TX batching/deadlines stay with the USB worker. */
bool usb_ncm_bound(const struct usb_ncm_nic *nic);
bool usb_ncm_take_transmit(struct usb_ncm_nic *nic,
    void *destination, size_t capacity, size_t *length);
bool usb_ncm_receive(struct usb_ncm_nic *nic, const void *frame, size_t length);

#endif
