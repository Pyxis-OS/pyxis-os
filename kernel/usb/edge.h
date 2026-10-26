#ifndef USB_EDGE_H
#define USB_EDGE_H

#include <abi/gamepad.h>
#include <stdbool.h>
#include <stddef.h>

#define USB_EDGE_VENDOR 0x054c
#define USB_EDGE_PRODUCT 0x0df2
#define USB_EDGE_INTERFACE 3
#define USB_EDGE_ENDPOINT 0x84
#define USB_EDGE_INTERVAL 4
#define USB_EDGE_REPORT_BYTES 64
#define USB_EDGE_DESCRIPTOR_BYTES 389

bool usb_edge_descriptor(const uint8_t *descriptor, size_t bytes);
bool usb_edge_decode(const uint8_t *report, size_t bytes, struct gamepad_state *state);

#endif
