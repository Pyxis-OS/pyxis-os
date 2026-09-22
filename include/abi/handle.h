#ifndef ABI_HANDLE_H
#define ABI_HANDLE_H

#include <stdint.h>

/* Opaque process-local capability. Never decode or use as an object identity. */
typedef uint64_t handle_t;
#define HANDLE_INVALID UINT64_C(0)

/* COPY with this flag preserves the source grant and requires rights == 0.
 * Without it, rights requests an exact subset of the source grant. */
#define HANDLE_COPY_SAME_RIGHTS UINT64_C(1)

#endif
