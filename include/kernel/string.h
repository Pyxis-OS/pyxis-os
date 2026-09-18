#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <stddef.h>

/* Copy up to max_length bytes, stopping at the first NUL, and append a NUL.
 * Returns a heap-owned string (release with kfree), or NULL on allocation or
 * size overflow failure. A zero bound produces an allocated empty string.
 * Requires initialized heap, BSP execution and IF=0, outside interrupt/fault entry. */
char *strndup(const char *text, size_t max_length);

#endif
