#ifndef KERNEL_FB_EARLY_CONSOLE_H
#define KERNEL_FB_EARLY_CONSOLE_H

#include <kernel/boot.h>
#include <stdarg.h>
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

/* Enabled debugger panic, IF=0 after clock initialization. One CPU streams a
 * local-only message before terminal entry; no log/NIC path or allocation. */
void early_console_panic_vprintf(const char *format, va_list args)
  __attribute__((format(printf, 1, 0)));
bool early_console_panic_message_begin(void);
void early_console_panic_message_end(void);
/* Terminal entrants boundedly await the message. An interrupted owner marks
 * its own output failed, without retrying a possibly broken framebuffer. */
void early_console_panic_message_wait(void);

/* Presenter, under the log lock, before the first framebuffer write. True when
 * the console was retired (or never started) and the presenter owns the
 * screen; false when a panic owns it and the presenter must never draw. */
bool early_console_retire(void);

#endif
