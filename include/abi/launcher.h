#ifndef ABI_LAUNCHER_H
#define ABI_LAUNCHER_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/startup.h>

#define LAUNCHER_RIGHT_LAUNCH (UINT64_C(1) << 0)
#define LAUNCHER_LAUNCH UINT64_C(1)
#define LAUNCH_CAPTURE_MAX_SIZE STARTUP_MAX_SIZE
/* Host executables are copied before loading; this bounds staging bytes, not
 * the child's mapped memory. RAM and archive images do not need that copy. */
#define LAUNCH_HOST_IMAGE_MAX_SIZE (UINT64_C(16) * 1024 * 1024)

struct launch_grant {
  handle_t source;
  uint64_t rights;
};

struct launch_binding {
  uint64_t name; /* Caller address of a NUL-terminated string. */
  uint64_t grant; /* Index into grants, not a handle. */
};

struct launch_stream {
  uint64_t protocol;
  uint64_t grant; /* Grant-list index; NONE requires zero and installs nothing. */
};

/* Each present stream selects a distinct grant entry, referenced by no resource,
 * root or working-directory binding. That entry installs exactly one handle,
 * adopted by child libc, with only the protocol's READ (stdin) or WRITE rights.
 * Repeated source handles in separate entries deliberately create separate
 * grants, including any explicitly delegated terminal resources. */

/* All addresses are in the caller. Environment uses startup_variable;
 * argv is an array of string addresses, working_directories an array of grant
 * indices. Empty arrays are ignored. working_path is optional (zero = absent).
 * Arrays, strings and alignment together have a 64 KiB capture budget; the
 * resulting child startup region separately obeys STARTUP_MAX_SIZE including
 * page padding. No implicit grants, environment or directory inheritance. */
struct launch_request {
  /* READ file grant; not implicitly passed to the child. Host capture accepts
   * positive short reads, rejects early EOF/observed size changes with IO, and
   * never retries a failed capture. Do not modify the file in place while it
   * loads: even equal before/after sizes cannot establish a host snapshot. */
  handle_t image;
  uint64_t grants, grant_count;
  uint64_t resources, resource_count;
  uint64_t roots, root_count;
  uint64_t working_directories, working_directory_count;
  uint64_t working_path;
  uint64_t environment, environment_count;
  uint64_t argv, argc;
  struct launch_stream streams[STARTUP_STREAM_COUNT];
};

struct launch_message {
  struct message_header header;
  struct launch_request body;
};

/* Reply is one WAIT-authorized process-control handle. Failure returns no
 * handle or runnable child, and preserves source grants. A successful child
 * belongs to the caller's space and runs on the caller's assigned CPU. */
_Static_assert(sizeof(struct launch_grant) == 16, "launch grant layout");
_Static_assert(sizeof(struct launch_binding) == 16, "launch binding layout");
_Static_assert(sizeof(struct launch_stream) == 16, "launch stream layout");
_Static_assert(sizeof(struct launch_request) == 160, "launch request layout");
_Static_assert(sizeof(struct launch_message) == 176, "launch message layout");

#endif
