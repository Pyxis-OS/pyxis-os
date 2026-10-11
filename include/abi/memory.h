#ifndef ABI_MEMORY_H
#define ABI_MEMORY_H

#include <abi/message.h>
#include <stddef.h>

/* Authority applies to the calling process, including after a handle transfer.
 * Regions belong to the process, not the handle or service object's lifetime. */
#define MEMORY_RIGHT_MANAGE (UINT64_C(1) << 0)
#define MEMORY_PAGE_SIZE UINT64_C(4096)

#define MEMORY_ALLOCATE UINT64_C(1)
#define MEMORY_RELEASE UINT64_C(2)
#define MEMORY_ALLOCATE_GUARDED UINT64_C(3)
#define MEMORY_RELEASE_RESERVATION UINT64_C(4)

struct memory_region {
  uint64_t address;
  uint64_t size;
};

/* Opaque identity is resolved only in the calling process. Address and size
 * describe the full reservation, including its first unmapped guard page. */
struct memory_reservation {
  uint64_t identity;
  uint64_t address;
  uint64_t size;
};

union memory_payload {
  struct {
    uint64_t size;
  } allocate;
  struct memory_region release;
  struct {
    uint64_t size;
    uint64_t reserved;
  } allocate_guarded;
  struct {
    uint64_t identity;
    uint64_t reserved;
  } release_reservation;
};

/* Send the complete zero-initialized message for each operation. ALLOCATE
 * rejects zero/overflow, rounds up to pages, and returns one memory_region.
 * Backing is private, eager, zeroed, readable/writable and non-executable.
 * RELEASE requires the exact returned address/size and returns no reply bytes.
 * Failed operations preserve existing allocations. No partial release.
 * ALLOCATE_GUARDED takes usable backing bytes, rounded up to pages, and returns
 * a memory_reservation. Its full range adds one unmapped lower guard page;
 * the remaining pages have eager zeroed RW/NX backing. RELEASE_RESERVATION
 * accepts only the identity and returns no bytes. Released/stale identities
 * reject even after address reuse; live/retiring context pins return BUSY.
 * Ordinary RELEASE cannot release a guarded reservation. Closing the service
 * releases neither kind. Reserved request words must be zero. */
struct memory_message {
  struct message_header header;
  union memory_payload body;
};

_Static_assert(sizeof(struct memory_region) == 16, "memory region layout");
_Static_assert(sizeof(struct memory_reservation) == 24, "memory reservation layout");
_Static_assert(sizeof(union memory_payload) == 16, "memory payload layout");
_Static_assert(offsetof(struct memory_message, body) == 16, "memory payload offset");
_Static_assert(sizeof(struct memory_message) == 32, "memory message layout");

#endif
