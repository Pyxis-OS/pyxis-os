#include <arch/console.h>
#include <kernel/log.h>
#include <stdint.h>
#include <stddef.h>
#include <kernel/fb/tty.h>

#define UINT64_DECIMAL_DIGITS 20

enum integer_length {
  LENGTH_INT,
  LENGTH_LONG,
  LENGTH_LONG_LONG,
  LENGTH_SIZE,
};

static void put_char(char c)
{
  console_putc(c);
  if (get_tty()->initialized)
    tty_put_char(c);
}

static void put_string(const char *text)
{
  for (; *text; ++text) {
    put_char(*text);
  }
}

static void put_unsigned(uint64_t value, unsigned radix)
{
  char digits[UINT64_DECIMAL_DIGITS];
  size_t count = 0;

  do {
    digits[count] = "0123456789abcdef"[value % radix];
    ++count;
    value /= radix;
  } while (value);

  while (count) {
    --count;
    put_char(digits[count]);
  }
}

static int64_t next_signed_integer(va_list args, enum integer_length length)
{
  switch (length) {
  case LENGTH_LONG:
    return va_arg(args, long);
  case LENGTH_LONG_LONG:
    return va_arg(args, long long);
  case LENGTH_SIZE:
    return va_arg(args, ptrdiff_t);
  default:
    return va_arg(args, int);
  }
}

static uint64_t next_unsigned_integer(va_list args, enum integer_length length)
{
  switch (length) {
  case LENGTH_LONG:
    return va_arg(args, unsigned long);
  case LENGTH_LONG_LONG:
    return va_arg(args, unsigned long long);
  case LENGTH_SIZE:
    return va_arg(args, size_t);
  default:
    return va_arg(args, unsigned int);
  }
}

void kvlog(const char *format, va_list args)
{
  while (*format) {
    if (*format != '%') {
      put_char(*format);
      ++format;
      continue;
    }

    ++format;
    enum integer_length length = LENGTH_INT;
    if (*format == 'l') {
      length = LENGTH_LONG;
      ++format;
      if (*format == 'l') {
        length = LENGTH_LONG_LONG;
        ++format;
      }
    } else if (*format == 'z') {
      length = LENGTH_SIZE;
      ++format;
    }

    char conversion = *format;
    if (!conversion) {
      return;
    }
    ++format;

    switch (conversion) {
    case '%':
      put_char('%');
      break;
    case 's': {
      const char *text = va_arg(args, const char *);
      put_string(text ? text : "(null)");
      break;
    }
    case 'c':
      put_char((char)va_arg(args, int));
      break;
    case 'p':
      put_string("0x");
      put_unsigned((uintptr_t)va_arg(args, void *), 16);
      break;
    case 'd': {
      int64_t signed_value = next_signed_integer(args, length);
      uint64_t magnitude = (uint64_t)signed_value;
      if (signed_value < 0) {
        put_char('-');
        /* Unsigned negation also handles INT64_MIN without signed overflow. */
        magnitude = 0 - magnitude;
      }

      put_unsigned(magnitude, 10);
      break;
    }
    case 'u':
    case 'x': {
      uint64_t value = next_unsigned_integer(args, length);
      put_unsigned(value, conversion == 'x' ? 16 : 10);
      break;
    }
    default:
      put_string("<format?>");
      break;
    }
  }
}
