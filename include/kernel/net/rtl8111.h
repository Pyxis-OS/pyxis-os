#ifndef KERNEL_NET_RTL8111_H
#define KERNEL_NET_RTL8111_H

#include <kernel/boot.h>

/* BSP/IF=0 before AP startup. Prepare each supported controller independently;
 * retain uncertain hardware ownership until reboot. No rings, DMA or delivery. */
void rtl8111_prepare(const struct boot_info *boot);

#endif
