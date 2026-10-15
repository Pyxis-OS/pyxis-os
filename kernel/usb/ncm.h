#ifndef USB_NCM_H
#define USB_NCM_H

#include "ncm_provider.h"
#include "ncm_wire.h"

struct usb_ncm_transports;
struct usb_ncm_transport;

/* Prepared before AP startup; each attempted generation consumes one entry.
 * Only its BSP controller worker uses class state, control and DMA interfaces. */
struct usb_ncm_transports *usb_ncm_transports_prepare(struct usb_host_controller *host);
void usb_ncm_transports_release(struct usb_ncm_transports *transports);
enum usb_result usb_ncm_setup(struct usb_ncm_transports *transports,
    struct usb_ncm_pool *pool, struct usb_host_device *device,
    const struct usb_ncm_function *function, struct usb_ncm_nic *existing,
    uint64_t deadline, struct usb_ncm_transport **transport);
struct usb_ncm_nic *usb_ncm_transport_nic(const struct usb_ncm_transport *transport);
/* Owning worker, after confirmed endpoint fences. Requires a lost transport;
 * transfer its logical NIC to recovery while retaining old transport storage.
 * Later loss callbacks on the detached generation cannot withdraw a replacement. */
struct usb_ncm_nic *usb_ncm_transport_detach(struct usb_ncm_transport *transport);
bool usb_ncm_transport_failed(const struct usb_ncm_transport *transport);
void usb_ncm_transport_lost(struct usb_ncm_transport *transport, bool retry_pending);
void usb_ncm_transport_process(struct usb_ncm_transport *transport);
bool usb_ncm_transport_deadline(const struct usb_ncm_transport *transport, uint64_t *deadline);

#endif
