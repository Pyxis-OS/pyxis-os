#include <kernel/log.h>
#include <kernel/format.h>
#include <kernel/fb/early_console.h>
#include <arch/cpu.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define UINT64_DECIMAL_DIGITS 20
/* Widest field %d, %u and %x pad to; a larger width is unsupported. */
#define FORMAT_WIDTH_MAX 32

enum integer_length {
  LENGTH_INT,
  LENGTH_LONG,
  LENGTH_LONG_LONG,
  LENGTH_SIZE,
};

struct format_output {
  char *buffer;
  size_t length;
  bool panic_display;
};

static void put_char(struct format_output *output, char c)
{
  if (output->buffer) {
    output->buffer[output->length] = c;
  } else if (output->panic_display) {
    early_console_panic_putc(c);
  } else {
    log_putc(c);
  }
  ++output->length;
}

static void put_string(struct format_output *output, const char *text)
{
  for (; *text; ++text) {
    put_char(output, *text);
  }
}

static void put_padding(struct format_output *output, char c, size_t count)
{
  while (count) {
    put_char(output, c);
    --count;
  }
}

/* Digits of value, preceded by a minus sign if negative, right-aligned in at
 * least width cells: filled with spaces before the sign, or with zeros after
 * it. */
static void put_number(struct format_output *output, uint64_t value, unsigned radix,
                       bool negative, size_t width, bool zero_fill)
{
  char digits[UINT64_DECIMAL_DIGITS];
  size_t count = 0;

  do {
    digits[count] = "0123456789abcdef"[value % radix];
    ++count;
    value /= radix;
  } while (value);

  size_t used = count + negative;
  size_t padding = width > used ? width - used : 0;
  if (!zero_fill) {
    put_padding(output, ' ', padding);
  }
  if (negative) {
    put_char(output, '-');
  }
  if (zero_fill) {
    put_padding(output, '0', padding);
  }

  while (count) {
    --count;
    put_char(output, digits[count]);
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

static void format_output(struct format_output *output, const char *format,
                           va_list args)
{
  while (*format) {
    if (*format != '%') {
      put_char(output, *format);
      ++format;
      continue;
    }

    ++format;
    bool zero_fill = false;
    if (*format == '0') {
      zero_fill = true;
      ++format;
    }
    size_t width = 0;
    bool has_width = false, width_valid = true;
    while (*format >= '0' && *format <= '9') {
      has_width = true;
      width = width * 10 + (size_t)(*format - '0');
      if (width > FORMAT_WIDTH_MAX) {
        width_valid = false;
        width = FORMAT_WIDTH_MAX + 1; /* Keep consuming digits without overflow. */
      }
      ++format;
    }
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

    /* Only integers take a width or zero fill; anything else with one is not
     * supported, as is a width past the limit. */
    bool integer = conversion == 'd' || conversion == 'u' || conversion == 'x';
    if (!width_valid || ((zero_fill || has_width) && !integer)) {
      put_string(output, "<format?>");
      continue;
    }

    switch (conversion) {
    case '%':
      put_char(output, '%');
      break;
    case 's': {
      const char *text = va_arg(args, const char *);
      put_string(output, text ? text : "(null)");
      break;
    }
    case 'c':
      put_char(output, (char)va_arg(args, int));
      break;
    case 'p':
      put_string(output, "0x");
      put_number(output, (uintptr_t)va_arg(args, void *), 16, false, 0, false);
      break;
    case 'd': {
      int64_t signed_value = next_signed_integer(args, length);
      uint64_t magnitude = (uint64_t)signed_value;
      if (signed_value < 0) {
        /* Unsigned negation also handles INT64_MIN without signed overflow. */
        magnitude = 0 - magnitude;
      }

      put_number(output, magnitude, 10, signed_value < 0, width, zero_fill);
      break;
    }
    case 'u':
    case 'x': {
      uint64_t value = next_unsigned_integer(args, length);
      put_number(output, value, conversion == 'x' ? 16 : 10, false, width, zero_fill);
      break;
    }
    default:
      put_string(output, "<format?>");
      break;
    }
  }
}

void kvlog(const char *format, va_list args)
{
  uint64_t flags = cpu_save_interrupts();
  bool locked = log_begin();
  struct format_output output = {0};
  format_output(&output, format, args);
  log_end(locked);
  cpu_restore_interrupts(flags);
}

void early_console_panic_vprintf(const char *format, va_list args)
{
  if (!early_console_panic_message_begin()) {
    return;
  }
  struct format_output output = {.panic_display = true};
  put_string(&output, "\nCaelum panic: ");
  format_output(&output, format, args);
  put_char(&output, '\n');
  early_console_panic_message_end();
}

int vsprintf(char *buffer, const char *format, va_list args)
{
  struct format_output output = {.buffer = buffer};
  format_output(&output, format, args);
  buffer[output.length] = '\0';
  return output.length > INT_MAX ? -1 : (int)output.length;
}

int sprintf(char *buffer, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  int length = vsprintf(buffer, format, args);
  va_end(args);
  return length;
}
