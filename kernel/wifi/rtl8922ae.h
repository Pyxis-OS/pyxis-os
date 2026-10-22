#ifndef KERNEL_WIFI_RTL8922AE_H
#define KERNEL_WIFI_RTL8922AE_H

#include <abi/wifi.h>

struct boot_info;
/* Opt-in boot reservation followed by one BSP firmware worker. No scan/data. */
void rtl8922ae_prepare(const struct boot_info *boot);
void rtl8922ae_start(void);
void rtl8922ae_interrupt(void);
void rtl8922ae_status(struct wifi_status_reply *status);

#endif
