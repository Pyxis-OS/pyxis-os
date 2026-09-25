#ifndef CAELUM_LWIP_STRING_H
#define CAELUM_LWIP_STRING_H

/* Private include path for upstream sources, not a kernel libc interface. */
#include <kernel/memory.h>
#include <kernel/string.h>

static inline int strncmp(const char *left, const char *right, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    unsigned char a = left[i], b = right[i];
    if (a != b || !a) {
      return (int)a - (int)b;
    }
  }
  return 0;
}

#endif
