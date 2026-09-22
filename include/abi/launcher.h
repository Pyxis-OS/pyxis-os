#ifndef ABI_LAUNCHER_H
#define ABI_LAUNCHER_H

#include <abi/handle.h>
#include <abi/message.h>
#include <abi/startup.h>

#define LAUNCHER_RIGHT_LAUNCH (UINT64_C(1) << 0)
#define LAUNCHER_LAUNCH UINT64_C(1)
#define LAUNCH_CAPTURE_MAX_SIZE STARTUP_MAX_SIZE

struct launch_grant {
  handle_t source;
  uint64_t rights;
};

struct launch_binding {
  uint64_t name; /* Caller address of a NUL-terminated string. */
  uint64_t grant; /* Index into grants, not a handle. */
};

/* All addresses are in the caller. Environment uses startup_variable;
 * argv is an array of string addresses, working_directories an array of grant
 * indices. Empty arrays are ignored. working_path is optional (zero = absent).
 * Arrays, strings and alignment together have a 64 KiB capture budget; the
 * resulting child startup region separately obeys STARTUP_MAX_SIZE including
 * page padding. No implicit grants, environment or directory inheritance. */
struct launch_request {
  handle_t image; /* READ file grant; not implicitly passed to the child. */
  uint64_t grants, grant_count;
  uint64_t resources, resource_count;
  uint64_t roots, root_count;
  uint64_t working_directories, working_directory_count;
  uint64_t working_path;
  uint64_t environment, environment_count;
  uint64_t argv, argc;
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
_Static_assert(sizeof(struct launch_request) == 112, "launch request layout");
_Static_assert(sizeof(struct launch_message) == 128, "launch message layout");

#endif
