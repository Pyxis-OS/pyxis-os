#ifndef ABI_LAUNCHER_H
#define ABI_LAUNCHER_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/startup.h>

#define LAUNCHER_RIGHT_LAUNCH (UINT64_C(1) << 0)
#define LAUNCHER_RIGHT_CREATE_GROUP (UINT64_C(1) << 1)
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

/* CREATE_GROUP sends only a message_header and requires CREATE_GROUP on an
 * unbound launcher. The ungrouped caller remains outside the new group. Both
 * handles are returned atomically: CONTROL|WAIT supervision and a LAUNCH-only
 * launcher bound to the group, caller's space and assigned CPU. Copies retain
 * that binding. A member can receive only launchers bound to its own group;
 * membership is permanent and inherited by every child. */
struct execution_group_create_reply {
  handle_t supervision;
  handle_t launcher;
};

struct launch_grant {
  handle_t source;
  uint64_t rights;
  uint64_t transport;
};

struct launch_binding {
  uint64_t name; /* Caller address of a NUL-terminated string. */
  uint64_t grant; /* Index into grants, not a handle. */
};

struct launch_stream {
  uint64_t protocol; /* Same protocol tags as startup_stream. */
  uint64_t grant; /* Grant-list index; NONE requires zero and installs nothing. */
};

/* Each present stream selects a distinct grant entry, referenced by no resource,
 * root or working-directory binding. That entry installs exactly one handle,
 * adopted by child libc, with only the protocol's READ (stdin) or WRITE rights.
 * Repeated source handles in separate entries deliberately create separate
 * grants, including any explicitly delegated terminal resources.
 *
 * All addresses are in the caller. Environment uses startup_variable;
 * argv is an array of string addresses, working_directories an array of grant
 * indices. Empty arrays are ignored. working_path is optional (zero = absent).
 * Arrays, strings and alignment together have a 64 KiB capture budget; the
 * resulting child startup region separately obeys STARTUP_MAX_SIZE including
 * page padding. No implicit grants, environment or directory inheritance. */
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
  uint64_t resources, resource_count;
  uint64_t roots, root_count;
  uint64_t working_directories, working_directory_count;
  uint64_t working_path;
  uint64_t environment, environment_count;
  uint64_t argv, argc;
  struct launch_stream streams[STARTUP_STREAM_COUNT];
  uint64_t namespace_grant; /* Zero = absent; otherwise grant-list index + 1. */
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
_Static_assert(sizeof(struct launch_binding) == 16, "launch binding layout");
_Static_assert(sizeof(struct launch_stream) == 16, "launch stream layout");
_Static_assert(sizeof(struct launch_request) == 176, "launch request layout");
_Static_assert(sizeof(struct launch_message) == 192, "launch message layout");
_Static_assert(sizeof(struct launch_batch_request) == 16, "launch batch request layout");
_Static_assert(sizeof(struct launch_batch_message) == 32, "launch batch message layout");
_Static_assert(sizeof(struct launch_batch_reply) == 72, "launch batch reply layout");

#endif
