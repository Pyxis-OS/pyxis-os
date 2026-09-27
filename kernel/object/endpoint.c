#include <abi/endpoint.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/endpoint.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user_memory.h>

enum delivery_state { DELIVERY_FREE, DELIVERY_FILLING, DELIVERY_QUEUED,
                      DELIVERY_RECEIVED, DELIVERY_COMPLETE };

/* Exactly the public prefix, without putting a page-sized packet on a stack. */
struct packet_header {
  handle_t receipt;
  uint64_t delivery, kind, result, size, grant_count, deadline_ns;
  struct endpoint_grant grants[ENDPOINT_GRANTS_MAX];
};
_Static_assert(sizeof(struct packet_header) == ENDPOINT_PACKET_HEADER_SIZE,
               "endpoint packet prefix");

struct endpoint_delivery_record {
  struct kernel_object receipt;
  struct endpoint_state *endpoint;
  struct endpoint_delivery_record *next;
  struct task_wait *wait;
  enum delivery_state state;
  enum endpoint_message_kind kind;
  enum call_status status;
  bool caller_active, receipt_live, delivered, cancel_pending;
  handle_t receipt_handle;
  uint64_t deadline_ns;
  uint64_t request_size, reply_size, result;
  size_t request_count, reply_count;
  struct kernel_object *request_grants[ENDPOINT_GRANTS_MAX];
  struct kernel_object *reply_grants[ENDPOINT_GRANTS_MAX];
  uint64_t request_rights[ENDPOINT_GRANTS_MAX], reply_rights[ENDPOINT_GRANTS_MAX];
  uint8_t request[ENDPOINT_DATA_MAX], reply[ENDPOINT_DATA_MAX];
};

struct endpoint_state {
  atomic_bool locked;
  struct endpoint caller, receiver;
  struct endpoint_delivery_record deliveries[ENDPOINT_DELIVERIES_MAX];
  struct endpoint_delivery_record *head, *tail;
  struct task_wait *waiting_receiver;
  size_t storage_references;
  bool closed;
};

/* IF=0. Endpoint -> scheduler/retirement locks, never the reverse. No
 * allocation, user copies or parking while held. All published data lives in
 * persistent heap mappings, not another CPU's private kernel stack. */
static void lock_endpoint(struct endpoint_state *state)
{
  while (atomic_exchange_explicit(&state->locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_endpoint(struct endpoint_state *state)
{
  atomic_store_explicit(&state->locked, false, memory_order_release);
}

static enum call_status grant_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT: return CALL_LIMIT;
  default: panic("unexpected endpoint capability result %u", (unsigned)result);
  }
}

static void release_grants(struct kernel_object **grants, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    object_release(grants[i]);
    grants[i] = NULL;
  }
}

static void wake_waiter(struct task_wait **slot)
{
  struct task_wait *wait = *slot;
  *slot = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void free_delivery(struct endpoint_delivery_record *record)
{
  if (!record->caller_active && !record->receipt_live) {
    KASSERT(record->state == DELIVERY_COMPLETE);
    KASSERT(!record->cancel_pending);
    record->state = DELIVERY_FREE;
  }
}

static bool deadline_expired(uint64_t deadline_ns)
{
  return deadline_ns && task_deadline_expired(deadline_ns);
}

/* The endpoint lock serializes expiry with reply, receipt close and owner exit. */
static void expire_delivery(struct endpoint_state *state,
    struct endpoint_delivery_record *record)
{
  KASSERT(record->kind == ENDPOINT_MESSAGE_CALL);
  KASSERT(deadline_expired(record->deadline_ns));
  KASSERT(record->state == DELIVERY_QUEUED || record->state == DELIVERY_RECEIVED);
  if (record->state == DELIVERY_QUEUED) {
    struct endpoint_delivery_record *previous = NULL;
    struct endpoint_delivery_record **link = &state->head;
    while (*link != record) {
      KASSERT(*link);
      previous = *link;
      link = &(*link)->next;
    }
    *link = record->next;
    if (state->tail == record) {
      state->tail = previous;
    }
    record->next = NULL;
    release_grants(record->request_grants, record->request_count);
    record->request_count = 0;
    object_release(&record->receipt);
  } else {
    record->cancel_pending = true;
    wake_waiter(&state->waiting_receiver);
  }
  record->state = DELIVERY_COMPLETE;
  record->status = CALL_TIMED_OUT;
  wake_waiter(&record->wait);
}

static void destroy_receipt(struct kernel_object *object)
{
  struct endpoint_delivery_record *record = (struct endpoint_delivery_record *)object;
  struct endpoint_state *state = record->endpoint;
  lock_endpoint(state);
  KASSERT(record->receipt_live);
  if (record->state == DELIVERY_RECEIVED) {
    record->state = DELIVERY_COMPLETE;
    record->status = record->kind == ENDPOINT_MESSAGE_CALL ?
        (deadline_expired(record->deadline_ns) ? CALL_TIMED_OUT : CALL_ABANDONED) : CALL_OK;
    wake_waiter(&record->wait);
  }
  record->cancel_pending = false;
  record->receipt_handle = HANDLE_INVALID;
  record->receipt_live = false;
  free_delivery(record);
  bool destroy = --state->storage_references == 0;
  unlock_endpoint(state);
  if (destroy) {
    kfree(state);
  }
}

static void close_endpoint(struct endpoint_state *state)
{
  /* Called with the lock; completed replies have already won the transition. */
  state->closed = true;
  state->head = state->tail = NULL;
  for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
    struct endpoint_delivery_record *record = &state->deliveries[i];
    if (record->state != DELIVERY_QUEUED && record->state != DELIVERY_RECEIVED) {
      continue;
    }
    if (record->state == DELIVERY_QUEUED) {
      release_grants(record->request_grants, record->request_count);
      record->request_count = 0;
      /* A queued receipt still has its initial, unpublished reference. */
      object_release(&record->receipt);
    }
    record->next = NULL;
    record->state = DELIVERY_COMPLETE;
    record->status = deadline_expired(record->deadline_ns) ?
        CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    record->cancel_pending = false;
    wake_waiter(&record->wait);
  }
  for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
    state->deliveries[i].cancel_pending = false;
  }
  wake_waiter(&state->waiting_receiver);
}

void endpoint_handle_close(struct kernel_object *object)
{
  if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    struct endpoint_state *state = ((struct endpoint *)object)->state;
    lock_endpoint(state);
    close_endpoint(state);
    unlock_endpoint(state);
  } else if (object->type == OBJECT_ENDPOINT_RECEIPT) {
    struct endpoint_delivery_record *record = (struct endpoint_delivery_record *)object;
    struct endpoint_state *state = record->endpoint;
    lock_endpoint(state);
    if (record->state == DELIVERY_RECEIVED) {
      record->state = DELIVERY_COMPLETE;
      record->status = record->kind == ENDPOINT_MESSAGE_CALL ?
          (deadline_expired(record->deadline_ns) ? CALL_TIMED_OUT : CALL_ABANDONED) : CALL_OK;
      wake_waiter(&record->wait);
    }
    record->cancel_pending = false;
    record->receipt_handle = HANDLE_INVALID;
    unlock_endpoint(state);
  }
}

static void destroy_endpoint(struct kernel_object *object)
{
  struct endpoint *endpoint = (struct endpoint *)object;
  struct endpoint_state *state = endpoint->state;
  /* Owner lists are changed only by creation, destruction and exit on BSP. */
  if (endpoint->owner) {
    struct endpoint **link = &endpoint->owner->endpoints;
    while (*link != endpoint) {
      KASSERT(*link);
      link = &(*link)->owner_next;
    }
    *link = endpoint->owner_next;
  }
  lock_endpoint(state);
  if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    endpoint->owner = NULL;
    close_endpoint(state);
  }
  bool destroy = --state->storage_references == 0;
  unlock_endpoint(state);
  if (destroy) {
    kfree(state);
  }
}

void endpoint_process_exit(struct process *owner)
{
  KASSERT(arch_cpu_index() == 0);
  struct endpoint *endpoint = owner->endpoints;
  owner->endpoints = NULL;
  while (endpoint) {
    struct endpoint *next = endpoint->owner_next;
    struct endpoint_state *state = endpoint->state;
    lock_endpoint(state);
    endpoint->owner = NULL;
    endpoint->owner_next = NULL;
    close_endpoint(state);
    unlock_endpoint(state);
    endpoint = next;
  }
}

enum call_status endpoint_create(struct process *owner, struct endpoint_create_reply *reply)
{
  KASSERT(arch_cpu_index() == 0);
  while (capability_free_slots(&owner->capabilities) < 2) {
    enum capability_result result = capability_grow(&owner->capabilities);
    if (result != CAP_OK) {
      return grant_status(result);
    }
  }
  struct endpoint_state *state = kmalloc(sizeof(*state));
  if (!state) {
    return CALL_NO_MEMORY;
  }
  memset(state, 0, sizeof(*state));
  atomic_init(&state->locked, false);
  state->storage_references = 2;
  state->caller.state = state->receiver.state = state;
  state->receiver.owner = owner;
  state->receiver.owner_next = owner->endpoints;
  owner->endpoints = &state->receiver;
  object_init(&state->caller.object, OBJECT_ENDPOINT, destroy_endpoint);
  object_init(&state->receiver.object, OBJECT_ENDPOINT_RECEIVER, destroy_endpoint);
  struct kernel_object *objects[] = {&state->receiver.object, &state->caller.object};
  uint64_t rights[] = {ENDPOINT_RIGHT_RECEIVE, ENDPOINT_RIGHT_SEND | ENDPOINT_RIGHT_RECEIVE};
  handle_t handles[2];
  enum capability_result result = capability_insert_batch(&owner->capabilities,
      objects, rights, 2, handles);
  object_release(&state->caller.object);
  object_release(&state->receiver.object);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  *reply = (struct endpoint_create_reply){handles[0], handles[1]};
  return CALL_OK;
}

static enum call_status reserve_handles(size_t count)
{
  while (capability_free_slots(&process_current()->capabilities) < count) {
    enum capability_result result = task_grow_capabilities();
    if (result != CAP_OK) {
      return grant_status(result);
    }
  }
  return CALL_OK;
}

static enum call_status capture_grants(const struct endpoint_message *message,
    struct kernel_object **objects, uint64_t *rights)
{
  for (size_t i = 0; i < message->grant_count; ++i) {
    struct kernel_object *object;
    enum capability_result result = capability_resolve(&process_current()->capabilities,
        message->grants[i].handle, message->grants[i].rights, &object, NULL);
    if (result != CAP_OK) {
      release_grants(objects, i);
      return grant_status(result);
    }
    if (object->type == OBJECT_ENDPOINT_RECEIPT || object->type == OBJECT_ENDPOINT_RECEIVER) {
      release_grants(objects, i);
      return CALL_DENIED;
    }
    if (!object_retain(object)) {
      release_grants(objects, i);
      return CALL_LIMIT;
    }
    objects[i] = object;
    rights[i] = message->grants[i].rights;
  }
  return CALL_OK;
}

static enum call_status read_message(uintptr_t address, size_t size,
    struct endpoint_message *message)
{
  if (size != sizeof(*message) - sizeof(message->header)) {
    return CALL_BAD_REQUEST;
  }
  if (!copy_from_user(&message->buffer, address, size)) {
    return CALL_BAD_BUFFER;
  }
  if (message->size > ENDPOINT_DATA_MAX || message->grant_count > ENDPOINT_GRANTS_MAX) {
    return CALL_BAD_REQUEST;
  }
  for (size_t i = message->grant_count; i < ENDPOINT_GRANTS_MAX; ++i) {
    if (message->grants[i].handle || message->grants[i].rights) {
      return CALL_BAD_REQUEST;
    }
  }
  if (!user_buffer_check(message->buffer, message->size, USER_BUFFER_READ)) {
    return CALL_BAD_BUFFER;
  }
  return CALL_OK;
}

static struct syscall_result write_packet(uintptr_t address,
    const struct packet_header *header, const uint8_t *bytes, enum call_status status)
{
  KASSERT(copy_to_user(address, header, sizeof(*header)));
  if (header->size) {
    KASSERT(copy_to_user(address + sizeof(*header), bytes, header->size));
  }
  return (struct syscall_result){status, sizeof(*header) + header->size};
}

/* Admission owns all copied bytes and references. A SEND publishes no task
 * pointer and never touches its record after unlocking: the receiver may
 * already finish it before the sender returns. A CALL keeps its record until
 * collection, independently of receipt retirement. */
static enum call_status admit_message(struct endpoint *endpoint,
    const struct endpoint_message *message, struct task_wait *wait,
    struct endpoint_delivery_record **result)
{
  KASSERT((wait != NULL) == (result != NULL));
  struct kernel_object *grants[ENDPOINT_GRANTS_MAX];
  uint64_t rights[ENDPOINT_GRANTS_MAX];
  enum call_status status = capture_grants(message, grants, rights);
  if (status != CALL_OK) {
    return status;
  }
  struct endpoint_state *state = endpoint->state;
  struct endpoint_delivery_record *record = NULL;
  lock_endpoint(state);
  if (wait && deadline_expired(message->deadline_ns)) {
    status = CALL_TIMED_OUT;
  } else if (state->closed) {
    status = CALL_ENDPOINT_CLOSED;
  } else {
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      if (state->deliveries[i].state == DELIVERY_FREE) {
        record = &state->deliveries[i];
        record->state = DELIVERY_FILLING;
        break;
      }
    }
    if (!record) {
      status = CALL_QUEUE_FULL;
    }
  }
  unlock_endpoint(state);
  if (status != CALL_OK) {
    release_grants(grants, message->grant_count);
    return status;
  }
  /* The reserved slot is private until publication. Owner exit leaves filling
   * slots to their callers, which recheck closure before admitting them. */
  if (message->size) {
    KASSERT(copy_from_user(record->request, message->buffer, message->size));
  }
  lock_endpoint(state);
  bool expired = wait && deadline_expired(message->deadline_ns);
  if (expired || state->closed) {
    record->state = DELIVERY_FREE;
    status = expired ? CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    unlock_endpoint(state);
    release_grants(grants, message->grant_count);
    return status;
  }
  record->endpoint = state;
  record->wait = wait;
  record->next = NULL;
  record->kind = wait ? ENDPOINT_MESSAGE_CALL : ENDPOINT_MESSAGE_SEND;
  record->caller_active = wait != NULL;
  record->receipt_live = true;
  record->delivered = false;
  record->cancel_pending = false;
  record->receipt_handle = HANDLE_INVALID;
  record->deadline_ns = message->deadline_ns;
  record->request_size = message->size;
  record->request_count = message->grant_count;
  record->reply_count = record->reply_size = record->result = 0;
  record->status = CALL_OK;
  for (size_t i = 0; i < message->grant_count; ++i) {
    record->request_grants[i] = grants[i];
    record->request_rights[i] = rights[i];
  }
  object_init(&record->receipt, OBJECT_ENDPOINT_RECEIPT, destroy_receipt);
  ++state->storage_references;
  record->state = DELIVERY_QUEUED;
  if (state->tail) {
    state->tail->next = record;
  } else {
    state->head = record;
  }
  state->tail = record;
  if (result) {
    *result = record;
  }
  wake_waiter(&state->waiting_receiver);
  unlock_endpoint(state);
  return CALL_OK;
}

static struct syscall_result send_endpoint(struct endpoint *endpoint,
    uintptr_t request_address, size_t request_size)
{
  struct endpoint_message message;
  enum call_status status = read_message(request_address, request_size, &message);
  if (status == CALL_OK && (message.result || message.deadline_ns)) {
    status = CALL_BAD_REQUEST;
  }
  if (status == CALL_OK) {
    status = admit_message(endpoint, &message, NULL, NULL);
  }
  return (struct syscall_result){status, 0};
}

static struct syscall_result call_endpoint(struct endpoint *endpoint,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address)
{
  struct packet_header output = {.kind = ENDPOINT_MESSAGE_CALL};
  struct endpoint_message message;
  enum call_status status = read_message(request_address, request_size, &message);
  if (status == CALL_OK) {
    output.deadline_ns = message.deadline_ns;
  }
  if (status == CALL_OK && message.result) {
    status = CALL_BAD_REQUEST;
  }
  if (status == CALL_OK && deadline_expired(message.deadline_ns)) {
    status = CALL_TIMED_OUT;
  }
  /* Reserve collection slots before admission: growth failure cannot discard
   * a completed operation's result. This process has only one executing task. */
  if (status == CALL_OK) {
    status = reserve_handles(ENDPOINT_GRANTS_MAX);
  }
  if (status != CALL_OK) {
    return write_packet(reply_address, &output, NULL, status);
  }
  struct task_wait *wait = task_wait_prepare();
  struct endpoint_delivery_record *record;
  status = admit_message(endpoint, &message, wait, &record);
  if (status != CALL_OK) {
    return write_packet(reply_address, &output, NULL, status);
  }
  if (message.deadline_ns) {
    task_wait_sleep_until(wait, message.deadline_ns);
  } else {
    task_wait_sleep(wait);
  }

  struct endpoint_state *state = endpoint->state;
  lock_endpoint(state);
  if (record->state != DELIVERY_COMPLETE) {
    KASSERT(deadline_expired(record->deadline_ns));
    /* Timed sleep can return with the wait pointer still published. */
    expire_delivery(state, record);
  }
  KASSERT(record->state == DELIVERY_COMPLETE && !record->wait);
  status = record->status;
  output.delivery = record->delivered ? ENDPOINT_DELIVERED : ENDPOINT_NOT_DELIVERED;
  if (status == CALL_OK) {
    handle_t handles[ENDPOINT_GRANTS_MAX];
    enum capability_result result = capability_insert_batch(&process_current()->capabilities,
        record->reply_grants, record->reply_rights, record->reply_count, handles);
    KASSERT(result != CAP_FULL);
    status = grant_status(result);
    if (status == CALL_OK) {
      output.result = record->result;
      output.size = record->reply_size;
      output.grant_count = record->reply_count;
      for (size_t i = 0; i < record->reply_count; ++i) {
        output.grants[i] = (struct endpoint_grant){handles[i], record->reply_rights[i]};
      }
    }
  }
  release_grants(record->reply_grants, record->reply_count);
  record->reply_count = 0;
  unlock_endpoint(state);
  struct syscall_result result = write_packet(reply_address, &output, record->reply, status);
  lock_endpoint(state);
  record->caller_active = false;
  free_delivery(record);
  unlock_endpoint(state);
  return result;
}

static struct syscall_result receive_endpoint(struct endpoint *endpoint, uintptr_t reply_address)
{
  struct endpoint_state *state = endpoint->state;
  for (;;) {
    struct task_wait *wait = task_wait_prepare();
    lock_endpoint(state);
    if (state->closed) {
      unlock_endpoint(state);
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      struct endpoint_delivery_record *due = &state->deliveries[i];
      if ((due->state == DELIVERY_QUEUED || due->state == DELIVERY_RECEIVED) &&
          due->kind == ENDPOINT_MESSAGE_CALL && deadline_expired(due->deadline_ns)) {
        expire_delivery(state, due);
      }
    }
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      struct endpoint_delivery_record *cancel = &state->deliveries[i];
      if (!cancel->cancel_pending) {
        continue;
      }
      KASSERT(cancel->state == DELIVERY_COMPLETE && cancel->receipt_live &&
          cancel->receipt_handle);
      struct packet_header output = {.receipt = cancel->receipt_handle,
        .delivery = ENDPOINT_DELIVERED, .kind = ENDPOINT_MESSAGE_CANCEL,
        .deadline_ns = cancel->deadline_ns};
      cancel->cancel_pending = false;
      unlock_endpoint(state);
      return write_packet(reply_address, &output, NULL, CALL_OK);
    }
    struct endpoint_delivery_record *record = state->head;
    if (!record) {
      KASSERT(!state->waiting_receiver);
      state->waiting_receiver = wait;
      unlock_endpoint(state);
      task_wait_sleep(wait);
      continue;
    }
    size_t count = record->request_count;
    if (capability_free_slots(&process_current()->capabilities) < count + 1) {
      unlock_endpoint(state);
      enum call_status status = reserve_handles(count + 1);
      if (status != CALL_OK) {
        return (struct syscall_result){status, 0};
      }
      continue;
    }
    struct kernel_object *objects[ENDPOINT_GRANTS_MAX + 1];
    uint64_t rights[ENDPOINT_GRANTS_MAX + 1];
    handle_t handles[ENDPOINT_GRANTS_MAX + 1];
    objects[0] = &record->receipt;
    rights[0] = record->kind == ENDPOINT_MESSAGE_CALL ? ENDPOINT_RIGHT_REPLY : 0;
    for (size_t i = 0; i < count; ++i) {
      objects[i + 1] = record->request_grants[i];
      rights[i + 1] = record->request_rights[i];
    }
    enum capability_result result = capability_insert_batch(&process_current()->capabilities,
        objects, rights, count + 1, handles);
    if (result != CAP_OK) {
      unlock_endpoint(state);
      return (struct syscall_result){grant_status(result), 0};
    }
    state->head = record->next;
    if (!state->head) {
      state->tail = NULL;
    }
    record->next = NULL;
    record->state = DELIVERY_RECEIVED;
    record->delivered = true;
    record->receipt_handle = handles[0];
    struct packet_header output = {.receipt = handles[0], .delivery = ENDPOINT_DELIVERED,
      .kind = record->kind, .size = record->request_size, .grant_count = count,
      .deadline_ns = record->deadline_ns};
    for (size_t i = 0; i < count; ++i) {
      output.grants[i] = (struct endpoint_grant){handles[i + 1], rights[i + 1]};
    }
    release_grants(record->request_grants, count);
    record->request_count = 0;
    object_release(&record->receipt);
    unlock_endpoint(state);
    return write_packet(reply_address, &output, record->request, CALL_OK);
  }
}

static struct syscall_result reply_endpoint(struct endpoint_delivery_record *record,
    handle_t handle, uintptr_t request_address, size_t request_size)
{
  struct endpoint_message message;
  enum call_status status = read_message(request_address, request_size, &message);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (message.deadline_ns) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct kernel_object *grants[ENDPOINT_GRANTS_MAX];
  uint64_t rights[ENDPOINT_GRANTS_MAX];
  status = capture_grants(&message, grants, rights);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct endpoint_state *state = record->endpoint;
  lock_endpoint(state);
  if (record->state == DELIVERY_RECEIVED && deadline_expired(record->deadline_ns)) {
    expire_delivery(state, record);
  }
  if (record->state != DELIVERY_RECEIVED || state->closed) {
    status = record->status == CALL_TIMED_OUT ? CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    unlock_endpoint(state);
    release_grants(grants, message.grant_count);
    return (struct syscall_result){status, 0};
  }
  unlock_endpoint(state);
  if (message.size) {
    KASSERT(copy_from_user(record->reply, message.buffer, message.size));
  }
  lock_endpoint(state);
  if (record->state == DELIVERY_RECEIVED && deadline_expired(record->deadline_ns)) {
    expire_delivery(state, record);
  }
  if (record->state != DELIVERY_RECEIVED || state->closed) {
    status = record->status == CALL_TIMED_OUT ? CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    unlock_endpoint(state);
    release_grants(grants, message.grant_count);
    return (struct syscall_result){status, 0};
  }
  record->reply_size = message.size;
  record->result = message.result;
  record->reply_count = message.grant_count;
  for (size_t i = 0; i < message.grant_count; ++i) {
    record->reply_grants[i] = grants[i];
    record->reply_rights[i] = rights[i];
  }
  record->status = CALL_OK;
  record->state = DELIVERY_COMPLETE;
  wake_waiter(&record->wait);
  unlock_endpoint(state);
  KASSERT(capability_close(&process_current()->capabilities, handle) == CAP_OK);
  return (struct syscall_result){CALL_OK, 0};
}

static void destroy_service(struct kernel_object *object)
{
  kfree(object);
}

struct kernel_object *endpoint_service_create(void)
{
  KASSERT(arch_cpu_index() == 0);
  struct kernel_object *object = kmalloc(sizeof(*object));
  if (object) {
    object_init(object, OBJECT_ENDPOINT_SERVICE, destroy_service);
  }
  return object;
}

struct syscall_result endpoint_service_call(uint64_t rights, uint64_t operation,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & ENDPOINT_SERVICE_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation != ENDPOINT_CREATE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (request_size || reply_capacity < sizeof(struct endpoint_create_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct endpoint_create_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct endpoint_create_reply reply;
  enum call_status status = task_create_endpoint(&reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result endpoint_call(struct kernel_object *object, handle_t handle,
    uint64_t rights, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t required;
  if (object->type == OBJECT_ENDPOINT) {
    if (operation != ENDPOINT_CALL && operation != ENDPOINT_SEND) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    required = ENDPOINT_RIGHT_SEND;
    if (operation == ENDPOINT_CALL) {
      required |= ENDPOINT_RIGHT_RECEIVE;
    }
  } else if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    if (operation != ENDPOINT_RECEIVE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    required = ENDPOINT_RIGHT_RECEIVE;
  } else {
    KASSERT(object->type == OBJECT_ENDPOINT_RECEIPT);
    if (operation != ENDPOINT_REPLY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    required = ENDPOINT_RIGHT_REPLY;
  }
  if ((rights & required) != required) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (object->type == OBJECT_ENDPOINT_RECEIPT) {
    struct endpoint_delivery_record *record = (struct endpoint_delivery_record *)object;
    struct endpoint_state *state = record->endpoint;
    lock_endpoint(state);
    bool owner = state->receiver.owner == process_current();
    bool closed = state->closed;
    unlock_endpoint(state);
    if (closed || !owner) {
      return (struct syscall_result){closed ? CALL_ENDPOINT_CLOSED : CALL_DENIED, 0};
    }
    return reply_endpoint(record, handle, request_address, request_size);
  }
  struct endpoint *endpoint = (struct endpoint *)object;
  if (object->type == OBJECT_ENDPOINT && operation == ENDPOINT_SEND) {
    return send_endpoint(endpoint, request_address, request_size);
  }
  if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    if (endpoint->owner != process_current()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  }
  if (reply_capacity < sizeof(struct endpoint_packet)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct endpoint_packet), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return object->type == OBJECT_ENDPOINT ?
      call_endpoint(endpoint, request_address, request_size, reply_address) :
      receive_endpoint(endpoint, reply_address);
}
