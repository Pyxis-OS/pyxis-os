#include <kernel/log.h>

void klog(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  kvlog(format, args);
  va_end(args);
}
