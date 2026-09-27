#ifndef KERNEL_USER_STARTUP_H
#define KERNEL_USER_STARTUP_H

#include <kernel/mm/types.h>
#include <abi/startup.h>

struct process;

struct process_binding {
  const char *name;
  handle_t handle;
};

struct process_variable {
  const char *name;
  const char *value;
};

struct process_startup {
  struct startup_stream streams[STARTUP_STREAM_COUNT];
  handle_t namespace;
  const struct process_binding *resources;
  size_t resource_count;
  const struct process_binding *roots;
  size_t root_count;
  const handle_t *working_directories;
  size_t working_directory_count;
  const char *working_path;
  const struct process_variable *environment;
  size_t environment_count;
  size_t argc;
  const char *const *argv;
};

/* BSP, IF=0, exclusively owned inactive/unsubmitted process. Input is borrowed
 * kernel memory, stable throughout this call; it is copied, never retained.
 * Resource names are nonempty and unique; handles must already be installed in
 * this process. Environment names are nonempty, unique and contain no '='.
 * Roots use their own unique names (no ':' or '/') and reference directories.
 * An optional namespace handle grants LOOKUP and cannot share a stream handle;
 * its existing names cannot conflict with root names.
 * Working-directory handles also reference installed directories; the launcher
 * chooses their order and navigation boundary. No ancestry is inferred or added.
 * A display path requires a nonempty chain and never grants access. Bindings may
 * alias handles except for streams, whose handles are exclusively owned by libc
 * and carry exactly their direction right. Copying metadata acquires no references.
 * The 64 KiB budget includes both mappings' page padding. MM_INVALID covers
 * malformed/oversized data; MM_NO_MEMORY covers allocation exhaustion.
 * Success publishes startup_address once; failure frees partial backing and
 * changes neither startup_address nor capability ownership. */
enum mm_result process_prepare_startup(struct process *process,
                                       const struct process_startup *source);

#endif
