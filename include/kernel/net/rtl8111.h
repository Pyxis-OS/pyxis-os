#ifndef KERNEL_NET_RTL8111_H
#define KERNEL_NET_RTL8111_H

#include <kernel/boot.h>
#include <kernel/net/interface.h>

/* BSP/IF=0 before AP startup. Prepare each supported controller independently;
 * allocate rings with DMA/delivery disabled. Retain uncertain ownership. */
void rtl8111_prepare(const struct boot_info *boot);

struct rtl8111_controller;
/* Only identified supported XIDs are selectable. Incomplete identification or
 * lost candidate storage prevents a unique MAC match; unsupported XIDs do not. */
bool rtl8111_inventory_complete(void);
struct rtl8111_controller *rtl8111_first(void);
struct rtl8111_controller *rtl8111_next(const struct rtl8111_controller *controller);
const uint8_t *rtl8111_identity_mac(const struct rtl8111_controller *controller);

/* Sole BSP network worker, IF=1. Start once after binding, service bounded
 * completion batches; deadline also accepts IF=0. No runtime storage release. */
void rtl8111_start(struct rtl8111_controller *controller);
bool rtl8111_service(struct rtl8111_controller *controller);
bool rtl8111_next_deadline(struct rtl8111_controller *controller, uint64_t *deadline);
const uint8_t *rtl8111_mac(const struct rtl8111_controller *controller);
bool rtl8111_available(const struct rtl8111_controller *controller);
bool rtl8111_ready(const struct rtl8111_controller *controller);
/* Copy a complete frame into owned storage. All returns preserve the caller's
 * bytes; OK means queued, not delivered. Interrupt entry only acknowledges/wakes. */
enum net_result rtl8111_transmit(struct rtl8111_controller *controller,
    const void *frame, size_t length);
void rtl8111_interrupt(void);

#endif
