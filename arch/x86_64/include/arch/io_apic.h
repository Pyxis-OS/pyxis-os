#ifndef ARCH_IO_APIC_H
#define ARCH_IO_APIC_H

#include <kernel/boot.h>

/* BSP bootstrap: capture ACPI routing before the CR3 switch, then initialize
 * the mapped controller with IF=0. Only the PS/2 keyboard and mouse routes
 * are installed, both masked; the mouse route is optional. */
void io_apic_prepare(const struct boot_info *boot);
uint64_t io_apic_physical_address(void);
bool io_apic_init(void);
void io_apic_keyboard_enable(void);
bool io_apic_mouse_available(void);
void io_apic_mouse_enable(void);

#endif
