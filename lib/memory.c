#include <kernel/memory.h>
#include <stdint.h>

void *memset(void *dest, int value, size_t count)
{
  unsigned char *d = dest;
  for (size_t i = 0; i < count; ++i) {
    d[i] = (unsigned char)value;
  }
  return dest;
}

void *memcpy(void *restrict dest, const void *restrict src, size_t count)
{
  unsigned char *d = dest;
  const unsigned char *s = src;
  for (size_t i = 0; i < count; ++i) {
    d[i] = s[i];
  }
  return dest;
}

void *memmove(void *dest, const void *src, size_t count)
{
  unsigned char *d = dest;
  const unsigned char *s = src;
  if ((uintptr_t)d < (uintptr_t)s) {
    for (size_t i = 0; i < count; ++i) {
      d[i] = s[i];
    }
  } else {
    while (count) {
      --count;
      d[count] = s[count];
    }
  }
  return dest;
}

int memcmp(const void *a, const void *b, size_t count)
{
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < count; ++i) {
    if (x[i] != y[i]) {
      return (int)x[i] - (int)y[i];
    }
  }
  return 0;
}
