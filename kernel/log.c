#include <kernel/log.h>
#include <arch/console.h>
#include <kernel/fb/tty.h>

void klog(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  kvlog(format, args);
  va_end(args);
}

void log_putc(char c)
{
  console_putc(c);
  if (get_tty()->initialized)
    tty_put_char(c);
}
