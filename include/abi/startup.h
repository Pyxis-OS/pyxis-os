#ifndef ABI_STARTUP_H
#define ABI_STARTUP_H

#include <abi/handle.h>
#include <stddef.h>

#define STARTUP_VERSION 1

/* x86_64 entry receives this user pointer in RDI. The record is read-only and
 * remains valid until process exit. Check version and size before reading
 * resource roles; an absent resource has HANDLE_INVALID. Fields are little
 * endian, with natural 8-byte alignment and no pointers into kernel memory. */
struct startup_info {
  uint32_t version;
  uint32_t size;
  handle_t output;
  handle_t content;
  handle_t endpoint;
};

_Static_assert(sizeof(struct startup_info) == 32, "startup record size");
_Static_assert(_Alignof(struct startup_info) == 8, "startup record alignment");
_Static_assert(offsetof(struct startup_info, version) == 0, "startup version offset");
_Static_assert(offsetof(struct startup_info, size) == 4, "startup size offset");
_Static_assert(offsetof(struct startup_info, output) == 8, "startup output offset");
_Static_assert(offsetof(struct startup_info, content) == 16, "startup content offset");
_Static_assert(offsetof(struct startup_info, endpoint) == 24, "startup endpoint offset");

#endif
