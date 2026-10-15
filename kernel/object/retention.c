#include <abi/retention.h>
#include <arch/smp.h>
#include <kernel/object/retention.h>
#include <kernel/object/describe.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/output.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

static enum call_status cap_status(enum capability_result status)
{
  switch (status) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT:
  case CAP_FULL: return CALL_LIMIT;
  default: return CALL_BAD_REQUEST;
  }
}

static void destroy_factory(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *retention_factory_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *factory = kmalloc(sizeof(*factory));
  if (factory) {
    object_init(factory, OBJECT_RETENTION_FACTORY, destroy_factory);
  }
  return factory;
}

static void destroy_account(struct kernel_object *object)
{
  struct retention_account *account = (struct retention_account *)object;
  KASSERT(!account->retained_bytes);
  kfree(account);
}

static struct retention_account *create_account(struct space *space,
    struct retention_limits limits)
{
  struct retention_account *account = kmalloc(sizeof(*account));
  if (!account) {
    return NULL;
  }
  *account = (struct retention_account){.space = space, .limits = limits};
  atomic_init(&account->lock.locked, false);
  object_init(&account->object, OBJECT_RETENTION_ACCOUNT, destroy_account);
  return account;
}

bool retention_reserve(struct retention_account *account, uint64_t old_size,
    uint64_t new_size)
{
  KASSERT(new_size >= old_size);
  uint64_t growth = new_size - old_size;
  spin_lock(&account->lock);
  KASSERT(account->retained_bytes <= account->limits.session_limit);
  bool fits = new_size <= account->limits.value_limit &&
      growth <= account->limits.session_limit - account->retained_bytes;
  if (fits) {
    account->retained_bytes += growth;
  }
  spin_unlock(&account->lock);
  return fits;
}

void retention_release(struct retention_account *account, uint64_t bytes)
{
  spin_lock(&account->lock);
  KASSERT(bytes <= account->retained_bytes);
  account->retained_bytes -= bytes;
  spin_unlock(&account->lock);
}

void retention_execute(struct retention_request *request)
{
  struct kernel_object *object = NULL;
  uint64_t rights;
  if (request->operation == RETENTION_REQUEST_ACCOUNT) {
    struct retention_account *account = create_account(request->space, request->limits);
    if (account) {
      object = &account->object;
    }
    rights = RETENTION_RIGHT_CAPTURE;
  } else {
    KASSERT(request->operation == RETENTION_REQUEST_FAILURE);
    struct file_object *file = file_create_failure(request->account, request->outcome,
        request->detail, request->detail_size);
    if (file) {
      object = &file->object;
    }
    rights = FILE_RIGHT_READ;
  }
  request->status = CALL_NO_MEMORY;
  if (!object) {
    return;
  }
  struct capability_grant grant = {0};
  enum capability_result result = capability_grant_retain(object, rights, 0, &grant);
  if (result == CAP_OK) {
    result = forwarder_derive_grant(&grant, task_forwarder_operation());
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, &grant, 1);
  }
  if (result == CAP_OK && !task_operation_start()) {
    result = CAP_DENIED;
  }
  if (result == CAP_OK) {
    capability_install_reserved(&request->reservation, &request->slot, &grant, 1,
        &request->result);
  }
  request->status = cap_status(result);
  capability_grant_release(&grant);
  object_release(object);
}

static struct syscall_result submit_create(enum retention_request_operation operation,
    struct retention_account *account, struct retention_limits limits,
    uint64_t outcome, const char *detail, size_t detail_size, uintptr_t reply_address)
{
  if (!user_buffer_check(reply_address, sizeof(handle_t), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum capability_result cap = capability_request_reservation(1, &reservation, &slot);
  if (cap != CAP_OK) {
    return (struct syscall_result){cap_status(cap), 0};
  }
  struct retention_request *request =
      (struct retention_request *)bsp_request_prepare(BSP_SERVICE_RETENTION);
  request->operation = operation;
  request->reservation = reservation;
  request->slot = slot;
  request->limits = limits;
  request->space = process_current()->space;
  request->account = account;
  request->outcome = outcome;
  request->detail_size = detail_size;
  memset(request->detail, 0, sizeof(request->detail));
  if (detail_size) {
    memcpy(request->detail, detail, detail_size);
  }
  request->result = HANDLE_INVALID;
  request->status = CALL_NO_MEMORY;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  handle_t result = request->result;
  capability_reservation_release(&request->reservation, &request->slot);
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &result, sizeof(result), &result, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(result)};
}

struct syscall_result retention_factory_call(uint64_t rights, uint64_t operation,
    uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & RETENTION_FACTORY_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation != RETENTION_CREATE_ACCOUNT) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct retention_limits limits;
  if (request_size != sizeof(limits) || reply_capacity < sizeof(struct retention_account_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&limits, request_address, sizeof(limits))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!limits.value_limit || !limits.session_limit ||
      limits.value_limit > SIZE_MAX || limits.session_limit > SIZE_MAX) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  return submit_create(RETENTION_REQUEST_ACCOUNT, NULL, limits, 0, NULL, 0, reply_address);
}

struct syscall_result retention_account_call(struct retention_account *account,
    uint64_t rights, uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & RETENTION_RIGHT_CAPTURE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == RETENTION_CREATE_OUTPUT) {
    return output_create_call(account, request_size, reply_address, reply_capacity);
  }
  if (operation == RETENTION_STATUS) {
    if (request_size || reply_capacity < sizeof(struct retention_status_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    spin_lock(&account->lock);
    struct retention_status_reply reply = {account->limits.value_limit,
      account->limits.session_limit, account->retained_bytes};
    spin_unlock(&account->lock);
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (operation != RETENTION_CREATE_FAILURE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  struct retention_failure_request input;
  char detail[RESOURCE_DETAIL_MAX_BYTES + 1] = {0};
  if (request_size < sizeof(input) || request_size > sizeof(input) + RESOURCE_DETAIL_MAX_BYTES ||
      reply_capacity < sizeof(struct retention_failure_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&input, request_address, sizeof(input))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!input.outcome || input.outcome >= RESOURCE_OUTCOME_COUNT ||
      input.detail_size != request_size - sizeof(input)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(detail, request_address + sizeof(input), input.detail_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!description_string_valid(detail, input.detail_size, sizeof(detail))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  return submit_create(RETENTION_REQUEST_FAILURE, account, (struct retention_limits){0},
      input.outcome, detail, input.detail_size, reply_address);
}
