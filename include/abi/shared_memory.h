#ifndef ABI_SHARED_MEMORY_H
#define ABI_SHARED_MEMORY_H

#include <abi/message.h>

/* Native region authority. READ permits an R/NX view; READ|WRITE permits RW/NX.
 * A read-only view is not an immutable snapshot while another writer exists.
 * There is no execute, resize, device import or implicit notification right. */
#define SHARED_MEMORY_RIGHT_READ (UINT64_C(1) << 0)
#define SHARED_MEMORY_RIGHT_WRITE (UINT64_C(1) << 1)
#define SHARED_MEMORY_RIGHTS (SHARED_MEMORY_RIGHT_READ | SHARED_MEMORY_RIGHT_WRITE)

#endif
