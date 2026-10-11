#ifndef KERNEL_USER_MEMORY_H
#define KERNEL_USER_MEMORY_H

#include <abi/handle.h>
#include <stddef.h>
#include <stdint.h>

struct process;

struct user_buffer_lease {
  struct process *process;
};

/* IF=0, nonblocking and nested. Covers only a bounded check/copy or commit,
 * never allocation, an I/O wait or scheduling. Release before root departure. */
bool user_buffer_lease_acquire(struct user_buffer_lease *lease);
void user_buffer_lease_release(struct user_buffer_lease *lease);

enum user_buffer_access {
  USER_BUFFER_READ,
  USER_BUFFER_WRITE,
};

/* Current user process only, IF=0, outside interrupt/fault entry, after scheduler
 * initialization. No allocation, scratch mappings, or address-space switches.
 * False is a recoverable invalid-buffer result, not a syscall status value.
 * Zero bytes succeeds without inspecting the address, but still requires a
 * current process with its private root active. */
bool user_buffer_check(uintptr_t address, size_t bytes,
                        enum user_buffer_access access);

/* Caller owns this live lease through validation and any bounded copy/commit.
 * Does not acquire another borrower or extend the lease across a wait. */
bool user_buffer_check_leased(const struct user_buffer_lease *lease,
    uintptr_t address, size_t bytes, enum user_buffer_access access);

/* Validate the entire range under a short mapping lease before copying. Failure
 * leaves the destination untouched. Kernel buffers must be valid and not alias
 * user ranges; zero bytes touches neither buffer. Input contents remain untrusted
 * and can change independently of mapping stability. No allocation, scheduling
 * or fault recovery. A precheck is not a lease across a later wait: stage inputs
 * and revalidate final outputs, preserving already committed effects on failure. */
bool copy_from_user(void *destination, uintptr_t source, size_t bytes);
bool copy_to_user(uintptr_t destination, const void *source, size_t bytes);

/* Same copy guarantees, using the caller's live bounded lease. The caller must
 * release it before allocation, scheduling, waiting or authority cleanup. */
bool copy_from_user_leased(const struct user_buffer_lease *lease,
    void *destination, uintptr_t source, size_t bytes);

enum user_string_result {
  USER_STRING_OK,
  USER_STRING_BAD_BUFFER,
  USER_STRING_TOO_LONG,
};

/* One short lease, validating each page only as needed. Copy through the first
 * NUL without reading beyond it; copied includes NUL on success and is zero on
 * failure. Kernel destination may contain a partial string on failure. Capacity
 * is the caller's bounded storage budget; zero capacity touches no user address.
 * Destination and copied are valid kernel storage and do not alias the source. */
enum user_string_result copy_string_from_user(char *destination, uintptr_t source,
    size_t capacity, size_t *copied);

/* Validate both destinations under one lease, then copy data followed by reply.
 * Overlapping user destinations retain that order; kernel sources must not alias
 * either. A validation failure writes neither range. */
bool copy_reply_to_user(uintptr_t reply_address, const void *reply, size_t reply_size,
    uintptr_t data_address, const void *data, size_t data_size);
bool copy_reply_to_user_leased(const struct user_buffer_lease *lease,
    uintptr_t reply_address, const void *reply, size_t reply_size,
    uintptr_t data_address, const void *data, size_t data_size);

/* A failed reply releases only the operation's unreported handle generations.
 * External creation, launch and device effects are not rolled back. */
bool copy_returned_handles_to_user(uintptr_t destination, const void *source,
    size_t bytes, const handle_t *handles, size_t count);

#endif
