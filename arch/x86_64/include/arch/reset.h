#ifndef ARCH_RESET_H
#define ARCH_RESET_H

/* Last-resort reset for machines whose ACPI reset register is missing or did
 * not reset: pulse the 8042 reset line, then triple-fault this CPU. Disables
 * interrupts and never returns. */
[[noreturn]] void arch_reset_fallback(void);

#endif
