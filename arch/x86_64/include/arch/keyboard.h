#ifndef ARCH_KEYBOARD_H
#define ARCH_KEYBOARD_H

/* BSP, IF=0; the keyboard I/O APIC route must already be initialized/masked. */
void ps2_keyboard_init(void);
/* IRQ entry only collects bytes. Caller acknowledges the local APIC afterward. */
void ps2_keyboard_interrupt(void);

#endif
