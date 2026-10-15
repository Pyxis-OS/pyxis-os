#ifndef KERNEL_OBJECT_RETENTION_H
#define KERNEL_OBJECT_RETENTION_H

#include <abi/retention.h>
#include <abi/syscall.h>
#include <kernel/object/object.h>
#include <kernel/spinlock.h>
#include <kernel/object/capability.h>
#include <kernel/service/request.h>

struct space;
struct file_object;

/* Short IF=0 guard, nests no other locks; no allocation, grant callback or wait.
 * Backing FILEs own references. The account owns no FILE or process reference. */
struct retention_account {
  struct kernel_object object;
  struct spinlock lock;
  struct space *space; /* Origin identity; spaces survive processes/handles. */
  struct retention_limits limits;
  uint64_t retained_bytes;
};

enum retention_request_operation {
  RETENTION_REQUEST_ACCOUNT,
  RETENTION_REQUEST_FAILURE,
};

struct retention_request {
  struct bsp_request request;
  enum retention_request_operation operation;
  struct capability_reservation reservation;
  struct capability_reserved_slot slot;
  struct retention_limits limits;
  struct space *space;
  struct retention_account *account; /* Borrowed from admitted caller. */
  uint64_t outcome, detail_size;
  char detail[RESOURCE_DETAIL_MAX_BYTES + 1];
  handle_t result;
  enum call_status status;
};

struct kernel_object *retention_factory_create(void);
struct syscall_result retention_factory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
struct syscall_result retention_account_call(struct retention_account *account,
    uint64_t rights, uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity);
void retention_execute(struct retention_request *request);

/* File operation ownership, IF=0. Reserve growth before backing allocation;
 * refund on uncommitted failure and release shrink/final backing charge. */
bool retention_reserve(struct retention_account *account, uint64_t old_size,
    uint64_t new_size);
void retention_release(struct retention_account *account, uint64_t bytes);

#endif
