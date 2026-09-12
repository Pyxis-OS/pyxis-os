#include <arch/console.h>
#include <kernel/log.h>
#include <stdint.h>
#include <stddef.h>

static void put_string(const char *s)
{
  for (; *s; ++s) {
    console_putc(*s);
  }
}

static void put_unsigned(uint64_t value, unsigned base)
{
  char digits[20];
  size_t count = 0;
  do {
    digits[count++] = "0123456789abcdef"[value % base];
    value /= base;
  } while (value);
  while (count) {
    console_putc(digits[--count]);
  }
}

void kvlog(const char *format, va_list args)
{
  while (*format) {
    if (*format != '%') {
      console_putc(*format++);
      continue;
    }
    ++format;
    enum { NORMAL, LONG, LONG_LONG, SIZE } length = NORMAL;
    if (*format == 'l') {
      length = LONG;
      if (*++format == 'l') {
        length = LONG_LONG;
        ++format;
      }
    } else if (*format == 'z') {
      length = SIZE;
      ++format;
    }
    char conversion = *format;
    if (!conversion) {
      return;
    }
    ++format;
    switch (conversion) {
    case '%': console_putc('%'); break;
    case 's': {
      const char *s = va_arg(args, const char *);
      put_string(s ? s : "(null)");
      break;
    }
    case 'c': console_putc((char)va_arg(args, int)); break;
    case 'p':
      put_string("0x");
      put_unsigned((uintptr_t)va_arg(args, void *), 16);
      break;
    case 'd':
    case 'u':
    case 'x': {
      uint64_t value;
      if (conversion == 'd') {
        int64_t signed_value;
        switch (length) {
        case LONG: signed_value = va_arg(args, long); break;
        case LONG_LONG: signed_value = va_arg(args, long long); break;
        case SIZE: signed_value = va_arg(args, ptrdiff_t); break;
        default: signed_value = va_arg(args, int); break;
        }
        value = (uint64_t)signed_value;
        if (signed_value < 0) {
          console_putc('-');
          value = 0 - value;
        }
      } else {
        switch (length) {
        case LONG: value = va_arg(args, unsigned long); break;
        case LONG_LONG: value = va_arg(args, unsigned long long); break;
        case SIZE: value = va_arg(args, size_t); break;
        default: value = va_arg(args, unsigned int); break;
        }
      }
      put_unsigned(value, conversion == 'x' ? 16 : 10);
      break;
    }
    default: put_string("<format?>"); break;
    }
  }
}
