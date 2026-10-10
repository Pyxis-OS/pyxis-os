#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

#include <stdint.h>

#define SYSCALL_LOG_PUTCHAR UINT64_C(1)
#define SYSCALL_CALL UINT64_C(2)
#define SYSCALL_CLOSE UINT64_C(3)
#define SYSCALL_COPY UINT64_C(4)
#define SYSCALL_HANDLE_INFO UINT64_C(5)
#define SYSCALL_WAIT_MANY UINT64_C(6)
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
  CALL_ALREADY_EXISTS = 14,
  CALL_READ_ONLY = 15,
  CALL_INPUT_LOST = 16,
  CALL_TIMED_OUT = 17,
  CALL_NOT_EMPTY = 18,
  CALL_IO = 19,
  CALL_NO_ROUTE = 20,
  CALL_BUFFER_TOO_SMALL = 21,
  CALL_CONNECTION_REFUSED = 22,
  CALL_CONNECTION_RESET = 23,
  CALL_NO_SPACE = 24,
  CALL_QUOTA = 25,
  CALL_FILE_TOO_LARGE = 26,
  /* A submitted mutation has no trustworthy completion. It may have taken
   * effect: no known byte count, rollback guarantee or safe automatic retry. */
  CALL_OUTCOME_UNKNOWN = 27,
  CALL_ABANDONED = 28,
  CALL_WOULD_BLOCK = 29,
  /* Lookup met a symbolic link; Pyxis follows none. */
  CALL_LINK_NOT_FOLLOWED = 30,
  /* One path component exceeds its directory backing's name limit. */
  CALL_NAME_TOO_LONG = 31,
  CALL_STATUS_COUNT, /* Validation bound, not a result. */
};

/* CALL/CLOSE/COPY/HANDLE_INFO/WAIT_MANY return status in RAX and reply bytes in
 * RDX (zero on failure, always zero for CLOSE). WAIT_MANY is defined in wait.h. COPY takes source, resource rights, transport rights,
 * flags and an output handle address in RDI/RSI/RDX/R10/R8; success writes
 * one handle and returns its size. HANDLE_INFO takes a handle and
 * handle_info output address in RDI/RSI; success writes authority and interface.
 * It exposes no object identity and requires no right beyond holding the handle.
 * The C x86_64 ABI returns this two-word structure in those same registers.
 * LAUNCHER_LAUNCH_BATCH is a narrow exception: after reply-buffer validation it
 * returns its fixed reply on operation failure too, with status still in RAX.
 * ENDPOINT_CALL similarly returns delivery metadata after output validation,
 * including on transport failure; application results are separate. */
struct syscall_result {
  uint64_t status;
  uint64_t reply_size;
};

_Static_assert(sizeof(struct syscall_result) == 16, "syscall result layout");

#endif
