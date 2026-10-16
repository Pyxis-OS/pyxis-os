#ifndef ABI_LAUNCHER_H
#define ABI_LAUNCHER_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/startup.h>
#include <abi/environment.h>

#define LAUNCHER_RIGHT_LAUNCH (UINT64_C(1) << 0)
#define LAUNCHER_LAUNCH UINT64_C(1)
#define LAUNCHER_LAUNCH_BATCH UINT64_C(2)
#define LAUNCHER_CREATE_GROUP UINT64_C(3)
#define LAUNCH_BATCH_MAX 8
#define LAUNCH_NO_STAGE UINT64_MAX
#define LAUNCH_CAPTURE_MAX_SIZE STARTUP_MAX_SIZE
#define LAUNCH_INITIAL_STACK_MIN_SIZE (UINT64_C(1) * 1024 * 1024)
#define LAUNCH_INITIAL_STACK_MAX_SIZE (UINT64_C(8) * 1024 * 1024)
/* Per selected executable for HOST, native filesystem and RAM copies, not an
 * aggregate concurrent budget or mapped-span limit. Backing is released after
 * loading or failure. Boot archive bytes need no copy. */
#define LAUNCH_CAPTURED_IMAGE_MAX_SIZE (UINT64_C(128) * 1024 * 1024)

/* CREATE_GROUP requires LAUNCH. An unbound launcher creates a root in the
 * ungrouped caller's space; a bound launcher creates a subgroup of its binding.
 * A member may use only own-group/descendant launchers, within its fixed space.
 * The creator stays outside the new group. Both owned handles return atomically:
 * CONTROL|WAIT supervision and a LAUNCH-only group-bound launcher. Copies retain
 * the binding; CPU placement remains a property of the space/scheduler. */
struct execution_group_create_reply {
  handle_t supervision;
  handle_t launcher;
};

struct launch_grant {
  handle_t source;
  uint64_t rights;
  uint64_t transport;
};

/* All entries share one exact-name space. CAP value is an index into grants,
 * with length zero; TEXT value is a caller string address and length excludes
 * its NUL. Flags currently select EXPORTED only. Explicit private entries are
 * permitted when their grants/text are staged by the caller; ordinary snapshot
 * selection copies exported entries only. No implicit ambient inheritance.
 * CAP masks remain exactly the selected grant's masks. No protected names. */
struct launch_environment_entry {
  uint64_t name;
  uint64_t kind;
  uint64_t flags;
  uint64_t value;
  uint64_t length;
};

/* Every address is in the caller. Arrays, strings and alignment share a 64 KiB
 * capture budget, and child serialization separately obeys STARTUP_MAX_SIZE
 * including page padding and selected environment names/text. Cwd uses grant
 * indices, boundary first; working_path is only an optional description.
 * Standard streams are ordinary stdin/stdout/stderr entries. Child redirection
 * replaces these entries in staging, never in the parent's live environment. */
struct launch_request {
  /* READ file grant; not implicitly passed to the child. Host capture accepts
   * positive short reads, rejects early EOF/observed size changes with IO, and
   * never retries a failed capture. Do not modify the file in place while it
   * loads: even equal before/after sizes cannot establish a host snapshot.
   * Native filesystem capture holds a serialized file operation through the
   * copy, requires READ-plus-metadata, and publishes no bytes on a core/backing
   * error. Retaining a file or directory handle does not freeze later lookups
   * or writable aliases; published bundle revisions must remain unchanged. */
  handle_t image;
  uint64_t grants, grant_count;
  uint64_t entries, entry_count;
  uint64_t working_directories, working_directory_count;
  uint64_t working_path;
  uint64_t argv, argc;
  /* Zero selects 1 MiB. Explicit sizes must be page-aligned, 1..8 MiB inclusive;
   * invalid requests fail with BAD_REQUEST before image capture. Eager backing
   * ends at the fixed high stack top, with one unmapped guard page directly
   * below the backing. */
  uint64_t initial_stack_bytes;
};

struct launch_message {
  struct message_header header;
  struct launch_request body;
};

/* requests is a caller address of count launch_request records. All children
 * are prepared before any becomes runnable. On failure no child is runnable,
 * source handles survive, and children[] is entirely zero. File writes and
 * other external effects are not rolled back. A valid batch call
 * returns the fixed reply even on an operation error; failed_index names the
 * zero-based failing stage, or LAUNCH_NO_STAGE for a whole-batch error. */
struct launch_batch_request {
  uint64_t requests;
  uint64_t count;
};

struct launch_batch_message {
  struct message_header header;
  struct launch_batch_request body;
};

struct launch_batch_reply {
  uint64_t failed_index;
  handle_t children[LAUNCH_BATCH_MAX];
};

/* Reply is one WAIT-authorized process-control handle. Failure returns no
 * handle or runnable child, and preserves source grants. A successful child
 * belongs to the caller's space and runs on the caller's assigned CPU. */
_Static_assert(sizeof(struct launch_grant) == 24, "launch grant layout");
_Static_assert(sizeof(struct execution_group_create_reply) == 16, "group creation reply layout");
_Static_assert(sizeof(struct launch_environment_entry) == 40, "launch environment entry layout");
_Static_assert(sizeof(struct launch_request) == 88, "launch request layout");
_Static_assert(sizeof(struct launch_message) == 104, "launch message layout");
_Static_assert(sizeof(struct launch_batch_request) == 16, "launch batch request layout");
_Static_assert(sizeof(struct launch_batch_message) == 32, "launch batch message layout");
_Static_assert(sizeof(struct launch_batch_reply) == 72, "launch batch reply layout");

#endif
