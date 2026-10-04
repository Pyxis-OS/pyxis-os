#ifndef BLOCK_REGISTRY_H
#define BLOCK_REGISTRY_H

#include <stddef.h>

struct usb_block_device;

/* BSP/IF=0. Reserve before AP startup; append terminal USB candidates without
 * allocation during boot discovery. Entries and IDs survive until reboot. */
void block_prepare(void);
size_t block_registry_capacity(void);
void block_register_usb(struct usb_block_device *device);

#endif
