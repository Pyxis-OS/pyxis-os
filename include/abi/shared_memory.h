#ifndef ABI_SHARED_MEMORY_H
#define ABI_SHARED_MEMORY_H

#include <abi/message.h>
#include <abi/handle.h>

/* Native region authority. READ permits an R/NX view; READ|WRITE permits RW/NX.
 * A read-only view is not an immutable snapshot while another writer exists.
 * There is no execute, resize, device import or implicit notification right. */
#define SHARED_MEMORY_RIGHT_READ (UINT64_C(1) << 0)
#define SHARED_MEMORY_RIGHT_WRITE (UINT64_C(1) << 1)
#define SHARED_MEMORY_RIGHTS (SHARED_MEMORY_RIGHT_READ | SHARED_MEMORY_RIGHT_WRITE)

#define SHARED_MEMORY_MAP UINT64_C(1)

struct shared_memory_created {
  handle_t handle;
  uint64_t size;
};

/* Full-region view only. RIGHTS must be READ or READ|WRITE, within the held
 * grant; RESERVED must be zero. Returns memory_reservation (memory.h).
 * Neither handle close nor creator exit alone removes a view. Release its
 * caller-owned identity through MEMORY RELEASE_RESERVATION with MANAGE. */
struct shared_memory_map {
  uint64_t rights;
  uint64_t reserved;
};

struct shared_memory_message {
  struct message_header header;
  struct shared_memory_map body;
};

_Static_assert(sizeof(struct shared_memory_created) == 16, "shared creation reply");
_Static_assert(sizeof(struct shared_memory_map) == 16, "shared mapping payload");
_Static_assert(sizeof(struct shared_memory_message) == 32, "shared mapping message");

#endif
