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
  OBJECT_TCP_LISTENER = 30,
  OBJECT_TERMINAL_SERVICE = 31,
  OBJECT_TERMINAL_INPUT = 32,
  OBJECT_TERMINAL_OUTPUT = 33,
  OBJECT_TERMINAL_ATTACHMENT = 34,
  OBJECT_EXECUTION_GROUP = 35,
  OBJECT_TERMINAL_EVENTS = 36,
  OBJECT_SYSTEM_INFO = 37,
  OBJECT_DISKS = 38,
  OBJECT_DISK = 39,
  OBJECT_POINTER = 40,
  OBJECT_SPACE_FACTORY = 41,
  OBJECT_POWER = 42,
  OBJECT_LOG = 43,
};

struct execution_group;

/* Embed in a resource whose lifetime is shared by kernel owners and handles.
 * The reference count and retirement link coordinate lifetime across CPUs. Payload access has
 * its own synchronization rules; reference ownership alone does not lock it. */
struct kernel_object {
  enum object_type type; /* Immutable after initialization. */
  atomic_size_t references;
  struct kernel_object *retired_next;
  struct execution_group *cleanup_group; /* Retirement's pending storage token. */
  void (*destroy)(struct kernel_object *);
};

/* Initialize before publication with one owned reference. The callback runs
 * only on the BSP with IF=0, outside the retirement lock. It releases the
 * enclosing allocation and owned resources, or transfers final cleanup to a
 * worker. It must not sleep or borrow a process/capability entry that may
 * already have been destroyed. Embedded endpoint receipts instead use a NULL
 * callback: their final release ends delivery ownership synchronously. */
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
 * release on every CPU; the last release queues destruction without allocating,
 * except receipts, which release logical ownership without freeing storage. */
bool object_retain(struct kernel_object *object);
void object_release(struct kernel_object *object);

/* IF=0. Borrowed cleanup context follows the current task across worker sleeps;
 * scheduler cleanup has a separate context. Enter/leave must be paired. Last
 * object release inherits this context or the executing user's group. */
struct execution_group *object_cleanup_enter(struct execution_group *group);
void object_cleanup_leave(struct execution_group *previous);
/* IF=0. Acquire pending storage for the active context or executing user's
 * group before lending an internal reference or transferring destruction to a
 * worker. NULL means ungrouped cleanup. The worker enters this context around
 * release/destruction and ends the token after returning that ownership. */
struct execution_group *object_cleanup_defer(void);

/* Capability entries and in-flight capability transfers own authority as well
 * as storage. Observation and operation references use retain/release above.
 * IF=0; release may synchronously close a logical endpoint and wake waiters. */
bool object_grant_retain(struct kernel_object *object, uint64_t rights);
void object_grant_release(struct kernel_object *object, uint64_t rights);

/* Scheduler helpers, IF=0. Reaping is BSP-only; callbacks run outside the lock.
 * Pending work must also bring a busy BSP user task back to its scheduler. */
bool object_reap_pending(void);
void object_reap(void);

#endif
