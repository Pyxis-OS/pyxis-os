#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

#include <stdint.h>

#define SYSCALL_PUTCHAR UINT64_C(0)
#define SYSCALL_LOG_PUTCHAR UINT64_C(1)
#define SYSCALL_CALL UINT64_C(2)
#define SYSCALL_CLOSE UINT64_C(3)
#define SYSCALL_EXIT UINT64_C(-1)

enum call_status {
  CALL_OK = 0,
  CALL_BAD_HANDLE = 1,
  CALL_DENIED = 2,
  CALL_BAD_OPERATION = 3,
  CALL_BAD_REQUEST = 4,
  CALL_BAD_BUFFER = 5,
  CALL_UNAVAILABLE = 6,
};

/* CALL/CLOSE return status in RAX and reply bytes in RDX (zero on failure,
 * always zero for CLOSE).
 * The C x86_64 ABI returns this two-word structure in those same registers. */
struct syscall_result {
  uint64_t status;
  uint64_t reply_size;
};

_Static_assert(sizeof(struct syscall_result) == 16, "syscall result layout");

#endif
