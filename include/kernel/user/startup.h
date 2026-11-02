#ifndef KERNEL_USER_STARTUP_H
#define KERNEL_USER_STARTUP_H

#include <kernel/mm/types.h>
#include <abi/startup.h>
#include <abi/environment.h>
#include <abi/limit.h>
#include <abi/syscall.h>

struct process;

/* Child preparation only: names/text are stable borrowed kernel storage.
 * CAP value is an installed child handle, TEXT points to the immutable text.
 * The environment takes its own logical grant and initial handles are retired
 * before the child is runnable, except the independently owned cwd chain. */
struct process_environment_entry {
  const char *name;
  uint64_t kind;
  uint64_t flags;
  uint64_t value;
  size_t length;
};

struct process_startup {
  const struct process_environment_entry *entries;
  size_t entry_count;
  const handle_t *working_directories;
  size_t working_directory_count;
  const char *working_path;
  size_t argc;
  const char *const *argv;
};

/* Borrowed kernel serialization scratch; its owner retains it through startup. */
struct process_startup_storage {
  size_t metadata;
  size_t arguments;
  uint8_t *buffer;
};

/* Pure owned-metadata validation and native environment admission. */
enum call_status process_startup_measure(const struct process_startup *source,
    struct process_startup_storage *storage, struct call_limit *detail);

/* IF=0, exclusively owned inactive/unsubmitted process. The caller may provide
 * preallocated serialization storage; NULL prepares local scratch for boot.
 * Copies selected entries into the native table and publishes intrinsic ENV
 * and SET_FS grants. Failure retains its original status/detail; the caller
 * discards the unpublished process and any already seeded entries. */
enum call_status process_prepare_startup(struct process *process,
    const struct process_startup *source, struct process_startup_storage *storage,
    struct call_limit *detail);

#endif
