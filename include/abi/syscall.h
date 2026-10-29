#ifndef ABI_SYSCALL_H
#define ABI_SYSCALL_H

#include <stdint.h>

#define SYSCALL_LOG_PUTCHAR UINT64_C(1)
#define SYSCALL_CALL UINT64_C(2)
#define SYSCALL_CLOSE UINT64_C(3)
#define SYSCALL_COPY UINT64_C(4)
/* 5 was HANDLE_INFO without a reply capacity. It stays unassigned so a binary
 * built for that call gets the unknown-call status instead of a larger reply. */
#define SYSCALL_WAIT_MANY UINT64_C(6)
#define SYSCALL_FORWARDER_CREATE UINT64_C(7)
#define SYSCALL_DESCRIBE UINT64_C(8)
#define SYSCALL_HANDLE_INFO UINT64_C(9)
#define SYSCALL_READINESS_SET_CREATE UINT64_C(10)
#define SYSCALL_PROCESS_INTERRUPT_CREATE UINT64_C(11)
/* (reply address, capacity): the caller's own space, abi/process_view.h. */
#define SYSCALL_SPACE_PROCESSES UINT64_C(12)
#define SYSCALL_EXIT UINT64_C(-1)
#define SYSCALL_START_FAILED UINT64_C(-2)

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
  /* A pipe read reached a value segment; PIPE_RECEIVE moves it out. */
  CALL_PIPE_VALUE = 32,
  /* The existing unknown-call wire result, not a protocol operation failure. */
  CALL_UNKNOWN_SYSTEM_CALL = -1,
  CALL_STATUS_COUNT = 33, /* Validation bound, not a result. */
};

/* START_FAILED takes a packed process startup reason in RDI (process.h),
 * completes the calling process through normal retirement and never returns.
 * Invalid reasons return BAD_REQUEST without stopping the process.
 * EXIT takes a signed 32-bit program status and never returns.
 *
 * CALL/CLOSE/COPY/HANDLE_INFO/WAIT_MANY/FORWARDER_CREATE and local object creation
 * return status in RAX and reply bytes in
 * RDX (zero on failure, always zero for CLOSE). WAIT_MANY is defined in wait.h. COPY takes source, resource rights, transport rights,
 * flags and an output handle address in RDI/RSI/RDX/R10/R8; success writes
 * one handle and returns its size. HANDLE_INFO takes a handle, handle_info
 * output address and that buffer's capacity in RDI/RSI/RDX; a capacity below
 * sizeof(struct handle_info) fails BUFFER_TOO_SMALL before any write. Success
 * writes authority and interface. It exposes no object identity and requires no
 * right beyond holding the handle.
 * FORWARDER_CREATE takes the fixed forwarder_create request address, the
 * forwarder_reply address and that buffer's capacity in RDI/RSI/RDX; it
 * publishes both holder/controller handles or neither (forwarder.h).
 * DESCRIBE takes a handle, describe_reply address, that buffer's capacity and
 * flags in RDI/RSI/RDX/R10; the provider-free query and optional owned snapshot
 * are defined in describe.h. For HANDLE_INFO, FORWARDER_CREATE and DESCRIBE, a
 * capacity below the minimum reply size fails BUFFER_TOO_SMALL before any
 * other check or write.
 * PROCESS_INTERRUPT_CREATE takes reply address/capacity in RDI/RSI; its
 * owner-local receiver and fixed minimum reply are defined in process.h.
 * The C x86_64 ABI returns this two-word structure in those same registers.
 * LAUNCHER_LAUNCH_BATCH is a narrow exception: after reply-buffer validation it
 * returns its fixed reply on operation failure too, with status still in RAX.
 * ENVIRONMENT LOOKUP/ENUMERATE return their fixed metadata on BUFFER_TOO_SMALL
 * without publishing text bytes or a handle.
 * ENDPOINT_CALL similarly returns delivery metadata after output validation,
 * including on transport failure; application results are separate. */
struct syscall_result {
  uint64_t status;
  uint64_t reply_size;
};

_Static_assert(sizeof(struct syscall_result) == 16, "syscall result layout");

#endif
