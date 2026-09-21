#ifndef KERNEL_USER_MEMORY_H
#define KERNEL_USER_MEMORY_H

#include <stddef.h>
#include <stdint.h>

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

/* Validate the entire user range before copying; failure leaves the destination
 * untouched. The kernel buffer must be valid for bytes and must not overlap or
 * alias the user range. Zero bytes touches neither buffer. Mappings remain
 * stable because this process has one task, no shared user backing and no
 * concurrent VM mutation. The copy itself never schedules. A blocking syscall
 * must keep the process alive, stage all input first, and resume its original
 * task/root before using checked destinations; keep IF=0 while copying.
 * No fault recovery is attempted for broken kernel mapping/buffer invariants.
 * A syscall must check all request/reply/data buffers before its side effects;
 * capture request metadata with copy_from_user before interpreting it. */
bool copy_from_user(void *destination, uintptr_t source, size_t bytes);
bool copy_to_user(uintptr_t destination, const void *source, size_t bytes);

#endif
