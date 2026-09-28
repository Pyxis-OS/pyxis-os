#ifndef KERNEL_OBJECT_H
#define KERNEL_OBJECT_H

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

enum object_type {
  OBJECT_CONSOLE = 1,
  OBJECT_FILE = 2,
  OBJECT_ENDPOINT = 3,
  OBJECT_DIRECTORY = 4,
  OBJECT_MEMORY = 5,
  OBJECT_PROCESS_CONTROL = 6,
  OBJECT_LAUNCHER = 7,
  OBJECT_DISPLAY = 8,
  OBJECT_CLOCK = 9,
  OBJECT_KEYBOARD = 10,
  OBJECT_MOUNT = 11,
  OBJECT_ECHO = 12,
  OBJECT_NET_CONFIG = 13,
  OBJECT_UDP_SERVICE = 14,
  OBJECT_UDP = 15,
  OBJECT_RANDOM = 16,
  OBJECT_TCP_SERVICE = 17,
  OBJECT_TCP = 18,
  OBJECT_SPACE = 19,
  OBJECT_PROFILE = 20,
  OBJECT_PIPE_SERVICE = 21,
  OBJECT_PIPE = 22,
  OBJECT_ENDPOINT_SERVICE = 23,
  OBJECT_ENDPOINT_RECEIVER = 24,
  OBJECT_ENDPOINT_RECEIPT = 25,
  OBJECT_ENDPOINT_EXPORT = 26,
  OBJECT_NAMESPACE_SERVICE = 27,
  OBJECT_NAMESPACE = 28,
};

/* Embed in a resource whose lifetime is shared by kernel owners and handles.
 * The reference count and retirement link coordinate lifetime across CPUs. Payload access has
 * its own synchronization rules; reference ownership alone does not lock it. */
struct kernel_object {
  enum object_type type; /* Immutable after initialization. */
  atomic_size_t references;
  struct kernel_object *retired_next;
  void (*destroy)(struct kernel_object *);
};

/* Initialize before publication with one owned reference. The callback runs
 * only on the BSP with IF=0, outside the retirement lock. It releases the
 * enclosing allocation and owned resources, or transfers final cleanup to a
 * worker. It must not sleep or borrow a process/capability entry that may
 * already have been destroyed. */
void object_init(struct kernel_object *object, enum object_type type,
                 void (*destroy)(struct kernel_object *));

/* Resource rights have meaning within the object's protocol; transport
 * authority controls endpoint delivery. Providers interpret resource rights.
 * Unknown types, bits and grants above an export's ceiling are invalid. */
bool object_authority_valid(const struct kernel_object *object, uint64_t rights,
                            uint64_t transport);

/* Immutable public interface; caller retains the object. No liveness promise. */
uint64_t object_protocol(const struct kernel_object *object);

/* Standard streams accept native objects or exported FILE with CALL transport. */
bool object_stream_valid(const struct kernel_object *object, uint64_t protocol,
                         uint64_t transport);

/* Caller owns a live reference throughout retain. False means count overflow;
 * no reference is acquired. Release consumes one owned reference. IF=0 for
 * release on every CPU; the last release queues destruction without allocating. */
bool object_retain(struct kernel_object *object);
void object_release(struct kernel_object *object);

/* Scheduler helpers, IF=0. Reaping is BSP-only; callbacks run outside the lock.
 * Pending work must also bring a busy BSP user task back to its scheduler. */
bool object_reap_pending(void);
void object_reap(void);

#endif
