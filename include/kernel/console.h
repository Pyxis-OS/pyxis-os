#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <kernel/object.h>
#include <abi/syscall.h>

struct tty;

struct console_object {
  struct kernel_object object;
  struct tty *tty;
};

/* BSP, IF=0. Returns one owned reference, or NULL on allocation failure.
 * The space owns the TTY and must keep it alive through all console references.
 * Destroying the wrapper never frees the TTY or framebuffer. */
struct console_object *console_create(struct tty *tty);

/* IF=0, trusted kernel buffer. Uses the existing output lock. False means no
 * bytes were written (TTY unavailable or panic output has disabled drawing). */
bool console_write(struct console_object *console, const char *bytes, size_t size);

/* Current process, IF=0. The caller holds a live reference and supplies the
 * rights from its capability entry. User buffers are checked before output. */
struct syscall_result console_call(struct console_object *console, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
