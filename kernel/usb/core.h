#ifndef USB_CORE_H
#define USB_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* Prepare/release only before publication, on the BSP with IF=0. Enumeration
 * runs once on the BSP controller worker with one absolute overall deadline. */
bool usb_prepare(void);
void usb_release_prepared(void);
void usb_enumerate(uint64_t deadline);

#endif
