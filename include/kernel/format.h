#ifndef KERNEL_FORMAT_H
#define KERNEL_FORMAT_H

#include <stdarg.h>

/* Shared with klog: %s %c %p %d %u %x, l/ll/z integer lengths and %%.
 * No flags, field widths, precision or floating-point conversions.
 * Unsupported conversions emit <format?>; a trailing % emits nothing.
 *
 * Buffer must hold the entire result plus its terminating NUL and must not
 * overlap the format or string arguments. Returns the byte count excluding
 * the terminator, or -1 if it exceeds INT_MAX. No allocation or log output. */
int sprintf(char *buffer, const char *format, ...)
  __attribute__((format(printf, 2, 3)));
int vsprintf(char *buffer, const char *format, va_list args)
  __attribute__((format(printf, 2, 0)));

#endif
