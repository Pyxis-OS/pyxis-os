#ifndef KERNEL_FS_NATIVE_H
#define KERNEL_FS_NATIVE_H

#include <abi/directory.h>
#include <abi/file.h>
#include <abi/syscall.h>
#include <kernel/block.h>
#include <kernel/gpt.h>
#include <kernel/service/request.h>
#include <pyxis_fs/access.h>

#define NATIVEFS_REQUEST_LIMIT 32u
#define NATIVEFS_CORE_BYTES (8u * 1024u * 1024u)
#define NATIVEFS_ADAPTER_BYTES (1024u * 1024u)
#define NATIVEFS_WRAPPER_LIMIT 1024u
#define NATIVEFS_TIMEOUT_MS 30000u
#define NATIVEFS_DIRECTORY_RIGHTS (DIRECTORY_RIGHT_LOOKUP | DIRECTORY_RIGHT_ENUMERATE | DIRECTORY_RIGHT_READ_FILES)

struct nativefs_node;
struct kernel_object;
struct nativefs_request;

enum nativefs_operation {
  NATIVEFS_ROOT, NATIVEFS_LOOKUP, NATIVEFS_ENUMERATE, NATIVEFS_READ, NATIVEFS_SIZE,
};
enum nativefs_job_state {
  NATIVEFS_JOB_IDLE, NATIVEFS_JOB_QUEUED, NATIVEFS_JOB_ACTIVE, NATIVEFS_JOB_COMPLETE,
};

/* Shared kernel storage, never user pointers or remote stacks. ROOT's principal
 * and disk are trusted embedding authority, not caller-supplied authentication.
 * Other operations derive children only from the held node/view and the calling
 * capability's actual rights. Inputs are borrowed until detached completion;
 * successful ROOT/LOOKUP transfers one object reference, failure transfers none.
 * READ/ENUMERATE publish data only on success. Core/backing diagnostics belong
 * to this operation alone. */
struct nativefs_job {
  enum nativefs_job_state state;
  enum nativefs_operation operation;
  struct gpt_guid disk;
  uint32_t partition;
  struct pfs_principal_id principal;
  struct nativefs_node *node;
  uint64_t rights, child_rights, kind, offset;
  size_t count;
  char name[PFS_NAME_MAX + 1];
  struct directory_cursor cursor;
  struct directory_enumerate_reply entry;
  uint8_t data[FILE_READ_MAX_BYTES];
  enum call_status status;
  enum pfs_status core_status;
  enum block_result backing_error;
  struct kernel_object *object;
  /* Internal queue/completion ownership. */
  struct nativefs_job *next;
  struct nativefs_request *user_request;
  uint64_t deadline;
  bool admitted;
};

/* One provisioned request per user task. A live capability retains node through
 * the uninterruptible call; no capability entry or user buffer crosses to BSP.
 * Consume/detach outputs before release, including on stop. */
struct nativefs_request {
  struct bsp_request request;
  struct nativefs_job job;
};

struct nativefs_request *nativefs_request_prepare(enum nativefs_operation operation);
void nativefs_request_submit_and_wait(struct nativefs_request *request);
void nativefs_request_release(struct nativefs_request *request);
/* Any publishing CPU, IF=0. Includes time in the executor FIFO and reserves one
 * of the 32 slots there; saturation is completed by the executor with BUSY. */
void nativefs_request_published(struct nativefs_request *request);
/* BSP/IF=0. Takes FORWARDED ownership; no access after transfer. */
void nativefs_forward(struct nativefs_request *request);

/* Trusted asynchronous kernel submission, BSP/IF=0. IDLE -> QUEUED on success;
 * rejection preserves the record. Caller owns/retains input references until
 * COMPLETE and can inspect completion only on BSP/IF=0. This does not submit a
 * synchronous executor request from a kernel worker. No automatic mount/probe. */
enum call_status nativefs_submit(struct nativefs_job *job);
/* BSP/IF=0 once after task_init and gpt_start. */
void nativefs_start(void);
/* BSP/IF=0 object destructor: allocation-free transfer of wrapper, core view,
 * backing reference and deferred group cleanup to the owning worker. */
void nativefs_retire(struct nativefs_node *node);

#endif
