#ifndef CAELUM_LWIP_STDLIB_H
#define CAELUM_LWIP_STDLIB_H

/* netif_find's numeric interface suffix is the only consumer. Keep this local
 * to upstream code; no host library or new kernel-wide conversion API. */
static inline int atoi(const char *text)
{
  while (*text == ' ' || (*text >= '\t' && *text <= '\r')) {
    ++text;
  }
  int sign = 1;
  if (*text == '-' || *text == '+') {
    sign = *text++ == '-' ? -1 : 1;
  }
  unsigned value = 0;
  unsigned limit = (unsigned)__INT_MAX__ + (sign < 0);
  while (*text >= '0' && *text <= '9') {
    unsigned digit = (unsigned)(*text++ - '0');
    if (value > (limit - digit) / 10) {
      return 0;
    }
    value = value * 10 + digit;
  }
  if (sign < 0) {
    return value ? -(int)(value - 1) - 1 : 0;
  }
  return (int)value;
}

#endif
