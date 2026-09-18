#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H
#include <stdarg.h>
/* Supported: %s %c %p %d %u %x, with l/ll/z integer lengths and %%. */
void kvlog(const char *format, va_list args);
void klog(const char *format, ...) __attribute__((format(printf, 1, 2)));

void log_putc(char c);

#endif
