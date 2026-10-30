#ifndef ABI_MEMORY_H
#define ABI_MEMORY_H

#include <abi/message.h>
#include <stddef.h>

/* MANAGE applies to the calling process, including after handle transfer.
 * Its private allocations belong to the process, not the service handle.
 * CREATE_SHARED is independent; its region lifetime is in shared_memory.h. */
#define MEMORY_RIGHT_MANAGE (UINT64_C(1) << 0)
#define MEMORY_RIGHT_CREATE_SHARED (UINT64_C(1) << 1)
#define MEMORY_PAGE_SIZE UINT64_C(4096)

#define MEMORY_ALLOCATE UINT64_C(1)
#define MEMORY_RELEASE UINT64_C(2)
#define MEMORY_ALLOCATE_GUARDED UINT64_C(3)
#define MEMORY_RELEASE_RESERVATION UINT64_C(4)
#define MEMORY_RESERVE UINT64_C(5)
#define MEMORY_COMMIT UINT64_C(6)
#define MEMORY_DECOMMIT UINT64_C(7)
#define MEMORY_CREATE_SHARED UINT64_C(8)

struct memory_region {
  uint64_t address;
  uint64_t size;
};

/* Opaque identity is resolved only in the calling process. Address and size
 * describe the full reservation; a guarded allocation includes its guard. */
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
  struct {
    uint64_t size;
    uint64_t alignment;
  } reserve;
  struct {
    uint64_t size;
    uint64_t reserved;
  } create_shared;
};

struct memory_subrange {
  uint64_t identity;
  uint64_t offset;
  uint64_t size;
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
 * Ordinary RELEASE cannot release an identity-owned reservation. Closing the
 * service releases neither kind. Reserved request words must be zero.
 * RESERVE takes a nonzero size, rounded up to pages, and a power-of-two
 * alignment at least one page. It returns memory_reservation without backing,
 * a guard or fixed placement. RELEASE_RESERVATION also releases sparse and
 * shared-view identities; shared pages stay owned by their region object.
 * CREATE_SHARED requires CREATE_SHARED rather than MANAGE. Its size/reserved
 * payload rounds to pages and returns shared_memory_created (shared_memory.h).
 * No partial backing or handle is published on failure. */
struct memory_message {
  struct message_header header;
  union memory_payload body;
};

/* COMMIT/DECOMMIT accept only sparse RESERVE identities, with a nonzero
 * page-aligned contained offset/size. Pinned guarded identities return BUSY;
 * all other non-sparse, stale and foreign identities return BAD_REQUEST.
 * COMMIT adds zeroed private RW/NX backing to holes, preserving backed bytes.
 * DECOMMIT discards backing and withdraws address waits, retaining the address
 * reservation. Recommit supplies zeros and fresh mapping incarnations.
 * Both return status only, capture input before mutation and leave prior
 * mappings/contents/identities unchanged on failure. */
struct memory_subrange_message {
  struct message_header header;
  struct memory_subrange body;
};

_Static_assert(sizeof(struct memory_region) == 16, "memory region layout");
_Static_assert(sizeof(struct memory_reservation) == 24, "memory reservation layout");
_Static_assert(sizeof(union memory_payload) == 16, "memory payload layout");
_Static_assert(offsetof(struct memory_message, body) == 16, "memory payload offset");
_Static_assert(sizeof(struct memory_message) == 32, "memory message layout");
_Static_assert(sizeof(struct memory_subrange) == 24, "memory subrange layout");
_Static_assert(offsetof(struct memory_subrange_message, body) == 16, "memory subrange offset");
_Static_assert(sizeof(struct memory_subrange_message) == 40, "memory subrange message layout");

#endif
