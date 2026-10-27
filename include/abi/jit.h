#ifndef ABI_JIT_H
#define ABI_JIT_H

#include <abi/message.h>
#include <stddef.h>

#define JIT_RIGHT_MANAGE (UINT64_C(1) << 0)
#define JIT_PAGE_SIZE UINT64_C(4096)
#define JIT_ALLOCATE UINT64_C(1)
#define JIT_PROTECT UINT64_C(2)
#define JIT_RELEASE UINT64_C(3)

enum jit_state {
  JIT_WRITABLE = 1, /* Read/write, non-executable. */
  JIT_READ_ONLY = 2, /* Read-only, non-executable. */
  JIT_EXECUTABLE = 3, /* Read/execute, never writable. */
};

struct jit_region {
  uint64_t identity;
  uint64_t address;
  uint64_t size;
};

union jit_payload {
  struct {
    uint64_t size;
    uint64_t hint; /* Advisory only, never fixed placement; may be ignored. */
    uint64_t reserved[2];
  } allocate;
  struct {
    uint64_t identity;
    uint64_t offset;
    uint64_t size;
    uint64_t state;
  } protect;
  struct {
    uint64_t identity;
    uint64_t reserved[3];
  } release;
};

/* Send a complete zero-initialized message; reserved words must be zero.
 * Authority applies only to the calling process, including copied grants.
 * ALLOCATE rounds a nonzero size to pages and returns jit_region in RW/NX.
 * PROTECT requires a nonzero page-aligned contained interval and a named state;
 * ordinary MEMORY, image, stack/TLS and foreign/stale identities refuse.
 * RELEASE consumes the whole identity. Neither handle close nor transfer frees
 * process-owned backing. Quota exhaustion is LIMIT; backing/metadata/identity
 * exhaustion is NO_MEMORY. Pre-effect failure leaves existing mappings intact.
 * Callers own code lifetime and must keep returning code/stack mapped; there
 * are no code-entry leases or concurrent live-patching guarantees. */
struct jit_message {
  struct message_header header;
  union jit_payload body;
};

_Static_assert(sizeof(struct jit_region) == 24, "JIT region layout");
_Static_assert(sizeof(union jit_payload) == 32, "JIT payload layout");
_Static_assert(offsetof(struct jit_message, body) == 16, "JIT payload offset");
_Static_assert(sizeof(struct jit_message) == 48, "JIT message layout");

#endif
