#include <abi/endpoint.h>
#include <arch/cpu.h>
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
  uint64_t object_id, protocol, operation, rights, reason;
  struct endpoint_grant grants[ENDPOINT_GRANTS_MAX];
};
_Static_assert(sizeof(struct packet_header) == ENDPOINT_PACKET_HEADER_SIZE,
               "endpoint packet prefix");

struct endpoint_export {
  struct kernel_object client;
  struct kernel_object storage;
  struct endpoint_state *endpoint;
  struct endpoint_export *next;
  uint64_t object_id, protocol, rights, transport;
  size_t in_flight;
  bool client_live, linked, withdrawn, retire_pending, retire_delivered;
};

struct endpoint_delivery_record {
  struct kernel_object receipt;
  struct endpoint_state *endpoint;
  struct endpoint_export *target;
  uint64_t operation, rights;
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
  uint64_t request_transport[ENDPOINT_GRANTS_MAX], reply_transport[ENDPOINT_GRANTS_MAX];
  uint8_t request[ENDPOINT_DATA_MAX], reply[ENDPOINT_DATA_MAX];
};

struct endpoint_state {
  struct kernel_object storage;
  atomic_bool locked;
  struct endpoint caller, receiver;
  struct endpoint_delivery_record deliveries[ENDPOINT_DELIVERIES_MAX];
  struct endpoint_delivery_record *head, *tail;
  struct endpoint_export *exports;
  size_t export_count;
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

static void retire_export(struct endpoint_export *export)
{
  if (export->linked && !export->endpoint->closed && !export->in_flight &&
      (export->withdrawn || !export->client_live) &&
      !export->retire_pending && !export->retire_delivered) {
    export->retire_pending = true;
    wake_waiter(&export->endpoint->waiting_receiver);
  }
}

static void destroy_endpoint_storage(struct kernel_object *object)
{
  struct endpoint_state *state = (struct endpoint_state *)object;
  KASSERT(!state->storage_references);
  kfree(state);
}

static void destroy_export_storage(struct kernel_object *object)
{
  struct endpoint_export *export = (void *)((uint8_t *)object -
      offsetof(struct endpoint_export, storage));
  struct endpoint_state *state = export->endpoint;
  lock_endpoint(state);
  KASSERT(!export->linked && !export->client_live && !export->in_flight);
  bool destroy = --state->storage_references == 0;
  unlock_endpoint(state);
  kfree(export);
  if (destroy) {
    object_release(&state->storage);
  }
}

/* Client references include captured attachments and admitted work, but never
 * provider control. The list and client each own a separate storage reference. */
static void destroy_export_client(struct kernel_object *object)
{
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  KASSERT(export->client_live && !export->in_flight);
  export->client_live = false;
  retire_export(export);
  unlock_endpoint(export->endpoint);
  object_release(&export->storage);
}

bool endpoint_export_authority_valid(const struct kernel_object *object,
    uint64_t rights, uint64_t transport)
{
  const struct endpoint_export *export = (const struct endpoint_export *)object;
  return !(rights & ~export->rights) && !(transport & ~export->transport);
}

bool endpoint_export_available(struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_ENDPOINT_EXPORT);
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  bool available = !export->withdrawn && !export->endpoint->closed;
  unlock_endpoint(export->endpoint);
  return available;
}

uint64_t endpoint_export_protocol(const struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_ENDPOINT_EXPORT);
  return ((const struct endpoint_export *)object)->protocol;
}

static void free_delivery(struct endpoint_delivery_record *record)
{
  if (!record->caller_active && !record->receipt_live) {
    KASSERT(record->state == DELIVERY_COMPLETE);
    KASSERT(!record->cancel_pending);
    record->state = DELIVERY_FREE;
    if (record->target) {
      struct endpoint_export *export = record->target;
      record->target = NULL;
      KASSERT(export->in_flight);
      --export->in_flight;
      retire_export(export);
      object_release(&export->client);
    }
  }
}

static void release_queued_receipt(struct endpoint_delivery_record *record);

static bool deadline_expired(uint64_t deadline_ns)
{
  return deadline_ns && task_deadline_expired(deadline_ns);
}

/* The endpoint lock serializes expiry with reply, receipt close and owner exit. */
static void cancel_delivery(struct endpoint_state *state,
    struct endpoint_delivery_record *record, enum call_status reason)
{
  KASSERT(record->state == DELIVERY_QUEUED || record->state == DELIVERY_RECEIVED);
  bool queued = record->state == DELIVERY_QUEUED;
  if (queued) {
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
  } else {
    record->cancel_pending = true;
    wake_waiter(&state->waiting_receiver);
  }
  record->state = DELIVERY_COMPLETE;
  record->status = reason;
  wake_waiter(&record->wait);
  if (queued) {
    release_queued_receipt(record);
  }
}

static void expire_delivery(struct endpoint_state *state,
    struct endpoint_delivery_record *record)
{
  KASSERT(record->kind == ENDPOINT_MESSAGE_CALL && deadline_expired(record->deadline_ns));
  cancel_delivery(state, record, CALL_TIMED_OUT);
}

/* Called at zero references with the endpoint lock held. A receipt never enters
 * the retirement queue: its embedded link must not outlive this delivery. */
static bool release_receipt(struct endpoint_delivery_record *record)
{
  struct endpoint_state *state = record->endpoint;
  KASSERT(!atomic_load_explicit(&record->receipt.references, memory_order_relaxed));
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
  KASSERT(state->storage_references);
  return --state->storage_references == 0;
}

void endpoint_receipt_release(struct kernel_object *object)
{
  struct endpoint_delivery_record *record = (struct endpoint_delivery_record *)object;
  struct endpoint_state *state = record->endpoint;
  lock_endpoint(state);
  bool destroy = release_receipt(record);
  unlock_endpoint(state);
  if (destroy) {
    object_release(&state->storage);
  }
}

static void release_queued_receipt(struct endpoint_delivery_record *record)
{
  /* No handle was published. The caller/receiver still owns endpoint storage,
   * and dropping this initial reference must not reacquire the endpoint lock. */
  KASSERT(!record->delivered && !record->receipt_handle);
  size_t previous = atomic_fetch_sub_explicit(&record->receipt.references, 1,
      memory_order_acq_rel);
  KASSERT(previous == 1);
  bool destroy = release_receipt(record);
  KASSERT(!destroy);
}

static void close_endpoint(struct endpoint_state *state)
{
  /* Called with the lock; completed replies have already won the transition. */
  state->closed = true;
  struct endpoint_export *export = state->exports;
  state->exports = NULL;
  state->export_count = 0;
  while (export) {
    struct endpoint_export *next = export->next;
    export->next = NULL;
    export->linked = false;
    export->withdrawn = true;
    export->retire_pending = false;
    object_release(&export->storage);
    export = next;
  }
  state->head = state->tail = NULL;
  for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
    struct endpoint_delivery_record *record = &state->deliveries[i];
    if (record->state != DELIVERY_QUEUED && record->state != DELIVERY_RECEIVED) {
      continue;
    }
    bool queued = record->state == DELIVERY_QUEUED;
    if (queued) {
      release_grants(record->request_grants, record->request_count);
      record->request_count = 0;
    }
    record->next = NULL;
    record->state = DELIVERY_COMPLETE;
    record->status = deadline_expired(record->deadline_ns) ?
        CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    record->cancel_pending = false;
    wake_waiter(&record->wait);
    if (queued) {
      release_queued_receipt(record);
    }
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
    object_release(&state->storage);
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

/* BSP, IF=0, with exclusive ownership of the caller's capability table.
 * Reserves all delivery storage and installs both handles or neither. */
static enum call_status endpoint_create(struct process *owner,
    struct endpoint_create_reply *reply)
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
  object_init(&state->storage, OBJECT_ENDPOINT, destroy_endpoint_storage);
  atomic_init(&state->locked, false);
  state->storage_references = 2;
  state->caller.state = state->receiver.state = state;
  state->receiver.owner = owner;
  state->receiver.owner_next = owner->endpoints;
  owner->endpoints = &state->receiver;
  object_init(&state->caller.object, OBJECT_ENDPOINT, destroy_endpoint);
  object_init(&state->receiver.object, OBJECT_ENDPOINT_RECEIVER, destroy_endpoint);
  struct kernel_object *objects[] = {&state->receiver.object, &state->caller.object};
  uint64_t rights[] = {ENDPOINT_RECEIVER_RIGHT_CONTROL, 0};
  uint64_t transport[] = {HANDLE_TRANSPORT_RECEIVE, HANDLE_TRANSPORT_CALL};
  handle_t handles[2];
  enum capability_result result = capability_insert_batch(&owner->capabilities,
      objects, rights, transport, 2, handles);
  object_release(&state->caller.object);
  object_release(&state->receiver.object);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  *reply = (struct endpoint_create_reply){handles[0], handles[1]};
  return CALL_OK;
}

static struct endpoint_export *find_export(struct endpoint_state *state, uint64_t id)
{
  for (struct endpoint_export *export = state->exports; export; export = export->next) {
    if (export->object_id == id) {
      return export;
    }
  }
  return NULL;
}

static enum call_status endpoint_export_create(struct process *owner,
    const struct endpoint_export_message *request, struct endpoint_export_reply *reply)
{
  KASSERT(arch_cpu_index() == 0);
  if (!request->object_id || !request->protocol ||
      (request->transport & ~HANDLE_TRANSPORT_CALL)) {
    return CALL_BAD_REQUEST;
  }
  struct kernel_object *object;
  enum capability_result result = capability_resolve(&owner->capabilities,
      request->receiver, ENDPOINT_RECEIVER_RIGHT_CONTROL, 0, &object, NULL, NULL);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  if (object->type != OBJECT_ENDPOINT_RECEIVER) {
    return CALL_WRONG_TYPE;
  }
  struct endpoint *receiver = (struct endpoint *)object;
  if (receiver->owner != owner) {
    return CALL_DENIED;
  }
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED :
      find_export(state, request->object_id) ? CALL_ALREADY_EXISTS :
      state->export_count == ENDPOINT_EXPORTS_MAX ? CALL_LIMIT : CALL_OK;
  unlock_endpoint(state);
  if (status != CALL_OK) {
    return status;
  }
  while (!capability_free_slots(&owner->capabilities)) {
    result = capability_grow(&owner->capabilities);
    if (result != CAP_OK) {
      return grant_status(result);
    }
  }
  struct endpoint_export *export = kmalloc(sizeof(*export));
  if (!export) {
    return CALL_NO_MEMORY;
  }
  memset(export, 0, sizeof(*export));
  export->endpoint = state;
  export->object_id = request->object_id;
  export->protocol = request->protocol;
  export->rights = request->rights;
  export->transport = request->transport;
  export->client_live = true;
  object_init(&export->client, OBJECT_ENDPOINT_EXPORT, destroy_export_client);
  object_init(&export->storage, OBJECT_ENDPOINT_EXPORT, destroy_export_storage);
  KASSERT(object_retain(&export->storage));
  lock_endpoint(state);
  ++state->storage_references;
  unlock_endpoint(state);
  result = capability_insert(&owner->capabilities, &export->client,
      request->rights, request->transport, &reply->client);
  if (result != CAP_OK) {
    object_release(&export->client);
    object_release(&export->storage);
    return grant_status(result);
  }
  /* Creation is BSP-owned while this process lends its table; the owned
   * receiver cannot close or change during the allocation/installation. */
  lock_endpoint(state);
  KASSERT(!state->closed && !find_export(state, request->object_id));
  export->next = state->exports;
  state->exports = export;
  ++state->export_count;
  export->linked = true;
  unlock_endpoint(state);
  object_release(&export->client);
  return CALL_OK;
}

void endpoint_create_execute(struct endpoint_create_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING && request->loan);
  request->result = endpoint_create(request->loan, &request->reply);
  request->loan = NULL;
}

void endpoint_export_execute(struct endpoint_export_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING && request->loan);
  request->result = endpoint_export_create(request->loan, &request->input, &request->reply);
  request->loan = NULL;
}

static enum call_status create_endpoint(struct endpoint_create_reply *reply)
{
  struct process *process = process_current();
  KASSERT(process);
  struct endpoint_create_request *request =
      (struct endpoint_create_request *)bsp_request_prepare(BSP_SERVICE_ENDPOINT_CREATE);
  request->loan = process;
  request->reply = (struct endpoint_create_reply){0};
  request->result = CALL_NO_MEMORY;

  bsp_request_submit_and_wait(&request->request);
  enum call_status result = request->result;
  if (result == CALL_OK) {
    *reply = request->reply;
  }
  bsp_request_release(&request->request);
  return result;
}

static enum call_status export_endpoint(const struct endpoint_export_message *input,
    struct endpoint_export_reply *reply)
{
  struct process *process = process_current();
  KASSERT(process);
  struct endpoint_export_request *request =
      (struct endpoint_export_request *)bsp_request_prepare(BSP_SERVICE_ENDPOINT_EXPORT);
  request->loan = process;
  request->input = *input;
  request->reply = (struct endpoint_export_reply){0};
  request->result = CALL_NO_MEMORY;

  bsp_request_submit_and_wait(&request->request);
  enum call_status result = request->result;
  if (result == CALL_OK) {
    *reply = request->reply;
  }
  bsp_request_release(&request->request);
  return result;
}

static struct syscall_result control_export(struct endpoint *receiver,
    uint64_t operation, uintptr_t address, size_t size)
{
  uint64_t id;
  if (size != sizeof(id)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&id, address, sizeof(id))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  struct endpoint_export *export = find_export(state, id);
  if (state->closed || !export) {
    enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED : CALL_NOT_FOUND;
    unlock_endpoint(state);
    return (struct syscall_result){status, 0};
  }
  if (operation == ENDPOINT_WITHDRAW) {
    export->withdrawn = true;
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      struct endpoint_delivery_record *record = &state->deliveries[i];
      if (record->target != export ||
          (record->state != DELIVERY_QUEUED && record->state != DELIVERY_RECEIVED)) {
        continue;
      }
      enum call_status reason = deadline_expired(record->deadline_ns) ?
          CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
      cancel_delivery(state, record, reason);
    }
    retire_export(export);
    unlock_endpoint(state);
    return (struct syscall_result){CALL_OK, 0};
  }
  KASSERT(operation == ENDPOINT_RETIRE_ACK);
  if (!export->retire_delivered || export->in_flight) {
    unlock_endpoint(state);
    return (struct syscall_result){CALL_BUSY, 0};
  }
  struct endpoint_export **link = &state->exports;
  while (*link != export) {
    link = &(*link)->next;
  }
  *link = export->next;
  export->next = NULL;
  export->linked = false;
  export->withdrawn = true;
  --state->export_count;
  unlock_endpoint(state);
  object_release(&export->storage);
  return (struct syscall_result){CALL_OK, 0};
}

static enum call_status reserve_handles(size_t count)
{
  while (capability_free_slots(&process_current()->capabilities) < count) {
    enum capability_result result = capability_request_growth();
    if (result != CAP_OK) {
      return grant_status(result);
    }
  }
  return CALL_OK;
}

static enum call_status capture_grants(const struct endpoint_message *message,
    struct kernel_object **objects, uint64_t *rights, uint64_t *transport)
{
  for (size_t i = 0; i < message->grant_count; ++i) {
    struct kernel_object *object;
    enum capability_result result = capability_resolve(&process_current()->capabilities,
        message->grants[i].handle, message->grants[i].rights,
        message->grants[i].transport, &object, NULL, NULL);
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
    transport[i] = message->grants[i].transport;
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
    if (message->grants[i].handle || message->grants[i].rights || message->grants[i].transport) {
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
    struct endpoint_export *target, uint64_t resource_rights,
    const struct endpoint_message *message, struct task_wait *wait,
    struct endpoint_delivery_record **result)
{
  KASSERT((wait != NULL) == (result != NULL));
  struct kernel_object *grants[ENDPOINT_GRANTS_MAX];
  uint64_t rights[ENDPOINT_GRANTS_MAX], transport[ENDPOINT_GRANTS_MAX];
  enum call_status status = capture_grants(message, grants, rights, transport);
  if (status != CALL_OK) {
    return status;
  }
  struct endpoint_state *state = endpoint->state;
  struct endpoint_delivery_record *record = NULL;
  lock_endpoint(state);
  if (wait && deadline_expired(message->deadline_ns)) {
    status = CALL_TIMED_OUT;
  } else if (state->closed || (target && target->withdrawn)) {
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
  if (expired || state->closed || (target && target->withdrawn)) {
    record->state = DELIVERY_FREE;
    status = expired ? CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    unlock_endpoint(state);
    release_grants(grants, message->grant_count);
    return status;
  }
  if (target && !object_retain(&target->client)) {
    record->state = DELIVERY_FREE;
    unlock_endpoint(state);
    release_grants(grants, message->grant_count);
    return CALL_LIMIT;
  }
  record->target = target;
  if (target) {
    ++target->in_flight;
  }
  record->operation = message->operation;
  record->rights = resource_rights;
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
    record->request_transport[i] = transport[i];
  }
  object_init(&record->receipt, OBJECT_ENDPOINT_RECEIPT, NULL);
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
    struct endpoint_export *target, uint64_t resource_rights,
    uintptr_t request_address, size_t request_size)
{
  struct endpoint_message message;
  enum call_status status = read_message(request_address, request_size, &message);
  if (status == CALL_OK && (target ? message.protocol != target->protocol :
      (message.protocol || message.operation))) {
    status = CALL_BAD_OPERATION;
  }
  if (status == CALL_OK && (message.result || message.deadline_ns)) {
    status = CALL_BAD_REQUEST;
  }
  if (status == CALL_OK) {
    status = admit_message(endpoint, target, resource_rights, &message, NULL, NULL);
  }
  return (struct syscall_result){status, 0};
}

static struct syscall_result call_endpoint(struct endpoint *endpoint,
    struct endpoint_export *target, uint64_t resource_rights,
    uintptr_t request_address, size_t request_size, uintptr_t reply_address)
{
  struct packet_header output = {.kind = ENDPOINT_MESSAGE_CALL,
    .object_id = target ? target->object_id : 0,
    .protocol = target ? target->protocol : 0, .rights = resource_rights};
  struct endpoint_message message;
  enum call_status status = read_message(request_address, request_size, &message);
  if (status == CALL_OK) {
    output.deadline_ns = message.deadline_ns;
    output.operation = message.operation;
    if (target ? message.protocol != target->protocol : (message.protocol || message.operation)) {
      status = CALL_BAD_OPERATION;
    }
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
  status = admit_message(endpoint, target, resource_rights, &message, wait, &record);
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
        record->reply_grants, record->reply_rights, record->reply_transport, record->reply_count, handles);
    KASSERT(result != CAP_FULL);
    status = grant_status(result);
    if (status == CALL_OK) {
      output.result = record->result;
      output.size = record->reply_size;
      output.grant_count = record->reply_count;
      for (size_t i = 0; i < record->reply_count; ++i) {
        output.grants[i] = (struct endpoint_grant){handles[i], record->reply_rights[i], record->reply_transport[i]};
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
        .deadline_ns = cancel->deadline_ns,
        .object_id = cancel->target ? cancel->target->object_id : 0,
        .protocol = cancel->target ? cancel->target->protocol : 0,
        .operation = cancel->operation, .rights = cancel->rights, .reason = cancel->status};
      cancel->cancel_pending = false;
      unlock_endpoint(state);
      return write_packet(reply_address, &output, NULL, CALL_OK);
    }
    for (struct endpoint_export *export = state->exports; export; export = export->next) {
      if (export->retire_pending) {
        struct packet_header output = {.kind = ENDPOINT_MESSAGE_RETIRE,
          .object_id = export->object_id, .protocol = export->protocol};
        export->retire_pending = false;
        export->retire_delivered = true;
        unlock_endpoint(state);
        return write_packet(reply_address, &output, NULL, CALL_OK);
      }
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
    uint64_t rights[ENDPOINT_GRANTS_MAX + 1], transport[ENDPOINT_GRANTS_MAX + 1];
    handle_t handles[ENDPOINT_GRANTS_MAX + 1];
    objects[0] = &record->receipt;
    rights[0] = record->kind == ENDPOINT_MESSAGE_CALL ? ENDPOINT_RIGHT_REPLY : 0;
    transport[0] = 0;
    for (size_t i = 0; i < count; ++i) {
      objects[i + 1] = record->request_grants[i];
      rights[i + 1] = record->request_rights[i];
      transport[i + 1] = record->request_transport[i];
    }
    enum capability_result result = capability_insert_batch(&process_current()->capabilities,
        objects, rights, transport, count + 1, handles);
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
      .deadline_ns = record->deadline_ns,
      .object_id = record->target ? record->target->object_id : 0,
      .protocol = record->target ? record->target->protocol : 0,
      .operation = record->operation, .rights = record->rights};
    for (size_t i = 0; i < count; ++i) {
      output.grants[i] = (struct endpoint_grant){handles[i + 1], rights[i + 1], transport[i + 1]};
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
  if (message.deadline_ns || message.protocol || message.operation) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct kernel_object *grants[ENDPOINT_GRANTS_MAX];
  uint64_t rights[ENDPOINT_GRANTS_MAX], transport[ENDPOINT_GRANTS_MAX];
  status = capture_grants(&message, grants, rights, transport);
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
    record->reply_transport[i] = transport[i];
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
    uintptr_t request_address, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  if (!(rights & ENDPOINT_SERVICE_RIGHT_CREATE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation == ENDPOINT_EXPORT) {
    struct endpoint_export_message request = {0};
    if (request_size != sizeof(request) - sizeof(request.header) ||
        reply_capacity < sizeof(struct endpoint_export_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request.receiver, request_address, request_size) ||
        !user_buffer_check(reply_address, sizeof(struct endpoint_export_reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    struct endpoint_export_reply reply;
    enum call_status status = export_endpoint(&request, &reply);
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
    KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
    return (struct syscall_result){CALL_OK, sizeof(reply)};
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
  enum call_status status = create_endpoint(&reply);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  KASSERT(copy_to_user(reply_address, &reply, sizeof(reply)));
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

struct syscall_result endpoint_call(struct kernel_object *object, handle_t handle,
    uint64_t rights, uint64_t transport, uint64_t operation, uintptr_t request_address,
    size_t request_size, uintptr_t reply_address, size_t reply_capacity)
{
  if (object->type == OBJECT_ENDPOINT_RECEIPT) {
    if (operation != ENDPOINT_REPLY) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    if (!(rights & ENDPOINT_RIGHT_REPLY)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
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
  struct endpoint_export *target = object->type == OBJECT_ENDPOINT_EXPORT ?
      (struct endpoint_export *)object : NULL;
  struct endpoint *endpoint = target ? &target->endpoint->caller : (struct endpoint *)object;
  if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    if (endpoint->owner != process_current()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (operation == ENDPOINT_WITHDRAW || operation == ENDPOINT_RETIRE_ACK) {
      if (!(rights & ENDPOINT_RECEIVER_RIGHT_CONTROL)) {
        return (struct syscall_result){CALL_DENIED, 0};
      }
      return control_export(endpoint, operation, request_address, request_size);
    }
    if (operation != ENDPOINT_RECEIVE) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    if (!(transport & HANDLE_TRANSPORT_RECEIVE)) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (request_size) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
  } else {
    KASSERT(object->type == OBJECT_ENDPOINT || target);
    if (operation != ENDPOINT_CALL && operation != ENDPOINT_SEND) {
      return (struct syscall_result){CALL_BAD_OPERATION, 0};
    }
    uint64_t required = operation == ENDPOINT_CALL ? HANDLE_TRANSPORT_CALL : HANDLE_TRANSPORT_SEND;
    if ((transport & required) != required) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (operation == ENDPOINT_SEND) {
      return send_endpoint(endpoint, target, rights, request_address, request_size);
    }
  }
  if (reply_capacity < sizeof(struct endpoint_packet)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct endpoint_packet), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return object->type == OBJECT_ENDPOINT_RECEIVER ?
      receive_endpoint(endpoint, reply_address) :
      call_endpoint(endpoint, target, rights, request_address, request_size, reply_address);
}
