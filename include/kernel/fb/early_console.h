#ifndef KERNEL_FB_EARLY_CONSOLE_H
#define KERNEL_FB_EARLY_CONSOLE_H

#include <kernel/boot.h>
#include <stdint.h>

/* Plain-text boot console drawn directly on the boot framebuffer until the
 * display presenter takes over. No heap, TTY or framebuffer reads. Ordinary
 * output requires the log lock. After retirement, panic may reclaim the
 * physical display; see docs/kernel/early-console.md for the panic
 * ownership rules. */

/* BSP, once, after the framebuffer is validated. address maps fb->physical. */
void early_console_start(const struct boot_framebuffer *fb, uintptr_t address);
/* BSP, before APs start, immediately after the mapping at address replaces
 * the previous one. */
void early_console_rebind(uintptr_t address);

/* Under the log lock. Drops output unless the console is active. */
void early_console_putc(char c);

/* IF=0, without the log lock. The first panicking CPU takes the console;
 * including after presenter handoff. Later panics and nested faults stay
 * serial-only. */
void early_console_panic_begin(void);
/* Panic output path: draws only on the owning CPU. */
void early_console_panic_putc(char c);

/* Presenter, under the log lock, before the first framebuffer write. True when
 * the console was retired (or never started) and the presenter owns the
 * screen; false when a panic owns it and the presenter must never draw. */
bool early_console_retire(void);

#endif
