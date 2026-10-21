#ifndef KERNEL_FS_PACKAGE_CURRENT_H
#define KERNEL_FS_PACKAGE_CURRENT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* The current schema, return { set = N }, with the literal parser's syntax.
 * No allocation, execution, package manifest parsing or generalized Lua state. */
bool package_current_parse(const char *bytes, size_t size, uint64_t *set);

#endif
