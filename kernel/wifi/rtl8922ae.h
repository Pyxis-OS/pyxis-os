#ifndef KERNEL_WIFI_RTL8922AE_H
#define KERNEL_WIFI_RTL8922AE_H

#include <abi/wifi.h>

/* BSP boot preparation only: no device writes, DMA or firmware execution. */
void rtl8922ae_prepare(void);
void rtl8922ae_status(struct wifi_status_reply *status);

#endif
