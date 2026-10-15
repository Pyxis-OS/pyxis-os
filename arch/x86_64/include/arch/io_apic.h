#ifndef ARCH_IO_APIC_H
#define ARCH_IO_APIC_H

#include <kernel/boot.h>

/* BSP bootstrap: capture ACPI routing before the CR3 switch, then initialize
 * the mapped controllers with IF=0. Only the PS/2 keyboard and mouse routes
 * and the ACPI SCI are installed, all masked; the mouse and SCI are optional. */
void io_apic_prepare(const struct boot_info *boot);
size_t io_apic_controller_count(void);
uint64_t io_apic_physical_address(size_t index);
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
/* Uninstall the SCI consumer, preserving any other consumer of the pin. */
void io_apic_sci_disable(void);
/* BSP interrupt entry; dispatch every enabled consumer of this input vector. */
void io_apic_interrupt(uint8_t vector);

#endif
