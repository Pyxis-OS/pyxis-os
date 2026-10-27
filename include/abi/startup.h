#ifndef ABI_STARTUP_H
#define ABI_STARTUP_H

#include <abi/handle.h>
#include <stddef.h>

#define STARTUP_VERSION 1
#define STARTUP_MAX_SIZE UINT64_C(65536)

/* RDI points to this kernel-prepared region. Addresses are child virtual
 * addresses, not offsets or kernel pointers. Metadata is read-only; argv and
 * its strings are writable. Both mappings are NX and live until process exit.
 * One caller-bound environment owns every named capability/text entry. Cwd is
 * an independently owned directory chain, not another name lookup authority.
 * The launch budget includes the selected environment's names/text and mapping
 * padding even though values live in the native environment table. */
struct startup_info {
  uint32_t version;
  uint32_t size;
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

_Static_assert(sizeof(struct startup_info) == 80, "startup record layout");
_Static_assert(offsetof(struct startup_info, environment_handle) == 16, "startup environment offset");
_Static_assert(offsetof(struct startup_info, argv) == 56, "startup argv offset");
_Static_assert(offsetof(struct startup_info, thread_fs) == 64, "startup FS grant offset");
_Static_assert(offsetof(struct startup_info, usable_cpu_count) == 72, "startup CPU count offset");

#endif
