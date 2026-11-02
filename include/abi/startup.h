#ifndef ABI_STARTUP_H
#define ABI_STARTUP_H

#include <abi/handle.h>
#include <stddef.h>

#define STARTUP_VERSION 1

/* RDI points to this kernel-prepared region. Addresses are child virtual
 * addresses, not offsets or kernel pointers. Metadata is read-only; argv and
 * its strings are writable. Both mappings are NX and live until process exit.
 * One caller-bound environment owns every named capability/text entry. Cwd is
 * an independently owned directory chain, not another name lookup authority.
 * Mapping sizes include page padding; environment names/text live in the
 * independently admitted native environment table. */
struct startup_info {
  uint64_t version;
  uint64_t size;
  uint64_t read_only_size;
  handle_t environment_handle;
  uint64_t working_directories; /* Boundary first, current directory last. */
  uint64_t working_directory_count;
  uint64_t working_path; /* Optional description, never authority. */
  uint64_t argc;
  uint64_t argv; /* Always ends in NULL, including when argc is zero. */
  /* Caller-only SET_FS, distinct from every selected grant. Libc closes this
   * after initial bind; it confers no MANAGE/CREATE authority. */
  handle_t thread_fs;
  /* Usable logical CPUs in this process's space at startup, always nonzero.
   * Kernel-prepared information, not authority or global hardware inventory. */
  uint64_t usable_cpu_count;
};

_Static_assert(sizeof(struct startup_info) == 88, "startup record layout");
_Static_assert(offsetof(struct startup_info, environment_handle) == 24, "startup environment offset");
_Static_assert(offsetof(struct startup_info, argv) == 64, "startup argv offset");
_Static_assert(offsetof(struct startup_info, thread_fs) == 72, "startup FS grant offset");
_Static_assert(offsetof(struct startup_info, usable_cpu_count) == 80, "startup CPU count offset");

#endif
