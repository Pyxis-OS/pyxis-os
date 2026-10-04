#ifndef RTL8111_IO_SETUP_H
#define RTL8111_IO_SETUP_H

#include <stdbool.h>
#include <kernel/mm/types.h>

struct rtl8111_controller;

/* BSP/IF=0 after controller and PHY preparation. Programs owned DMA ring
 * addresses while RX/TX, bus mastering and interrupt delivery remain disabled. */
bool rtl_io_prepare(struct rtl8111_controller *controller, phys_addr_t rx, phys_addr_t tx);

#endif
