#ifndef ABI_HANDLE_H
#define ABI_HANDLE_H

#include <stdint.h>

/* Opaque process-local capability. Never decode or use as an object identity. */
typedef uint64_t handle_t;
#define HANDLE_INVALID UINT64_C(0)

#endif
