#ifndef USB_CORE_H
#define USB_CORE_H

#include <abi/system_info.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct usb_host_controller;
struct usb_discovery;

/* Registry and allocations are prepared on the BSP before AP startup. Every PCI
 * USB controller is retained, including unsupported host interfaces. */
void usb_inventory_prepare(void);
size_t usb_inventory_controller_count(void);
size_t usb_inventory_pci_index(size_t index);
void usb_inventory_controller_failed(size_t index);
struct usb_discovery *usb_prepare(struct usb_host_controller *host, size_t index);
void usb_release_prepared(struct usb_discovery *discovery);
void usb_enumerate(struct usb_discovery *discovery, uint64_t deadline);
size_t usb_storage_capacity(void);
void usb_storage_process(struct usb_discovery *discovery);
/* Owning BSP worker: inner event progress never waits; outer progress performs
 * one retained hotplug state-machine step. Failure releases input ownership. */
void usb_hid_drain_progress(struct usb_host_controller *host);
void usb_hid_process(struct usb_discovery *discovery);
void usb_hid_controller_failed(struct usb_host_controller *host);
/* BSP/IF=0 physical-input admission check; no USB operations. */
bool usb_hid_input_complete(void);

/* Completeness is scoped to this retained host's final discovery record;
 * unsupported or incomplete controllers elsewhere do not change its result. */
bool usb_inventory_host_complete(const struct usb_host_controller *host);
/* Acquire publication before copying immutable records on any CPU. */
void usb_inventory_read(struct system_info_usb *reply);
bool usb_inventory_read_controller(uint64_t index, struct system_info_usb_controller *reply);
bool usb_inventory_read_device(uint64_t index, struct system_info_usb_device *reply);
bool usb_inventory_read_interface(uint64_t index, struct system_info_usb_interface *reply);

#endif
