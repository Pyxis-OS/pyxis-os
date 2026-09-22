#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

#include <stdint.h>

#define SYSCALL_LOG_PUTCHAR UINT64_C(1)
#define SYSCALL_CALL UINT64_C(2)
#define SYSCALL_CLOSE UINT64_C(3)
#define SYSCALL_COPY UINT64_C(4)
#define SYSCALL_EXIT UINT64_C(-1)

enum call_status {
  CALL_OK = 0,
  CALL_BAD_HANDLE = 1,
  CALL_DENIED = 2,
  CALL_BAD_OPERATION = 3,
  CALL_BAD_REQUEST = 4,
  CALL_BAD_BUFFER = 5,
  CALL_UNAVAILABLE = 6,
  CALL_QUEUE_FULL = 7,
  CALL_ENDPOINT_CLOSED = 8,
  CALL_BUSY = 9,
  CALL_NO_MEMORY = 10,
  CALL_LIMIT = 11,
  CALL_NOT_FOUND = 12,
  CALL_WRONG_TYPE = 13,
  CALL_STATUS_COUNT, /* Validation bound, not a result. */
};

/* CALL/CLOSE/COPY return status in RAX and reply bytes in RDX (zero on failure,
 * always zero for CLOSE). COPY takes source, rights, flags and an output handle
 * address in RDI/RSI/RDX/R10; success writes one handle and returns its size.
 * The C x86_64 ABI returns this two-word structure in those same registers. */
struct syscall_result {
  uint64_t status;
  uint64_t reply_size;
};

_Static_assert(sizeof(struct syscall_result) == 16, "syscall result layout");

#endif
