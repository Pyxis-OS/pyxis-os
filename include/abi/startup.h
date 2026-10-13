#ifndef ABI_STARTUP_H
#define ABI_STARTUP_H

#include <abi/handle.h>
#include <abi/message.h>
#include <stddef.h>

#define STARTUP_VERSION 1
#define STARTUP_MAX_SIZE UINT64_C(65536)

enum startup_stream_index {
  STARTUP_STDIN,
  STARTUP_STDOUT,
  STARTUP_STDERR,
  STARTUP_STREAM_COUNT,
};

#define STARTUP_STREAM_NONE UINT64_C(0)

/* NONE requires all fields zero. Present streams and their optional observation
 * own distinct handles, never aliased by resources, roots or working directories.
 * STREAM is response stdin only and requires an HTTP_TRANSACTION STATUS-only
 * observation; both use CALL transport. Other streams have no observation.
 * Libc adopts the pair; native access borrows it, and fclose leaves this snapshot stale. */
struct startup_stream {
  uint64_t protocol; /* STARTUP_STREAM_NONE, PROTOCOL_CONSOLE, PROTOCOL_FILE, PROTOCOL_PIPE or PROTOCOL_STREAM. */
  handle_t handle;
  uint64_t observation_protocol; /* Zero when absent, otherwise PROTOCOL_HTTP_TRANSACTION. */
  handle_t observation;
};

struct startup_binding {
  uint64_t name;
  handle_t handle;
};

struct startup_variable {
  uint64_t name;
  uint64_t value;
};

/* RDI points to this kernel-prepared region. Addresses are child virtual
 * addresses, not offsets or kernel pointers. All strings are NUL-terminated.
 * size includes page padding; read_only_size separates metadata from writable
 * argv pointers and strings. Both parts are NX and live until process exit.
 * Counts bound arrays; empty metadata arrays have address zero. argv always
 * includes a final NULL, even when argc is zero. Version 1 has no legacy layout. */
struct startup_info {
  uint32_t version;
  uint32_t size;
  uint64_t read_only_size;
  uint64_t resources;
  uint64_t resource_count;
  uint64_t roots;
  uint64_t root_count;
  uint64_t working_directories; /* Boundary first, current directory last. */
  uint64_t working_directory_count;
  uint64_t working_path; /* Optional descriptive string, never authority. */
  uint64_t environment;
  uint64_t environment_count;
  uint64_t argc;
  uint64_t argv;
  struct startup_stream streams[STARTUP_STREAM_COUNT];
  handle_t namespace_handle; /* Optional explicit namespace grant; zero means absent. */
  /* Kernel-provisioned THREAD_RIGHT_SET_FS only. Owned, distinct from every
   * launcher-selected grant; libc closes it after its caller-only initial bind.
   * Ignoring or closing it never grants MANAGE/CREATE or affects other tasks. */
  handle_t thread_fs;
};

_Static_assert(sizeof(struct startup_binding) == 16, "startup binding layout");
_Static_assert(sizeof(struct startup_variable) == 16, "startup variable layout");
_Static_assert(sizeof(struct startup_stream) == 32, "startup stream layout");
_Static_assert(offsetof(struct startup_info, streams) == 104, "startup streams offset");
_Static_assert(offsetof(struct startup_info, namespace_handle) == 200, "startup namespace offset");
_Static_assert(offsetof(struct startup_info, thread_fs) == 208, "startup FS grant offset");
_Static_assert(sizeof(struct startup_info) == 216, "startup record layout");
_Static_assert(offsetof(struct startup_info, resources) == 16, "startup resources offset");
_Static_assert(offsetof(struct startup_info, argv) == 96, "startup argv offset");

#endif
