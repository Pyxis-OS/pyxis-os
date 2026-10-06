#ifndef ARCH_IO_APIC_H
#define ARCH_IO_APIC_H

#include <kernel/boot.h>

/* BSP bootstrap: capture ACPI routing before the CR3 switch, then initialize
 * the mapped controller with IF=0. Only the PS/2 keyboard and mouse routes
 * and the ACPI SCI are installed, all masked; the mouse and SCI are optional. */
void io_apic_prepare(const struct boot_info *boot);
uint64_t io_apic_physical_address(void);
bool io_apic_init(void);
void io_apic_keyboard_enable(void);
bool io_apic_mouse_available(void);
void io_apic_mouse_enable(void);
/* False, with a zero IRQ, when the FADT's SCI has no supported route.
 * Otherwise IRQ is the FADT's SCI interrupt number, before any override. */
bool io_apic_sci_available(unsigned *irq);
/* BSP, IF=0: SCI interrupt entry masks the level-triggered input before EOI;
 * the ACPI worker unmasks it after handling the event. */
void io_apic_sci_mask(void);
void io_apic_sci_unmask(void);

#endif
