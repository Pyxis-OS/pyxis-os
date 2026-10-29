#ifndef KERNEL_ENVIRONMENT_H
#define KERNEL_ENVIRONMENT_H

#include <abi/environment.h>
#include <abi/syscall.h>
#include <abi/command_search.h>
#include <kernel/object/capability.h>
#include <kernel/object/object.h>
#include <kernel/service/request.h>

struct environment_table;
struct environment_cell;
struct environment_snapshot;
struct environment_staging;
struct file_object;

/* BSP owned tunable admission policies, not ABI constants. */
#define ENVIRONMENT_ENTRIES_LIMIT 1024
#define ENVIRONMENT_BYTES_LIMIT (1024 * 1024)
#define ENVIRONMENT_MUTATIONS_LIMIT 16

enum environment_request_action {
  ENVIRONMENT_ALLOCATE,
  ENVIRONMENT_COMMIT_SET,
  ENVIRONMENT_COMMIT_UNSET,
  ENVIRONMENT_CAPTURE,
  ENVIRONMENT_RELEASE_STAGING,
  ENVIRONMENT_CREATE_TEXT_FILE,
  ENVIRONMENT_ALLOCATE_COMMAND_LIST,
};

struct environment_request {
  struct bsp_request request;
  enum environment_request_action action;
  struct environment_table *table;
  struct environment_staging *staging;
  struct environment_set_entry entries[ENVIRONMENT_MUTATIONS_LIMIT];
  struct environment_unset_entry removals[ENVIRONMENT_MUTATIONS_LIMIT];
  size_t count;
  uint64_t flags;
  struct kernel_object *snapshot;
  struct environment_cell *cell; /* Caller retains the immutable cell through service. */
  struct file_object *file; /* Owned storage returned after sealing its bytes. */
  /* Borrowed caller-owned inputs throughout this synchronous BSP request. */
  const struct command_list_create_entry *command_entries;
  struct capability_grant *command_grants;
  struct kernel_object *command_list;
  enum call_status result;
};

/* IF=0. Tables are process-owned; admitted activity or exclusive unpublished
 * preparation keeps the table live. Destroy only after activity drains.
 * Seed helpers borrow names/text; seed_owned moves the logical grant only on
 * success. They are for exclusive child/bootstrap preparation. */
struct environment_table *environment_table_create(void);
void environment_table_destroy(struct environment_table *table);
enum call_status environment_table_seed_owned(struct environment_table *table,
    const char *name, uint64_t flags, struct capability_grant *grant);
enum call_status environment_table_seed_text(struct environment_table *table,
    const char *name, uint64_t flags, const char *text, size_t length);
/* Caller-bound stateless handle: copying this cannot reach another table. */
struct kernel_object *environment_create(void);

/* BSP, IF=0. Captures owned immutable cells. Cell capture/retain takes only the
 * short table guard; grant callbacks, allocation and copies are outside it. */
enum call_status environment_snapshot_capture(struct environment_table *table,
    uint64_t flags, struct kernel_object **snapshot);
/* Consumes the provisional logical owner returned by capture, independently
 * of admitted storage references. IF=0, no held guards, any CPU. */
void environment_snapshot_release(struct kernel_object *snapshot);
/* Object grant hooks; logical CLOSE releases captured cells synchronously. */
bool environment_snapshot_grant_retain(struct kernel_object *snapshot);
void environment_snapshot_grant_release(struct kernel_object *snapshot);
void environment_snapshot_info(struct kernel_object *snapshot,
    struct environment_info_reply *info);
/* Borrowed cell requires an owned logical snapshot grant or the provisional
 * capture owner throughout its use; a storage reference alone is insufficient.
 * Name/text are immutable. Clone
 * a CAP grant explicitly before delegation. No live table pointer is exposed. */
const struct environment_cell *environment_snapshot_cell(struct kernel_object *snapshot,
    size_t index);
const char *environment_cell_name(const struct environment_cell *cell);
uint64_t environment_cell_kind(const struct environment_cell *cell);
uint64_t environment_cell_flags(const struct environment_cell *cell);
uint64_t environment_cell_identity(const struct environment_cell *cell);
const char *environment_cell_text(const struct environment_cell *cell, size_t *length);
enum capability_result environment_cell_grant(const struct environment_cell *cell,
    struct capability_grant *grant);
/* BSP exclusive staging, capture identity is intentionally replaced in child. */
enum call_status environment_table_seed_cell(struct environment_table *table,
    const struct environment_cell *cell);

void environment_execute(struct environment_request *request);
struct syscall_result environment_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);

#endif
