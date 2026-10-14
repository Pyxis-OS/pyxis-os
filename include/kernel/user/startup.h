#ifndef KERNEL_USER_STARTUP_H
#define KERNEL_USER_STARTUP_H

#include <kernel/mm/types.h>
#include <abi/startup.h>
#include <abi/environment.h>

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

/* BSP, IF=0, exclusively owned inactive/unsubmitted process. Copies selected
 * entries into its one table and publishes intrinsic ENV and SET_FS grants.
 * Cwd handles must already be installed; no ancestry or extra rights inferred.
 * All names are exact/unique. The 64 KiB budget includes environment names/text
 * and page padding. Failure frees provisional mappings/grants; the caller
 * discards the unpublished process and any already seeded entries. */
enum mm_result process_prepare_startup(struct process *process,
                                       const struct process_startup *source);

#endif
