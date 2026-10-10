#ifndef KERNEL_FORMAT_H
#define KERNEL_FORMAT_H

#include <stdarg.h>

/* Shared with klog: %s %c %p %d %u %x, l/ll/z integer lengths and %%.
 * %d, %u and %x also take an optional 0 flag and a width of at most 32, so
 * %02x, %016lx and %5u pad on the left with zeros or spaces (a zero-padded
 * negative number keeps its sign first). There are no other flags, precision
 * or floating-point conversions, and %s %c %p take no width.
 * Unsupported conversions emit <format?> and consume no argument; a trailing %
 * emits nothing. Padding makes a result longer than its digits, so sprintf
 * buffers must allow for the width.
 *
 * Buffer must hold the entire result plus its terminating NUL and must not
 * overlap the format or string arguments. Returns the byte count excluding
 * the terminator, or -1 if it exceeds INT_MAX. No allocation or log output. */
int sprintf(char *buffer, const char *format, ...)
  __attribute__((format(printf, 2, 3)));
int vsprintf(char *buffer, const char *format, va_list args)
  __attribute__((format(printf, 2, 0)));

#endif
