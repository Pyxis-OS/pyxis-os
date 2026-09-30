#ifndef KERNEL_FS_NATIVE_H
#define KERNEL_FS_NATIVE_H

#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <pyxis_fs/read.h>

#define NATIVEFS_REQUEST_LIMIT 32u
#define NATIVEFS_CORE_BYTES (8u * 1024u * 1024u)
#define NATIVEFS_ADAPTER_BYTES (1024u * 1024u)
#define NATIVEFS_WRAPPER_LIMIT 1024u
#define NATIVEFS_TIMEOUT_MS 30000u

struct nativefs_volume;

enum nativefs_job_state {
  NATIVEFS_JOB_IDLE, NATIVEFS_JOB_QUEUED, NATIVEFS_JOB_ACTIVE, NATIVEFS_JOB_COMPLETE,
};

/* Trusted kernel backing preparation, NOT policy acquisition. No syscall or
 * principal is accepted here. Task 4 must acquire a policy view before exposing
 * objects. Storage is kernel-owned/shared, never a user pointer or remote stack.
 * Fill an IDLE record, then submit on BSP/IF=0. Success lends the whole record
 * until COMPLETE; failure leaves it unchanged. Observe COMPLETE only on BSP
 * with IF=0. Consume the owned volume and reset to IDLE before reuse. This is
 * separate from the synchronous BSP user-request transport. */
struct nativefs_job {
  enum nativefs_job_state state;
  struct gpt_guid disk;
  uint32_t partition;
  struct pfs_name name;
  enum call_status status;
  enum pfs_status core_status;
  enum block_result backing_error;
  struct nativefs_volume *volume;
  /* Worker-owned after publication. */
  struct nativefs_job *next;
  uint64_t deadline;
};

/* BSP/IF=0, once after task_init and gpt_start. Idle until explicitly submitted.
 * No disk probing or mount is initiated by startup. */
void nativefs_start(void);
enum call_status nativefs_submit(struct nativefs_job *job);

/* BSP/IF=0, outside IRQ/fault entry. Drop one owned reference; final release
 * transfers it to the worker without allocation or a free request slot. */
void nativefs_volume_put(struct nativefs_volume *volume);

#endif
