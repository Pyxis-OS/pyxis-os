#ifndef ARCH_BACKTRACE_H
#define ARCH_BACKTRACE_H

#include <stdbool.h>
#include <stdint.h>

#define ARCH_BACKTRACE_FRAMES 16
/* The largest kernel stack is Limine's 64 KiB boot stack; task, AP and
 * double-fault stacks are 16 KiB. A walk never strays farther than this from
 * its first frame. */
#define ARCH_BACKTRACE_STACK_SPAN (64 * 1024)

/* Panic and fault paths on any CPU, interrupts disabled, after
 * klog_panic_begin: no heap, locks or trust in GS. Logs the CPU, then RIP as
 * "rip" when it is the failing instruction or "caller" when it is a return
 * address (a symbolizer then looks up the byte before it), and up to
 * ARCH_BACKTRACE_FRAMES return addresses from the frame-pointer chain that
 * starts at FRAME, the frame pointer of the function containing RIP. Every
 * frame is checked before it is read, and the walk stops at the first one
 * that is null, non-canonical, misaligned, unmapped, not above the previous
 * frame or beyond the stack span. A fault inside the report doesn't walk again. */
void arch_fatal_report(uintptr_t rip, bool rip_returns, uintptr_t frame);

#endif
