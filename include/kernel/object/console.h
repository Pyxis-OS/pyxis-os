#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <kernel/object/object.h>
#include <kernel/wait.h>
#include <abi/syscall.h>

struct tty;

#define CONSOLE_INPUT_CAPACITY 4096

/* Ctrl+C interruption state of one input object, shared by framebuffer
 * consoles and terminal input. ARMED and PASSTHROUGH grants are counted here;
 * arming reserves the single armed interval while its handle is installed.
 * Lock order: owner input lock -> interrupt lock -> scheduler queues. */
struct console_interrupt {
  atomic_bool locked;
  size_t armed, passthrough;
  bool arming, latched;
};

struct console_object {
  struct kernel_object object;
  struct tty *tty;
  /* Caelum's log console: writes are also copied to serial, as klog is. */
  bool serial;
  struct console_interrupt interrupt;
  atomic_bool input_locked;
  char input[CONSOLE_INPUT_CAPACITY];
  size_t input_head, input_count;
  /* A reader keeps ownership while sleeping; queued readers cannot overtake. */
  bool input_lost, reader_active;
  struct task_wait_link *first_reader, *last_reader;
  struct task_wait *input_wait;
};

/* BSP input consumer; preserves IF. Whole sequences fit or latch input loss.
 * Loss clears the queue and discards input until a nonempty READ acknowledges it.
 * Neither entry point allocates or touches a reader's private stack/mappings. */
void console_input(struct console_object *console, const char *bytes, size_t size);
void console_input_lost(struct console_object *console);

/* Any CPU, IF=0. Routing handoff discards queued text without input-loss
 * notification; existing readers continue waiting for future text. */
void console_discard_input(struct console_object *console);

/* BSP, IF=0. Returns one owned reference, or NULL on allocation failure.
 * The space owns the TTY and must keep it alive through all console references.
 * Destroying the wrapper never frees the TTY or framebuffer. */
struct console_object *console_create(struct tty *tty);

/* IF=0, trusted kernel buffer. Uses the existing output lock. False means no
 * bytes were written (TTY unavailable or panic output has disabled drawing). */
bool console_write(struct console_object *console, const char *bytes, size_t size);

/* IF=0. Grant accounting for ARMED and PASSTHROUGH; other rights and other
 * console objects are a successful no-op. Release precedes object_release. */
bool console_authority_retain(struct kernel_object *object, uint64_t rights);
void console_authority_release(struct kernel_object *object, uint64_t rights);

/* Any CPU, preserves IF. WAIT_INTERRUPT while the latch is set, otherwise zero. */
uint64_t console_interrupt_ready(struct console_interrupt *interrupt);

/* IF=0, under the owner's input lock. While armed without passthrough, returns
 * the length of the prefix ending at the last byte 3 and sets the latch; the
 * caller discards queued input and that prefix. Otherwise returns zero. */
size_t console_interrupt_scan(struct console_interrupt *interrupt,
    const void *bytes, size_t size);

bool console_interrupt_retain(struct console_interrupt *interrupt, uint64_t rights);
void console_interrupt_release(struct console_interrupt *interrupt, uint64_t rights);

/* Current process, IF=0. ARM_INTERRUPT or PASSTHROUGH after the caller checked
 * rights and the request size. The caller's handle keeps object alive. */
struct syscall_result console_interrupt_call(struct kernel_object *object,
    struct console_interrupt *interrupt, uint64_t operation,
    uintptr_t reply_address, size_t reply_capacity);

/* Current process, IF=0. The caller holds a live reference and supplies the
 * rights from its capability entry and operation from a checked protocol tag.
 * request_address/size describe the payload after that tag. User buffers are
 * checked before consuming input or producing output. READ may sleep. */
struct syscall_result console_call(struct console_object *console, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
