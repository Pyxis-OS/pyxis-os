#include <abi/endpoint.h>
#include <abi/file.h>
#include <abi/stream.h>
#include <abi/provider.h>
#include <abi/http_prepare.h>
#include <abi/transaction.h>
#include <abi/wait.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/completion.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/describe.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/launcher.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include <kernel/user/readiness_source.h>
#include <kernel/user_memory.h>

enum delivery_state { DELIVERY_FREE, DELIVERY_FILLING, DELIVERY_QUEUED,
                      DELIVERY_RECEIVED, DELIVERY_COMPLETE };

/* Exactly the public prefix, without putting a page-sized packet on a stack. */
struct packet_header {
  handle_t receipt;
  uint64_t delivery, kind, result, size, grant_count, deadline_ns;
  uint64_t object_id, protocol, operation, rights, reason;
  uint64_t correlation_id, completion_flags, progress;
  struct endpoint_grant grants[ENDPOINT_GRANTS_MAX];
};
_Static_assert(sizeof(struct packet_header) == ENDPOINT_PACKET_HEADER_SIZE,
               "endpoint packet prefix");

struct endpoint_description {
  struct kernel_object object;
  struct resource_description value;
  struct file_object *body;
  struct file_object *read_file;
  struct endpoint_description *released_next;
};

struct endpoint_export {
  struct kernel_object client;
  struct kernel_object storage;
  struct endpoint_state *endpoint;
  struct endpoint_export *next;
  struct endpoint_description *description;
  uint64_t object_id, protocol, rights, transport;
  size_t in_flight, clients, caretakers;
  uint64_t read_bytes, reserved_read_bytes;
  bool has_read_file;
  bool client_live, linked, withdrawn, retire_pending, retire_delivered;
};

struct endpoint_delivery_record {
  struct kernel_object receipt;
  struct endpoint_state *endpoint;
  struct endpoint_export *target;
  struct native_completion *completion;
  struct forwarder_operation origin;
  bool completion_committed, completion_pending;
  uint64_t operation, rights;
  struct endpoint_delivery_record *next;
  struct task_wait *wait;
  enum delivery_state state;
  enum endpoint_message_kind kind;
  enum call_status status;
  bool caller_active, receipt_live, delivered, cancel_pending, replying;
  handle_t receipt_handle;
  uint64_t deadline_ns;
  uint64_t request_size, reply_size, result;
  size_t request_count, reply_count;
  struct capability_grant request_grants[ENDPOINT_GRANTS_MAX];
  struct capability_grant reply_grants[ENDPOINT_GRANTS_MAX];
  struct capability_grant released_grants[ENDPOINT_GRANTS_MAX * 2];
  size_t released_count;
  struct kernel_object *released_storage;
  struct capability_reservation reply_reservation;
  struct capability_reserved_slot reply_slots[ENDPOINT_GRANTS_MAX];
  uint8_t request[ENDPOINT_DATA_MAX], reply[ENDPOINT_DATA_MAX];
};

struct endpoint_state {
  struct kernel_object storage;
  struct execution_group *execution_group; /* Immutable receiver policy, owned storage. */
  atomic_bool locked;
  struct endpoint caller, receiver;
  struct endpoint_delivery_record deliveries[ENDPOINT_DELIVERIES_MAX];
  struct endpoint_delivery_record *head, *tail;
  struct endpoint_export *exports;
  struct endpoint_description *released_descriptions;
  size_t export_count;
  struct task_wait *waiting_receiver;
  size_t storage_references;
  size_t readiness_waiters;
  struct readiness_source readiness;
  size_t released_count;
  uint64_t last_completion_id;
  bool closed, receiving;
};

/* IF=0. Endpoint -> scheduler/retirement locks, never the reverse. No
 * allocation or parking while held. RECEIVE's bounded user copy holds a short
 * mapping lease through validation and publication. All published data lives
 * in persistent heap mappings, not another CPU's private kernel stack. */
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

static uint64_t receiver_events(struct endpoint_state *state)
{
  if (state->closed) {
    return WAIT_CLOSED;
  }
  if (state->head) {
    return WAIT_READABLE;
  }
  for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
    if (state->deliveries[i].cancel_pending) {
      return WAIT_READABLE;
    }
  }
  for (struct endpoint_export *export = state->exports; export; export = export->next) {
    if (export->retire_pending) {
      return WAIT_READABLE;
    }
  }
  return 0;
}

static uint64_t begin_endpoint_update(struct endpoint_state *state)
{
  lock_endpoint(state);
  return state->readiness_waiters ||
      atomic_load_explicit(&state->readiness.watchers, memory_order_acquire) ?
      receiver_events(state) : 0;
}

static void drain_released_grants(struct endpoint_state *state);

static void defer_grants(struct endpoint_delivery_record *record,
    struct capability_grant *grants, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    if (grants[i].object) {
      KASSERT(record->released_count < ENDPOINT_GRANTS_MAX * 2);
      record->released_grants[record->released_count++] = grants[i];
      ++record->endpoint->released_count;
      grants[i] = (struct capability_grant){0};
    }
  }
}

static void end_endpoint_update(struct endpoint_state *state, uint64_t before)
{
  bool persistent = atomic_load_explicit(&state->readiness.watchers, memory_order_acquire);
  bool changed = (state->readiness_waiters || persistent) && (receiver_events(state) & ~before);
  bool notify = changed && state->readiness_waiters;
  bool notify_source = changed && persistent;
  bool release = state->released_count != 0;
  struct endpoint_description *descriptions = state->released_descriptions;
  state->released_descriptions = NULL;
  unlock_endpoint(state);
  if (notify_source) {
    readiness_source_notify(&state->readiness);
  }
  if (notify) {
    readiness_notify();
  }
  while (descriptions) {
    struct endpoint_description *next = descriptions->released_next;
    object_release(&descriptions->object);
    descriptions = next;
  }
  if (release) {
    drain_released_grants(state);
  }
}

static void defer_description(struct endpoint_export *export)
{
  struct endpoint_description *description = export->description;
  if (description) {
    export->description = NULL;
    description->released_next = export->endpoint->released_descriptions;
    export->endpoint->released_descriptions = description;
  }
}

static void destroy_description(struct kernel_object *object)
{
  struct endpoint_description *description = (struct endpoint_description *)object;
  if (description->body) {
    object_release(&description->body->object);
  }
  if (description->read_file) {
    object_release(&description->read_file->object);
  }
  kfree(description);
}

void endpoint_readiness_register(struct endpoint *receiver)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  ++state->readiness_waiters;
  unlock_endpoint(state);
}

void endpoint_readiness_unregister(struct endpoint *receiver)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  KASSERT(state->readiness_waiters);
  --state->readiness_waiters;
  unlock_endpoint(state);
}

struct readiness_source *endpoint_readiness_source(struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_ENDPOINT_RECEIVER);
  return &((struct endpoint *)object)->state->readiness;
}

uint64_t endpoint_receiver_ready(struct endpoint *receiver, uint64_t events)
{
  uint64_t flags = cpu_save_interrupts();
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  uint64_t ready = receiver_events(state) & (events | WAIT_CLOSED);
  unlock_endpoint(state);
  cpu_restore_interrupts(flags);
  return ready;
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

static void release_grants(struct capability_grant *grants, size_t count)
{
  for (size_t i = 0; i < count; ++i) {
    capability_grant_release(&grants[i]);
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

static void wake_delivery_waiter(struct endpoint_delivery_record *record)
{
  if (record->wait && record->origin.count) {
    forwarder_operation_unwait(&record->origin, record->wait);
  }
  wake_waiter(&record->wait);
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
  if (state->execution_group) {
    object_release(&state->execution_group->object);
  }
  memzero_explicit(state, sizeof(*state));
  kfree(state);
}

static void destroy_export_storage(struct kernel_object *object)
{
  struct endpoint_export *export = (void *)((uint8_t *)object -
      offsetof(struct endpoint_export, storage));
  struct endpoint_state *state = export->endpoint;
  lock_endpoint(state);
  KASSERT(!export->linked && !export->client_live && !export->in_flight && !export->description);
  bool destroy = --state->storage_references == 0;
  unlock_endpoint(state);
  kfree(export);
  if (destroy) {
    object_release(&state->storage);
  }
}

/* Ordinary client references include captured attachments and admitted work.
 * Private forwarder caretaker references alone do not delay provider retirement.
 * The registry and client each own a separate backing-storage reference. */
static void destroy_export_client(struct kernel_object *object)
{
  struct endpoint_export *export = (struct endpoint_export *)object;
  uint64_t ready_before = begin_endpoint_update(export->endpoint);
  KASSERT(!export->clients && !export->caretakers && !export->in_flight);
  export->client_live = false;
  retire_export(export);
  end_endpoint_update(export->endpoint, ready_before);
  object_release(&export->storage);
}

bool endpoint_export_grant_retain(struct kernel_object *object)
{
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  bool retained = export->client_live && export->clients != SIZE_MAX;
  if (retained) {
    ++export->clients;
  }
  unlock_endpoint(export->endpoint);
  return retained;
}

void endpoint_export_grant_release(struct kernel_object *object)
{
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  KASSERT(export->clients);
  --export->clients;
  unlock_endpoint(export->endpoint);
}

bool endpoint_export_caretaker_retain(struct kernel_object *object)
{
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  bool retained = object_retain(object);
  if (retained) {
    ++export->caretakers;
  }
  unlock_endpoint(export->endpoint);
  return retained;
}

void endpoint_export_caretaker_release(struct kernel_object *object)
{
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  KASSERT(export->caretakers);
  --export->caretakers;
  unlock_endpoint(export->endpoint);
  object_release(object);
}

bool endpoint_export_storage_release(struct kernel_object *object)
{
  struct endpoint_export *export = (struct endpoint_export *)object;
  uint64_t ready_before = begin_endpoint_update(export->endpoint);
  size_t previous = atomic_fetch_sub_explicit(&object->references, 1, memory_order_acq_rel);
  KASSERT(previous);
  size_t remaining = previous - 1;
  KASSERT(remaining >= export->caretakers);
  if (!export->clients && remaining == export->caretakers) {
    export->client_live = false;
    retire_export(export);
  }
  end_endpoint_update(export->endpoint, ready_before);
  return !remaining;
}

bool endpoint_export_authority_valid(const struct kernel_object *object,
    uint64_t rights, uint64_t transport)
{
  const struct endpoint_export *export = (const struct endpoint_export *)object;
  return !(rights & ~export->rights) && !(transport & ~export->transport);
}

bool endpoint_export_available(struct kernel_object *object)
{
  if (object->type == OBJECT_FORWARDER) {
    return forwarder_available(object);
  }
  KASSERT(object->type == OBJECT_ENDPOINT_EXPORT);
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  bool available = export->client_live && !export->withdrawn && !export->endpoint->closed;
  unlock_endpoint(export->endpoint);
  return available;
}

uint64_t endpoint_export_protocol(const struct kernel_object *object)
{
  KASSERT(object->type == OBJECT_ENDPOINT_EXPORT);
  return ((const struct endpoint_export *)object)->protocol;
}

enum call_status endpoint_description_capture(struct kernel_object *object,
    struct resource_description *description, struct file_object **body)
{
  KASSERT(object->type == OBJECT_ENDPOINT_EXPORT);
  *description = (struct resource_description){.outcome = RESOURCE_OK};
  if (body) {
    *body = NULL;
  }
  struct endpoint_export *export = (struct endpoint_export *)object;
  lock_endpoint(export->endpoint);
  enum call_status status = CALL_OK;
  if (export->endpoint->closed || export->withdrawn || !export->client_live) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (export->description) {
    struct endpoint_description *snapshot = export->description;
    if (body && snapshot->body && !object_retain(&snapshot->body->object)) {
      status = CALL_LIMIT;
    } else {
      *description = snapshot->value;
      if (body) {
        *body = snapshot->body;
      }
    }
  }
  unlock_endpoint(export->endpoint);
  return status;
}

static void release_delivery_slot(struct endpoint_delivery_record *record)
{
  memzero_explicit(record->request, sizeof(record->request));
  memzero_explicit(record->reply, sizeof(record->reply));
  record->request_size = record->reply_size = 0;
  record->state = DELIVERY_FREE;
}

static void free_delivery(struct endpoint_delivery_record *record)
{
  if (!record->caller_active && !record->receipt_live && !record->released_count) {
    KASSERT(record->state == DELIVERY_COMPLETE);
    KASSERT(!record->cancel_pending);
    KASSERT(!record->reply_reservation.table && !record->reply_count);
    if (record->released_storage) {
      return;
    }
    forwarder_operation_release(&record->origin);
    if (record->completion) {
      struct native_completion *operation = record->completion;
      KASSERT(record->completion_committed && !record->completion_pending);
      record->completion = NULL;
      native_completion_ack(operation);
    }
    if (record->target) {
      struct endpoint_export *export = record->target;
      record->target = NULL;
      KASSERT(export->in_flight);
      --export->in_flight;
      retire_export(export);
      record->released_storage = &export->client;
      ++record->endpoint->released_count;
    } else {
      release_delivery_slot(record);
    }
  }
}

/* Detach under the structural guard; controller close and export retirement
 * can invoke another endpoint, including this same one, after unlocking. */
static void drain_released_grants(struct endpoint_state *state)
{
  for (;;) {
    struct capability_grant grants[ENDPOINT_GRANTS_MAX * 2];
    struct kernel_object *storage = NULL;
    size_t count = 0;
    lock_endpoint(state);
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      struct endpoint_delivery_record *record = &state->deliveries[i];
      if (!record->released_count && !record->released_storage) {
        continue;
      }
      count = record->released_count;
      for (size_t j = 0; j < count; ++j) {
        grants[j] = record->released_grants[j];
        record->released_grants[j] = (struct capability_grant){0};
      }
      state->released_count -= count;
      record->released_count = 0;
      if (!record->released_storage) {
        free_delivery(record);
      }
      if (record->released_storage) {
        storage = record->released_storage;
        record->released_storage = NULL;
        --state->released_count;
        free_delivery(record);
      }
      break;
    }
    unlock_endpoint(state);
    if (!count && !storage) {
      return;
    }
    release_grants(grants, count);
    if (storage) {
      object_release(storage);
    }
  }
}

static bool release_queued_receipt(struct endpoint_delivery_record *record);

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
    defer_grants(record, record->request_grants, record->request_count);
    record->request_count = 0;
  } else {
    record->cancel_pending = true;
    wake_waiter(&state->waiting_receiver);
  }
  record->state = DELIVERY_COMPLETE;
  record->status = reason;
  wake_delivery_waiter(record);
  if (queued) {
    bool destroy = release_queued_receipt(record);
    KASSERT(!destroy);
  }
}

void endpoint_forwarder_cancel(struct forwarder_operation *operation)
{
  struct endpoint_delivery_record *record = (void *)((uint8_t *)operation -
      offsetof(struct endpoint_delivery_record, origin));
  struct endpoint_state *state = record->endpoint;
  uint64_t ready_before = begin_endpoint_update(state);
  if (record->state == DELIVERY_QUEUED && forwarder_operation_denied(operation)) {
    cancel_delivery(state, record, deadline_expired(record->deadline_ns) ?
        CALL_TIMED_OUT : CALL_DENIED);
  }
  end_endpoint_update(state, ready_before);
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
    if (record->kind != ENDPOINT_MESSAGE_COMPLETION) {
      record->status = record->kind == ENDPOINT_MESSAGE_CALL ?
          (deadline_expired(record->deadline_ns) ? CALL_TIMED_OUT : CALL_ABANDONED) : CALL_OK;
    }
    wake_delivery_waiter(record);
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
  uint64_t ready_before = begin_endpoint_update(state);
  bool destroy = release_receipt(record);
  end_endpoint_update(state, ready_before);
  if (destroy) {
    object_release(&state->storage);
  }
}

static bool release_queued_receipt(struct endpoint_delivery_record *record)
{
  /* No handle was published. Dropping this initial reference must not reacquire
   * the endpoint lock. Detached completion drain may release the last owner. */
  KASSERT(!record->delivered && !record->receipt_handle);
  size_t previous = atomic_fetch_sub_explicit(&record->receipt.references, 1,
      memory_order_acq_rel);
  KASSERT(previous);
  return previous == 1 ? release_receipt(record) : false;
}

enum call_status endpoint_completion_reserve(struct endpoint *receiver,
    struct native_completion *operation, struct capability_reservation *reservation,
    struct capability_reserved_slot *slots)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!operation->delivery && reservation->table && reservation->count >= 1 &&
      reservation->count <= 2);
  enum call_status status = native_completion_charge(operation,
      sizeof(struct endpoint_delivery_record));
  if (status != CALL_OK) {
    return status;
  }
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  struct endpoint_delivery_record *record = NULL;
  if (state->closed) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (state->last_completion_id == UINT64_MAX) {
    status = CALL_LIMIT;
  } else if (!operation->correlation_id || operation->correlation_id <= state->last_completion_id) {
    status = CALL_BAD_REQUEST;
  } else {
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      if (state->deliveries[i].state == DELIVERY_FREE) {
        record = &state->deliveries[i];
        break;
      }
    }
    if (!record) {
      status = CALL_QUEUE_FULL;
    }
  }
  if (record) {
    forwarder_operation_init(&record->origin);
    record->endpoint = state;
    record->target = NULL;
    record->completion = operation;
    record->completion_committed = false;
    record->completion_pending = true;
    record->operation = operation->operation;
    record->rights = 0;
    record->next = NULL;
    record->wait = NULL;
    record->state = DELIVERY_FILLING;
    record->kind = ENDPOINT_MESSAGE_COMPLETION;
    record->status = CALL_OK;
    record->caller_active = false;
    record->receipt_live = true;
    record->delivered = record->cancel_pending = record->replying = false;
    record->receipt_handle = HANDLE_INVALID;
    record->deadline_ns = operation->deadline_ns;
    record->request_size = record->reply_size = record->result = 0;
    record->request_count = record->reply_count = 0;
    record->reply_reservation = *reservation;
    memcpy(record->reply_slots, slots, reservation->count * sizeof(*slots));
    *reservation = (struct capability_reservation){0};
    object_init(&record->receipt, OBJECT_ENDPOINT_RECEIPT, NULL);
    ++state->storage_references;
    operation->delivery = record;
  }
  unlock_endpoint(state);
  return status;
}

enum call_status endpoint_completion_commit(struct native_completion *operation)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct endpoint_delivery_record *record = operation->delivery;
  struct endpoint_state *state = record->endpoint;
  lock_endpoint(state);
  KASSERT(record->state == DELIVERY_FILLING && record->completion == operation &&
      !record->completion_committed);
  enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED :
      deadline_expired(operation->deadline_ns) ? CALL_TIMED_OUT : CALL_OK;
  if (status == CALL_OK && !task_operation_start()) {
    status = CALL_DENIED;
  }
  if (status == CALL_OK) {
    KASSERT(operation->correlation_id > state->last_completion_id);
    state->last_completion_id = operation->correlation_id;
    record->completion_committed = true;
  }
  unlock_endpoint(state);
  return status;
}

void endpoint_completion_discard(struct native_completion *operation)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct endpoint_delivery_record *record = operation->delivery;
  struct endpoint_state *state = record->endpoint;
  lock_endpoint(state);
  KASSERT(record->state == DELIVERY_FILLING && !record->completion_committed &&
      record->completion == operation);
  capability_reservation_release(&record->reply_reservation, record->reply_slots);
  record->completion = NULL;
  record->completion_pending = false;
  operation->delivery = NULL;
  record->state = DELIVERY_COMPLETE;
  bool destroy = release_queued_receipt(record);
  unlock_endpoint(state);
  if (destroy) {
    object_release(&state->storage);
  }
}

void endpoint_completion_publish(struct native_completion *operation)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(!operation->backend && operation->output_size <= ENDPOINT_DATA_MAX);
  struct endpoint_delivery_record *record = operation->delivery;
  struct endpoint_state *state = record->endpoint;
  uint64_t ready_before = begin_endpoint_update(state);
  KASSERT(record->state == DELIVERY_FILLING && record->completion_committed &&
      record->completion_pending && record->completion == operation);
  record->completion_pending = false;
  record->status = operation->result;
  record->request_size = operation->output_size;
  memcpy(record->request, operation->output, operation->output_size);
  if (operation->returned.object) {
    record->request_grants[0] = operation->returned;
    operation->returned = (struct capability_grant){0};
    record->request_count = 1;
  }
  bool destroy = false;
  if (state->closed) {
    defer_grants(record, record->request_grants, record->request_count);
    record->request_count = 0;
    capability_reservation_release(&record->reply_reservation, record->reply_slots);
    record->state = DELIVERY_COMPLETE;
    destroy = release_queued_receipt(record);
  } else {
    record->state = DELIVERY_QUEUED;
    if (state->tail) {
      state->tail->next = record;
    } else {
      state->head = record;
    }
    state->tail = record;
    wake_waiter(&state->waiting_receiver);
  }
  end_endpoint_update(state, ready_before);
  if (destroy) {
    object_release(&state->storage);
  }
}

enum call_status endpoint_completion_cancel(struct endpoint *receiver, uint64_t id)
{
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED : CALL_NOT_FOUND;
  if (!state->closed) {
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      struct endpoint_delivery_record *record = &state->deliveries[i];
      if (!record->completion || !record->completion_committed ||
          record->completion->correlation_id != id) {
        continue;
      }
      if (record->completion_pending) {
        native_completion_request_cancel(record->completion, CALL_ABANDONED);
      }
      status = CALL_OK;
      break;
    }
  }
  unlock_endpoint(state);
  return status;
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
    defer_description(export);
    object_release(&export->storage);
    export = next;
  }
  state->head = state->tail = NULL;
  for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
    struct endpoint_delivery_record *record = &state->deliveries[i];
    if (record->state != DELIVERY_FREE && record->kind == ENDPOINT_MESSAGE_COMPLETION) {
      capability_reservation_release(&record->reply_reservation, record->reply_slots);
      if (record->completion_pending) {
        if (record->completion_committed) {
          native_completion_request_cancel(record->completion, CALL_ENDPOINT_CLOSED);
        }
      } else if (record->state == DELIVERY_QUEUED) {
        defer_grants(record, record->request_grants, record->request_count);
        record->request_count = 0;
        record->next = NULL;
        record->state = DELIVERY_COMPLETE;
        bool destroy = release_queued_receipt(record);
        KASSERT(!destroy);
      } else if (record->state == DELIVERY_RECEIVED) {
        record->state = DELIVERY_COMPLETE;
      }
      continue;
    }
    if (record->state != DELIVERY_QUEUED && record->state != DELIVERY_RECEIVED) {
      continue;
    }
    bool queued = record->state == DELIVERY_QUEUED;
    if (queued) {
      defer_grants(record, record->request_grants, record->request_count);
      record->request_count = 0;
    }
    record->next = NULL;
    record->state = DELIVERY_COMPLETE;
    record->status = deadline_expired(record->deadline_ns) ?
        CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    record->cancel_pending = false;
    wake_delivery_waiter(record);
    if (queued) {
      bool destroy = release_queued_receipt(record);
      KASSERT(!destroy);
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
    uint64_t ready_before = begin_endpoint_update(state);
    close_endpoint(state);
    end_endpoint_update(state, ready_before);
  } else if (object->type == OBJECT_ENDPOINT_RECEIPT) {
    struct endpoint_delivery_record *record = (struct endpoint_delivery_record *)object;
    struct endpoint_state *state = record->endpoint;
    uint64_t ready_before = begin_endpoint_update(state);
    if (record->state == DELIVERY_RECEIVED) {
      record->state = DELIVERY_COMPLETE;
      if (record->kind != ENDPOINT_MESSAGE_COMPLETION) {
        record->status = record->kind == ENDPOINT_MESSAGE_CALL ?
            (deadline_expired(record->deadline_ns) ? CALL_TIMED_OUT : CALL_ABANDONED) : CALL_OK;
      }
      wake_delivery_waiter(record);
    }
    record->cancel_pending = false;
    record->receipt_handle = HANDLE_INVALID;
    end_endpoint_update(state, ready_before);
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
  uint64_t ready_before = begin_endpoint_update(state);
  if (object->type == OBJECT_ENDPOINT_RECEIVER) {
    endpoint->owner = NULL;
    close_endpoint(state);
  }
  bool destroy = --state->storage_references == 0;
  end_endpoint_update(state, ready_before);
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
    uint64_t ready_before = begin_endpoint_update(state);
    endpoint->owner = NULL;
    endpoint->owner_next = NULL;
    close_endpoint(state);
    end_endpoint_update(state, ready_before);
    endpoint = next;
  }
}

/* BSP owns allocation and the receiver owner list. The caller reserved both
 * table slots before submitting this request. */
static enum call_status endpoint_create(struct endpoint_create_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  struct process *owner = request->owner;
  struct endpoint_state *state = kmalloc(sizeof(*state));
  if (!state) {
    return CALL_NO_MEMORY;
  }
  memset(state, 0, sizeof(*state));
  if (owner->execution_group && !object_retain(&owner->execution_group->object)) {
    kfree(state);
    return CALL_LIMIT;
  }
  state->execution_group = owner->execution_group;
  object_init(&state->storage, OBJECT_ENDPOINT, destroy_endpoint_storage);
  atomic_init(&state->locked, false);
  atomic_init(&state->readiness.watchers, 0);
  state->storage_references = 2;
  state->caller.state = state->receiver.state = state;
  state->receiver.owner = owner;
  state->receiver.owner_next = owner->endpoints;
  owner->endpoints = &state->receiver;
  object_init(&state->caller.object, OBJECT_ENDPOINT, destroy_endpoint);
  object_init(&state->receiver.object, OBJECT_ENDPOINT_RECEIVER, destroy_endpoint);
  struct capability_grant grants[2] = {0};
  enum capability_result result = capability_grant_retain(&state->receiver.object,
      ENDPOINT_RECEIVER_RIGHT_CONTROL, HANDLE_TRANSPORT_RECEIVE, &grants[0]);
  if (result == CAP_OK) {
    result = capability_grant_retain(&state->caller.object, 0,
        HANDLE_TRANSPORT_CALL, &grants[1]);
  }
  for (size_t i = 0; result == CAP_OK && i < 2; ++i) {
    result = forwarder_derive_grant(&grants[i], request->request.operation);
  }
  if (result == CAP_OK && !task_operation_start()) {
    result = CAP_DENIED;
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, grants, 2);
  }
  if (result == CAP_OK) {
    handle_t handles[2];
    capability_install_reserved(&request->reservation, request->slots, grants, 2, handles);
    request->reply = (struct endpoint_create_reply){handles[0], handles[1]};
  }
  release_grants(grants, 2);
  object_release(&state->caller.object);
  object_release(&state->receiver.object);
  return grant_status(result);
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


static bool description_valid(const struct endpoint_describe_publish *input)
{
  const struct resource_description *description = &input->description;
  if (!input->object_id || description->outcome >= RESOURCE_OUTCOME_COUNT ||
      (description->flags & ~RESOURCE_FLAGS) ||
      (!(description->flags & RESOURCE_LENGTH_KNOWN) && description->length) ||
      input->error_body_size > RESOURCE_ERROR_BODY_MAX_BYTES ||
      (!(description->flags & RESOURCE_ERROR_BODY) &&
      (input->error_body_address || input->error_body_size))) {
    return false;
  }
  return description_string_valid(description->detail, description->detail_size,
      sizeof(description->detail)) &&
      description_string_valid(description->media_type, description->media_type_size,
      sizeof(description->media_type));
}

static enum call_status description_publish(struct endpoint_describe_request *request)
{
  struct endpoint *receiver = (struct endpoint *)request->receiver;
  if (receiver->owner != request->owner) {
    return CALL_DENIED;
  }
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  struct endpoint_export *export = find_export(state, request->object_id);
  enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED :
      !export ? CALL_NOT_FOUND :
      export->withdrawn || !export->client_live ? CALL_ENDPOINT_CLOSED : CALL_OK;
  if (status == CALL_OK && request->read_file &&
      (export->protocol != PROTOCOL_FILE || export->rights != FILE_RIGHT_READ ||
      export->transport != HANDLE_TRANSPORT_CALL ||
      request->description.outcome != RESOURCE_OK ||
      request->description.flags != (RESOURCE_LENGTH_KNOWN | RESOURCE_REREADABLE | RESOURCE_COMPLETE) ||
      request->description.length != request->read_file->size)) {
    status = CALL_BAD_OPERATION;
  }
  unlock_endpoint(state);
  if (status != CALL_OK) {
    return status;
  }
  struct endpoint_description *snapshot = kmalloc(sizeof(*snapshot));
  if (!snapshot) {
    return CALL_NO_MEMORY;
  }
  *snapshot = (struct endpoint_description){.value = request->description};
  object_init(&snapshot->object, OBJECT_DESCRIPTION_STORAGE, destroy_description);
  if (request->read_file) {
    if (!object_retain(&request->read_file->object)) {
      object_release(&snapshot->object);
      return CALL_LIMIT;
    }
    snapshot->read_file = request->read_file;
  }
  if (request->description.flags & RESOURCE_ERROR_BODY) {
    snapshot->body = file_prepare_snapshot(request->body_size);
    if (!snapshot->body) {
      object_release(&snapshot->object);
      return CALL_NO_MEMORY;
    }
    file_fill_snapshot(snapshot->body, request->body_bytes);
  }
  lock_endpoint(state);
  export = find_export(state, request->object_id);
  status = state->closed ? CALL_ENDPOINT_CLOSED : !export ? CALL_NOT_FOUND :
      export->withdrawn || !export->client_live ? CALL_ENDPOINT_CLOSED :
      task_operation_start() ? CALL_OK : CALL_DENIED;
  struct endpoint_description *previous = NULL;
  if (status == CALL_OK) {
    if (snapshot->read_file) {
      /* The parked publisher lends exclusive FILE operation ownership.
       * Every fallible step precedes sealing and the atomic publication. */
      snapshot->read_file->immutable_snapshot = true;
      export->has_read_file = true;
    }
    previous = export->description;
    export->description = snapshot;
  }
  unlock_endpoint(state);
  if (status != CALL_OK) {
    object_release(&snapshot->object);
  } else if (previous) {
    object_release(&previous->object);
  }
  return status;
}

void endpoint_describe_execute(struct endpoint_describe_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING && request->owner);
  request->result = description_publish(request);
  object_release(request->receiver);
  request->receiver = NULL;
  request->owner = NULL;
}

static struct syscall_result publish_description(struct endpoint *receiver,
    uintptr_t address, size_t size)
{
  struct endpoint_describe_publish input = {0};
  if (size != sizeof(input) - sizeof(input.header)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&input.object_id, address, size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!description_valid(&input)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  void *bytes = NULL;
  if (input.error_body_size) {
    bytes = kmalloc(input.error_body_size);
    if (!bytes) {
      return (struct syscall_result){CALL_NO_MEMORY, 0};
    }
    if (!copy_from_user(bytes, input.error_body_address, input.error_body_size)) {
      kfree(bytes);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  }
  if (task_stop_requested()) {
    kfree(bytes);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct capability_reference backing = {0};
  struct file_object *file = NULL;
  if (input.read_file) {
    enum capability_result acquired = capability_acquire(&process_current()->capabilities,
        input.read_file, FILE_RIGHTS, 0, &backing);
    enum call_status status = grant_status(acquired);
    if (status == CALL_OK) {
      /* Never strip a source's forwarder lineage or retain provider graphs. */
      if (backing.object->type != OBJECT_FILE || backing.transport) {
        status = CALL_WRONG_TYPE;
      } else {
        file = (struct file_object *)backing.object;
        if (file->backing != FILE_RAM) {
          status = CALL_WRONG_TYPE;
        } else if (!file_begin_operation(file)) {
          status = CALL_ENDPOINT_CLOSED;
        } else {
          status = file_failure_status(file);
          if (status != CALL_OK) {
            file_end_operation(file);
          }
        }
      }
    }
    if (status != CALL_OK) {
      capability_release(&backing);
      kfree(bytes);
      return (struct syscall_result){status, 0};
    }
  }
  if (!object_retain(&receiver->object)) {
    if (file) {
      file_end_operation(file);
    }
    capability_release(&backing);
    kfree(bytes);
    return (struct syscall_result){CALL_LIMIT, 0};
  }
  struct endpoint_describe_request *request =
      (struct endpoint_describe_request *)bsp_request_prepare(BSP_SERVICE_ENDPOINT_DESCRIBE);
  request->owner = process_current();
  request->receiver = &receiver->object;
  request->object_id = input.object_id;
  request->description = input.description;
  request->body_bytes = bytes;
  request->body_size = input.error_body_size;
  request->read_file = file;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  if (file) {
    file_end_operation(file);
  }
  capability_release(&backing);
  kfree(request->body_bytes);
  request->body_bytes = NULL;
  bsp_request_release(&request->request);
  return (struct syscall_result){status, 0};
}

static enum call_status endpoint_export_create(struct endpoint_export_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  const struct endpoint_export_message *input = &request->input;
  struct endpoint *receiver = (struct endpoint *)request->receiver.object;
  if (receiver->owner != request->owner) {
    return CALL_DENIED;
  }
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED :
      find_export(state, input->object_id) ? CALL_ALREADY_EXISTS :
      state->export_count == ENDPOINT_EXPORTS_MAX ? CALL_LIMIT : CALL_OK;
  unlock_endpoint(state);
  if (status != CALL_OK) {
    return status;
  }
  struct endpoint_export *export = kmalloc(sizeof(*export));
  if (!export) {
    return CALL_NO_MEMORY;
  }
  memset(export, 0, sizeof(*export));
  export->endpoint = state;
  export->object_id = input->object_id;
  export->protocol = input->protocol;
  export->rights = input->rights;
  export->transport = input->transport;
  export->client_live = true;
  object_init(&export->client, OBJECT_ENDPOINT_EXPORT, destroy_export_client);
  object_init(&export->storage, OBJECT_ENDPOINT_EXPORT_STORAGE, destroy_export_storage);
  KASSERT(object_retain(&export->storage));
  lock_endpoint(state);
  ++state->storage_references;
  unlock_endpoint(state);
  struct capability_grant target = {0};
  struct capability_grant grants[2] = {0};
  enum capability_result result = capability_grant_retain(&export->client,
      input->rights, input->transport, &target);
  if (result == CAP_OK) {
    result = forwarder_derive_grant(&target, request->request.operation);
  }
  status = grant_status(result);
  if (status == CALL_OK) {
    status = forwarder_create_owned(&target, FORWARDER_REVOCABLE, &grants[0], &grants[1]);
  }
  if (status == CALL_OK) {
    status = grant_status(capability_validate_grants(request->reservation.table, grants, 2));
  }
  if (status == CALL_OK) {
    lock_endpoint(state);
    status = state->closed ? CALL_ENDPOINT_CLOSED :
        find_export(state, input->object_id) ? CALL_ALREADY_EXISTS :
        state->export_count == ENDPOINT_EXPORTS_MAX ? CALL_LIMIT :
        !task_operation_start() ? CALL_DENIED : CALL_OK;
    if (status == CALL_OK) {
      export->next = state->exports;
      state->exports = export;
      ++state->export_count;
      export->linked = true;
      handle_t handles[2];
      capability_install_reserved(&request->reservation, request->slots, grants, 2, handles);
      request->reply = (struct endpoint_export_reply){handles[0], handles[1]};
    }
    unlock_endpoint(state);
  }
  capability_grant_release(&target);
  release_grants(grants, 2);
  object_release(&export->client);
  if (status != CALL_OK) {
    object_release(&export->storage);
  }
  return status;
}

void endpoint_create_execute(struct endpoint_create_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING && request->owner);
  request->result = endpoint_create(request);
  capability_reservation_release(&request->reservation, request->slots);
  request->owner = NULL;
}

void endpoint_export_execute(struct endpoint_export_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_SERVICING && request->owner);
  request->result = endpoint_export_create(request);
  capability_reservation_release(&request->reservation, request->slots);
  capability_release(&request->receiver);
  request->owner = NULL;
}

static enum call_status create_endpoint(struct endpoint_create_reply *reply)
{
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  enum capability_result result = capability_request_reservation(2, &reservation, slots);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return CALL_ENDPOINT_CLOSED;
  }
  struct endpoint_create_request *request =
      (struct endpoint_create_request *)bsp_request_prepare(BSP_SERVICE_ENDPOINT_CREATE);
  request->owner = process_current();
  request->reservation = reservation;
  memcpy(request->slots, slots, sizeof(slots));
  reservation = (struct capability_reservation){0};
  request->reply = (struct endpoint_create_reply){0};
  request->result = CALL_NO_MEMORY;

  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  if (status == CALL_OK) {
    *reply = request->reply;
  }
  bsp_request_release(&request->request);
  return status;
}

static enum call_status export_endpoint(const struct endpoint_export_message *input,
    struct endpoint_export_reply *reply)
{
  if (!input->object_id || !input->protocol ||
      (input->transport & ~HANDLE_TRANSPORT_CALL)) {
    return CALL_BAD_REQUEST;
  }
  struct capability_reference receiver;
  enum capability_result result = capability_acquire(&process_current()->capabilities,
      input->receiver, ENDPOINT_RECEIVER_RIGHT_CONTROL, 0, &receiver);
  if (result != CAP_OK) {
    return grant_status(result);
  }
  result = capability_reference_resolve(&receiver);
  if (result != CAP_OK) {
    capability_release(&receiver);
    return grant_status(result);
  }
  if (receiver.object->type != OBJECT_ENDPOINT_RECEIVER) {
    capability_release(&receiver);
    return CALL_WRONG_TYPE;
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  result = capability_request_reservation(2, &reservation, slots);
  if (result != CAP_OK) {
    capability_release(&receiver);
    return grant_status(result);
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    capability_release(&receiver);
    return CALL_ENDPOINT_CLOSED;
  }
  struct endpoint_export_request *request =
      (struct endpoint_export_request *)bsp_request_prepare(BSP_SERVICE_ENDPOINT_EXPORT);
  request->owner = process_current();
  request->receiver = receiver;
  receiver = (struct capability_reference){0};
  request->reservation = reservation;
  memcpy(request->slots, slots, sizeof(slots));
  reservation = (struct capability_reservation){0};
  request->input = *input;
  request->reply = (struct endpoint_export_reply){0};
  request->result = CALL_NO_MEMORY;

  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->result;
  if (status == CALL_OK) {
    *reply = request->reply;
  }
  bsp_request_release(&request->request);
  return status;
}

static struct syscall_result acknowledge_export(struct endpoint *receiver,
    uintptr_t address, size_t size)
{
  uint64_t id;
  if (size != sizeof(id)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&id, address, sizeof(id))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct endpoint_state *state = receiver->state;
  uint64_t ready_before = begin_endpoint_update(state);
  struct endpoint_export *export = find_export(state, id);
  if (state->closed || !export) {
    enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED : CALL_NOT_FOUND;
    end_endpoint_update(state, ready_before);
    return (struct syscall_result){status, 0};
  }
  if (!task_operation_start()) {
    end_endpoint_update(state, ready_before);
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (!export->retire_delivered || export->in_flight) {
    end_endpoint_update(state, ready_before);
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
  defer_description(export);
  --state->export_count;
  end_endpoint_update(state, ready_before);
  object_release(&export->storage);
  return (struct syscall_result){CALL_OK, 0};
}

static enum call_status capture_grants(const struct endpoint_message *message,
    struct capability_grant *grants)
{
  for (size_t i = 0; i < message->grant_count; ++i) {
    enum capability_result result = capability_grant_acquire(&process_current()->capabilities,
        message->grants[i].handle, message->grants[i].rights,
        message->grants[i].transport, &grants[i]);
    if (result != CAP_OK) {
      release_grants(grants, i);
      return grant_status(result);
    }
  }
  return CALL_OK;
}

static struct syscall_result read_byte_count(struct endpoint *receiver,
    uintptr_t address, size_t size, uintptr_t reply_address, size_t reply_capacity)
{
  uint64_t id;
  if (size != sizeof(id) || reply_capacity < sizeof(uint64_t)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&id, address, sizeof(id))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct endpoint_state *state = receiver->state;
  lock_endpoint(state);
  struct endpoint_export *export = find_export(state, id);
  enum call_status status = state->closed ? CALL_ENDPOINT_CLOSED :
      !export ? CALL_NOT_FOUND : !export->has_read_file ? CALL_BAD_OPERATION : CALL_OK;
  uint64_t bytes = status == CALL_OK ? export->read_bytes : 0;
  unlock_endpoint(state);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  return copy_to_user(reply_address, &bytes, sizeof(bytes)) ?
      (struct syscall_result){CALL_OK, sizeof(bytes)} : (struct syscall_result){CALL_BAD_BUFFER, 0};
}

static enum call_status read_message(uintptr_t address, size_t size,
    struct endpoint_message *message)
{
  if (size != sizeof(*message) - sizeof(message->header)) {
    return CALL_BAD_REQUEST;
  }
  struct user_buffer_lease lease;
  if (!user_buffer_lease_acquire(&lease)) {
    return CALL_BAD_BUFFER;
  }
  enum call_status status = CALL_BAD_REQUEST;
  if (!copy_from_user_leased(&lease, &message->buffer, address, size)) {
    status = CALL_BAD_BUFFER;
    goto done;
  }
  if (message->size > ENDPOINT_DATA_MAX || message->grant_count > ENDPOINT_GRANTS_MAX) {
    goto done;
  }
  for (size_t i = message->grant_count; i < ENDPOINT_GRANTS_MAX; ++i) {
    if (message->grants[i].handle || message->grants[i].rights || message->grants[i].transport) {
      goto done;
    }
  }
  status = user_buffer_check_leased(&lease, message->buffer, message->size,
      USER_BUFFER_READ) ? CALL_OK : CALL_BAD_BUFFER;
done:
  user_buffer_lease_release(&lease);
  return status;
}

static struct syscall_result write_packet(uintptr_t address,
    const struct packet_header *header, const uint8_t *bytes, enum call_status status)
{
  if (!copy_reply_to_user(address, header, sizeof(*header),
      address + sizeof(*header), bytes, header->size)) {
    /* Only ordinary deliveries introduce handles. CANCEL names an existing
     * receipt and must never close somebody else's already delivered grant. */
    if (header->kind == ENDPOINT_MESSAGE_CALL || header->kind == ENDPOINT_MESSAGE_SEND) {
      handle_t handles[ENDPOINT_GRANTS_MAX + 1];
      size_t count = 0;
      if (header->receipt) {
        handles[count++] = header->receipt;
      }
      for (size_t i = 0; i < header->grant_count; ++i) {
        handles[count++] = header->grants[i].handle;
      }
      capability_close_returned(&process_current()->capabilities, handles, count);
    }
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){status, sizeof(*header) + header->size};
}

/* Check the admitted byte copy, never a mutable userspace preflight view. */
static enum call_status provider_authority(const struct endpoint_export *target,
    uint64_t rights, const struct endpoint_message *message, const uint8_t *bytes)
{
  if (!target || target->protocol != PROTOCOL_PROVIDER) {
    return CALL_OK;
  }
  uint64_t required = 0;
  if (message->operation == PROVIDER_OPEN) {
    struct provider_open_request request;
    if (message->size < sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, bytes, sizeof(request));
    if (!request.intent || (request.intent & ~PROVIDER_INTENTS) ||
        (request.flags & ~PROVIDER_OPEN_FLAGS) ||
        ((request.flags & PROVIDER_OPEN_EXCLUSIVE) && !(request.flags & PROVIDER_OPEN_CREATE)) ||
        ((request.flags & (PROVIDER_OPEN_TRUNCATE | PROVIDER_OPEN_APPEND)) &&
        !(request.intent & PROVIDER_INTENT_WRITE)) ||
        request.target_size > PROVIDER_TARGET_MAX_BYTES ||
        message->size - sizeof(request) != request.target_size ||
        request.deadline_ns != message->deadline_ns) {
      return CALL_BAD_REQUEST;
    }
    if (request.intent & PROVIDER_INTENT_READ) {
      required |= PROVIDER_RIGHT_OPEN_READ;
    }
    if ((request.intent & PROVIDER_INTENT_WRITE) || (request.flags & PROVIDER_OPEN_CREATE)) {
      required |= PROVIDER_RIGHT_OPEN_WRITE;
    }
  } else if (message->operation == HTTP_PREPARE_ENDPOINT) {
    struct http_endpoint_request request;
    if (message->size < sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, bytes, sizeof(request));
    if (request.deadline_ns != message->deadline_ns ||
        (request.response_flags & ~HTTP_ENDPOINT_RESPONSE_FLAGS) ||
        (request.method != HTTP_ENDPOINT_GET && request.method != HTTP_ENDPOINT_POST &&
        request.method != HTTP_ENDPOINT_PUT && request.method != HTTP_ENDPOINT_PATCH) ||
        request.target_size > PROVIDER_TARGET_MAX_BYTES ||
        request.target_size > message->size - sizeof(request) ||
        request.headers_size != message->size - sizeof(request) - request.target_size) {
      return CALL_BAD_REQUEST;
    }
    required = request.method == HTTP_ENDPOINT_GET ? PROVIDER_RIGHT_OPEN_READ :
        PROVIDER_RIGHT_OPEN_WRITE;
  } else if (message->operation == PROVIDER_REMOVE) {
    struct provider_remove_request request;
    if (message->size < sizeof(request)) {
      return CALL_BAD_REQUEST;
    }
    memcpy(&request, bytes, sizeof(request));
    if (request.target_size > PROVIDER_TARGET_MAX_BYTES ||
        message->size - sizeof(request) != request.target_size ||
        request.deadline_ns != message->deadline_ns) {
      return CALL_BAD_REQUEST;
    }
    required = PROVIDER_RIGHT_REMOVE;
  }
  return (rights & required) == required ? CALL_OK : CALL_DENIED;
}

/* Endpoint guard held. Only published byte-resource outcomes affect READ;
 * private continuation operations keep their existing provider behavior. */
static enum call_status description_read_status(const struct endpoint_export *target,
    const struct endpoint_message *message)
{
  if (!target || !target->description ||
      !((target->protocol == PROTOCOL_FILE && message->operation == FILE_READ) ||
      (target->protocol == PROTOCOL_STREAM && message->operation == STREAM_READ))) {
    return CALL_OK;
  }
  switch (target->description->value.outcome) {
  case RESOURCE_OK: return CALL_OK;
  case RESOURCE_NOT_FOUND: return CALL_NOT_FOUND;
  case RESOURCE_DENIED: return CALL_DENIED;
  case RESOURCE_IO_ERROR: return CALL_IO;
  case RESOURCE_TOO_LARGE: return CALL_FILE_TOO_LARGE;
  case RESOURCE_ABORTED: return CALL_ABANDONED;
  case RESOURCE_UNCERTAIN: return CALL_OUTCOME_UNKNOWN;
  default: panic("invalid cached resource outcome");
  }
}

/* Admission owns all copied bytes and references. A SEND publishes no task
 * pointer and never touches its record after unlocking: the receiver may
 * already finish it before the sender returns. A CALL keeps its record until
 * collection, independently of receipt retirement. */
static enum call_status admit_message(struct endpoint *endpoint,
    struct endpoint_export *target, uint64_t resource_rights,
    const struct endpoint_message *message, struct task_wait *wait,
    struct endpoint_delivery_record **result, struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots)
{
  KASSERT((wait != NULL) == (result != NULL));
  KASSERT((wait != NULL) == (reservation != NULL));
  struct capability_grant grants[ENDPOINT_GRANTS_MAX];
  enum call_status status = capture_grants(message, grants);
  if (status != CALL_OK) {
    return status;
  }
  struct endpoint_state *state = endpoint->state;
  struct endpoint_delivery_record *record = NULL;
  lock_endpoint(state);
  if (wait && deadline_expired(message->deadline_ns)) {
    status = CALL_TIMED_OUT;
  } else if (state->closed || (target && (!target->client_live || target->withdrawn))) {
    status = CALL_ENDPOINT_CLOSED;
  } else {
    status = description_read_status(target, message);
    /* Reject before admission: an incompatible SEND at the FIFO head must not
     * leave the receiver permanently unable to collect later valid messages. */
    for (size_t i = 0; status == CALL_OK && i < message->grant_count; ++i) {
      if (state->execution_group && !launcher_grant_allowed(state->execution_group, grants[i].object)) {
        status = CALL_DENIED;
        break;
      }
    }
  }
  if (status == CALL_OK) {
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
  if (!copy_from_user(record->request, message->buffer, message->size)) {
    status = CALL_BAD_BUFFER;
  } else {
    status = provider_authority(target, resource_rights, message, record->request);
  }
  if (status != CALL_OK) {
    lock_endpoint(state);
    release_delivery_slot(record);
    unlock_endpoint(state);
    release_grants(grants, message->grant_count);
    return status;
  }
  forwarder_operation_init(&record->origin);
  struct forwarder_operation *origin = task_forwarder_operation();
  status = forwarder_operation_clone(&record->origin, origin);
  if (status != CALL_OK) {
    lock_endpoint(state);
    release_delivery_slot(record);
    unlock_endpoint(state);
    release_grants(grants, message->grant_count);
    return status;
  }
  uint64_t ready_before = begin_endpoint_update(state);
  bool expired = wait && deadline_expired(message->deadline_ns);
  if (expired || state->closed || (target && (!target->client_live || target->withdrawn)) ||
      forwarder_operation_denied(&record->origin) || !task_operation_start()) {
    release_delivery_slot(record);
    status = expired ? CALL_TIMED_OUT : state->closed ||
        (target && (!target->client_live || target->withdrawn)) ?
        CALL_ENDPOINT_CLOSED : CALL_DENIED;
    forwarder_operation_release(&record->origin);
    end_endpoint_update(state, ready_before);
    release_grants(grants, message->grant_count);
    return status;
  }
  status = description_read_status(target, message);
  if (status != CALL_OK) {
    release_delivery_slot(record);
    forwarder_operation_release(&record->origin);
    end_endpoint_update(state, ready_before);
    release_grants(grants, message->grant_count);
    return status;
  }
  if (target && !object_retain(&target->client)) {
    forwarder_operation_release(&record->origin);
    release_delivery_slot(record);
    end_endpoint_update(state, ready_before);
    release_grants(grants, message->grant_count);
    return CALL_LIMIT;
  }
  record->target = target;
  record->completion = NULL;
  record->completion_committed = record->completion_pending = false;
  if (target) {
    ++target->in_flight;
  }
  record->operation = message->operation;
  record->rights = resource_rights;
  record->endpoint = state;
  record->wait = wait;
  if (wait && record->origin.count) {
    forwarder_operation_wait(&record->origin, wait);
  }
  record->next = NULL;
  record->kind = wait ? ENDPOINT_MESSAGE_CALL : ENDPOINT_MESSAGE_SEND;
  record->caller_active = wait != NULL;
  record->receipt_live = true;
  record->delivered = false;
  record->cancel_pending = false;
  record->replying = false;
  record->receipt_handle = HANDLE_INVALID;
  record->deadline_ns = message->deadline_ns;
  record->request_size = message->size;
  record->request_count = message->grant_count;
  record->reply_count = record->reply_size = record->result = 0;
  record->status = CALL_OK;
  for (size_t i = 0; i < message->grant_count; ++i) {
    record->request_grants[i] = grants[i];
  }
  record->reply_reservation = (struct capability_reservation){0};
  if (reservation) {
    record->reply_reservation = *reservation;
    memcpy(record->reply_slots, slots, sizeof(record->reply_slots));
    *reservation = (struct capability_reservation){0};
  }
  object_init(&record->receipt, OBJECT_ENDPOINT_RECEIPT, NULL);
  if (record->origin.count) {
    forwarder_operation_delivery_owner(&record->origin, &record->receipt);
  }
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
  if (forwarder_operation_denied(&record->origin)) {
    status = deadline_expired(record->deadline_ns) ? CALL_TIMED_OUT : CALL_DENIED;
    cancel_delivery(state, record, status);
  } else {
    wake_waiter(&state->waiting_receiver);
  }
  end_endpoint_update(state, ready_before);
  return wait ? CALL_OK : status;
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
    status = admit_message(endpoint, target, resource_rights, &message, NULL, NULL, NULL, NULL);
  }
  return (struct syscall_result){status, 0};
}

/* Keep the export as the authority/lifetime boundary. Only the READ data copy
 * bypasses its userspace receiver; no native backing grant escapes. */
static bool read_bound_file(struct endpoint_export *target, uint64_t rights,
    const struct endpoint_message *message, struct packet_header *output,
    uintptr_t reply_address, struct syscall_result *result)
{
  struct file_read_request request = {0};
  enum call_status status = message->grant_count || message->size != sizeof(request) ?
      CALL_BAD_REQUEST : copy_from_user(&request, message->buffer, sizeof(request)) ?
      CALL_OK : CALL_BAD_BUFFER;
  if (status == CALL_OK && request.capacity > FILE_READ_MAX_BYTES) {
    status = CALL_BAD_REQUEST;
  }
  struct endpoint_state *state = target->endpoint;
  lock_endpoint(state);
  struct endpoint_description *snapshot = target->description;
  if (!snapshot || !snapshot->read_file) {
    unlock_endpoint(state);
    return false;
  }
  if (status == CALL_OK) {
    status = state->closed || target->withdrawn || !target->client_live ? CALL_ENDPOINT_CLOSED :
        !(rights & FILE_RIGHT_READ) ? CALL_DENIED :
        deadline_expired(message->deadline_ns) ? CALL_TIMED_OUT :
        description_read_status(target, message);
  }
  uint64_t count = request.offset < snapshot->value.length ? snapshot->value.length - request.offset : 0;
  if (count > request.capacity) {
    count = request.capacity;
  }
  if (status == CALL_OK && (target->reserved_read_bytes > UINT64_MAX - target->read_bytes ||
      count > UINT64_MAX - target->read_bytes - target->reserved_read_bytes)) {
    status = CALL_IO;
  }
  bool retained = status == CALL_OK && object_retain(&snapshot->object);
  if (status == CALL_OK && !retained) {
    status = CALL_LIMIT;
  }
  if (status == CALL_OK && !task_operation_start()) {
    status = CALL_DENIED;
  }
  if (status != CALL_OK) {
    unlock_endpoint(state);
    if (retained) {
      object_release(&snapshot->object);
    }
    *result = write_packet(reply_address, output, NULL, status);
    return true;
  }
  ++target->in_flight;
  target->reserved_read_bytes += count;
  unlock_endpoint(state);

  output->delivery = ENDPOINT_DELIVERED;
  output->result = CALL_OK;
  output->size = sizeof(struct file_read_reply) + count;
  struct syscall_result copied = file_read_captured(snapshot->read_file, &request,
      reply_address, output, sizeof(*output));
  uint64_t received = copied.status == CALL_OK ?
      copied.reply_size - sizeof(*output) - sizeof(struct file_read_reply) : 0;
  KASSERT(received <= count);
  uint64_t ready_before = begin_endpoint_update(state);
  target->reserved_read_bytes -= count;
  target->read_bytes += received;
  KASSERT(target->in_flight);
  --target->in_flight;
  retire_export(target);
  end_endpoint_update(state, ready_before);
  object_release(&snapshot->object);

  status = deadline_expired(message->deadline_ns) ? CALL_TIMED_OUT : CALL_OK;
  if (status == CALL_OK && copied.status == CALL_OK) {
    *result = copied;
  } else {
    output->size = 0;
    output->result = copied.status;
    *result = write_packet(reply_address, output, NULL, status);
  }
  return true;
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
  if (task_stop_requested()) {
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  if (status == CALL_OK && target && target->protocol == PROTOCOL_FILE && message.operation == FILE_READ) {
    struct syscall_result result;
    if (read_bound_file(target, resource_rights, &message, &output, reply_address, &result)) {
      return result;
    }
  }
  /* These claims remain in the delivery record until collection, even when
   * timeout or receipt retirement wins before the caller resumes. */
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[ENDPOINT_GRANTS_MAX];
  if (status == CALL_OK) {
    status = grant_status(capability_request_reservation(ENDPOINT_GRANTS_MAX,
        &reservation, slots));
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  if (status != CALL_OK) {
    return write_packet(reply_address, &output, NULL, status);
  }
  struct task_wait *wait = task_wait_prepare();
  struct endpoint_delivery_record *record;
  status = admit_message(endpoint, target, resource_rights, &message, wait, &record,
      &reservation, slots);
  if (status != CALL_OK) {
    capability_reservation_release(&reservation, slots);
    return write_packet(reply_address, &output, NULL, status);
  }
  bool resumed = message.deadline_ns ?
      task_wait_sleep_until_interruptible(wait, message.deadline_ns) :
      task_wait_sleep_interruptible(wait);

  struct endpoint_state *state = endpoint->state;
  uint64_t ready_before = begin_endpoint_update(state);
  if (!resumed || task_stop_requested()) {
    if (record->state != DELIVERY_COMPLETE) {
      cancel_delivery(state, record, CALL_ENDPOINT_CLOSED);
    }
    KASSERT(!record->wait);
    defer_grants(record, record->reply_grants, record->reply_count);
    record->reply_count = 0;
    capability_reservation_release(&record->reply_reservation, record->reply_slots);
    record->caller_active = false;
    free_delivery(record);
    end_endpoint_update(state, ready_before);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  if (record->state != DELIVERY_COMPLETE) {
    if (deadline_expired(record->deadline_ns)) {
      expire_delivery(state, record);
    } else {
      KASSERT(record->state == DELIVERY_QUEUED &&
          forwarder_operation_denied(&record->origin));
      cancel_delivery(state, record, CALL_DENIED);
    }
  }
  KASSERT(record->state == DELIVERY_COMPLETE && !record->wait);
  status = record->status;
  output.delivery = record->delivered ? ENDPOINT_DELIVERED : ENDPOINT_NOT_DELIVERED;
  if (status == CALL_OK) {
    handle_t handles[ENDPOINT_GRANTS_MAX];
    output.result = record->result;
    output.size = record->reply_size;
    output.grant_count = record->reply_count;
    for (size_t i = 0; i < record->reply_count; ++i) {
      output.grants[i].rights = record->reply_grants[i].rights;
      output.grants[i].transport = record->reply_grants[i].transport;
    }
    capability_install_reserved(&record->reply_reservation, record->reply_slots,
        record->reply_grants, record->reply_count, handles);
    for (size_t i = 0; i < record->reply_count; ++i) {
      output.grants[i].handle = handles[i];
    }
  }
  defer_grants(record, record->reply_grants, record->reply_count);
  record->reply_count = 0;
  capability_reservation_release(&record->reply_reservation, record->reply_slots);
  end_endpoint_update(state, ready_before);
  struct syscall_result result = write_packet(reply_address, &output, record->reply, status);
  ready_before = begin_endpoint_update(state);
  record->caller_active = false;
  free_delivery(record);
  end_endpoint_update(state, ready_before);
  return result;
}

static enum call_status ordinary_receive_output(uintptr_t address, size_t capacity)
{
  if (capacity < sizeof(struct endpoint_packet)) {
    return CALL_BAD_REQUEST;
  }
  return user_buffer_check(address, sizeof(struct endpoint_packet), USER_BUFFER_WRITE) ?
      CALL_OK : CALL_BAD_BUFFER;
}

static struct syscall_result receive_endpoint_owned(struct endpoint *endpoint,
    uintptr_t reply_address, size_t reply_capacity, bool nonblocking)
{
  struct endpoint_state *state = endpoint->state;
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[ENDPOINT_GRANTS_MAX + 1];
  for (;;) {
    uint64_t ready_before = begin_endpoint_update(state);
    if (task_stop_requested() || state->closed || task_operation_denied()) {
      enum call_status stopped = task_operation_denied() ? CALL_DENIED : CALL_ENDPOINT_CLOSED;
      end_endpoint_update(state, ready_before);
      capability_reservation_release(&reservation, slots);
      return (struct syscall_result){stopped, 0};
    }
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      struct endpoint_delivery_record *due = &state->deliveries[i];
      if ((due->state == DELIVERY_QUEUED || due->state == DELIVERY_RECEIVED) &&
          due->kind == ENDPOINT_MESSAGE_CALL && deadline_expired(due->deadline_ns)) {
        expire_delivery(state, due);
      } else if (due->state == DELIVERY_QUEUED &&
          forwarder_operation_denied(&due->origin)) {
        cancel_delivery(state, due, CALL_DENIED);
      }
    }
    for (size_t i = 0; i < ENDPOINT_DELIVERIES_MAX; ++i) {
      struct endpoint_delivery_record *cancel = &state->deliveries[i];
      if (!cancel->cancel_pending) {
        continue;
      }
      KASSERT(cancel->state == DELIVERY_COMPLETE && cancel->receipt_live &&
          cancel->receipt_handle);
      enum call_status status = ordinary_receive_output(reply_address, reply_capacity);
      if (status != CALL_OK) {
        end_endpoint_update(state, ready_before);
        capability_reservation_release(&reservation, slots);
        return (struct syscall_result){status, 0};
      }
      if (!task_operation_start()) {
        end_endpoint_update(state, ready_before);
        capability_reservation_release(&reservation, slots);
        return (struct syscall_result){CALL_DENIED, 0};
      }
      struct packet_header output = {.receipt = cancel->receipt_handle,
        .delivery = ENDPOINT_DELIVERED, .kind = ENDPOINT_MESSAGE_CANCEL,
        .deadline_ns = cancel->deadline_ns,
        .object_id = cancel->target ? cancel->target->object_id : 0,
        .protocol = cancel->target ? cancel->target->protocol : 0,
        .operation = cancel->operation, .rights = cancel->rights, .reason = cancel->status};
      cancel->cancel_pending = false;
      end_endpoint_update(state, ready_before);
      capability_reservation_release(&reservation, slots);
      return write_packet(reply_address, &output, NULL, CALL_OK);
    }
    for (struct endpoint_export *export = state->exports; export; export = export->next) {
      if (export->retire_pending) {
        enum call_status status = ordinary_receive_output(reply_address, reply_capacity);
        if (status != CALL_OK) {
          end_endpoint_update(state, ready_before);
          capability_reservation_release(&reservation, slots);
          return (struct syscall_result){status, 0};
        }
        if (!task_operation_start()) {
          end_endpoint_update(state, ready_before);
          capability_reservation_release(&reservation, slots);
          return (struct syscall_result){CALL_DENIED, 0};
        }
        struct packet_header output = {.kind = ENDPOINT_MESSAGE_RETIRE,
          .object_id = export->object_id, .protocol = export->protocol};
        export->retire_pending = false;
        export->retire_delivered = true;
        end_endpoint_update(state, ready_before);
        capability_reservation_release(&reservation, slots);
        return write_packet(reply_address, &output, NULL, CALL_OK);
      }
    }
    struct endpoint_delivery_record *record = state->head;
    if (!record) {
      if (nonblocking) {
        end_endpoint_update(state, ready_before);
        capability_reservation_release(&reservation, slots);
        return (struct syscall_result){CALL_WOULD_BLOCK, 0};
      }
      struct task_wait *wait = task_wait_prepare();
      KASSERT(!state->waiting_receiver);
      state->waiting_receiver = wait;
      struct forwarder_operation *origin = task_forwarder_operation();
      if (origin && origin->count) {
        forwarder_operation_wait(origin, wait);
      }
      end_endpoint_update(state, ready_before);
      capability_reservation_release(&reservation, slots);
      bool resumed = task_wait_sleep_interruptible(wait);
      ready_before = begin_endpoint_update(state);
      if (origin && origin->count) {
        forwarder_operation_unwait(origin, wait);
      }
      if (state->waiting_receiver == wait) {
        state->waiting_receiver = NULL;
      }
      bool stopped = !resumed || task_stop_requested();
      end_endpoint_update(state, ready_before);
      if (stopped) {
        return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
      }
      continue;
    }
    bool completion = record->kind == ENDPOINT_MESSAGE_COMPLETION;
    enum call_status status = completion ?
        (reply_capacity < ENDPOINT_PACKET_HEADER_SIZE + record->request_size ?
            CALL_BUFFER_TOO_SMALL : CALL_OK) :
        ordinary_receive_output(reply_address, reply_capacity);
    if (status != CALL_OK) {
      end_endpoint_update(state, ready_before);
      capability_reservation_release(&reservation, slots);
      return (struct syscall_result){status, 0};
    }
    size_t count = record->request_count;
    if (!completion && reservation.count < count + 1) {
      capability_reservation_release(&reservation, slots);
      enum capability_result result = capability_reserve(&process_current()->capabilities,
          count + 1, &reservation, slots);
      if (result != CAP_OK) {
        end_endpoint_update(state, ready_before);
        if (result == CAP_FULL) {
          result = capability_request_reservation(count + 1, &reservation, slots);
          if (result == CAP_OK) {
            /* Growth can change the selected message or its deadline. */
            continue;
          }
        }
        return (struct syscall_result){grant_status(result), 0};
      }
    }
    struct capability_reservation *claim = completion ? &record->reply_reservation : &reservation;
    struct capability_reserved_slot *claimed_slots = completion ? record->reply_slots : slots;
    KASSERT(claim->table && claim->count >= count + 1);
    struct capability_grant grants[ENDPOINT_GRANTS_MAX + 1];
    grants[0] = (struct capability_grant){&record->receipt,
        record->kind == ENDPOINT_MESSAGE_CALL ? ENDPOINT_RIGHT_REPLY : 0, 0, 0};
    for (size_t i = 0; i < count; ++i) {
      grants[i + 1] = record->request_grants[i];
    }
    enum capability_result result = capability_validate_grants(claim->table, grants, count + 1);
    if (result != CAP_OK) {
      end_endpoint_update(state, ready_before);
      capability_reservation_release(&reservation, slots);
      return (struct syscall_result){grant_status(result), 0};
    }
    struct packet_header output = {.delivery = ENDPOINT_DELIVERED,
      .kind = record->kind, .size = record->request_size, .grant_count = count,
      .deadline_ns = record->deadline_ns,
      .object_id = record->target ? record->target->object_id : 0,
      .protocol = record->target ? record->target->protocol : 0,
      .operation = record->operation, .rights = record->rights};
    if (completion) {
      struct native_completion *operation = record->completion;
      output.result = record->status;
      output.protocol = operation->protocol;
      output.correlation_id = operation->correlation_id;
      output.completion_flags = native_completion_flags(operation);
      output.progress = operation->progress;
      output.reason = native_completion_cancel_reason(operation);
    }
    for (size_t i = 0; i < count; ++i) {
      output.grants[i].rights = grants[i + 1].rights;
      output.grants[i].transport = grants[i + 1].transport;
    }
    struct user_buffer_lease lease;
    if (!user_buffer_lease_acquire(&lease)) {
      end_endpoint_update(state, ready_before);
      capability_reservation_release(&reservation, slots);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!user_buffer_check_leased(&lease, reply_address, sizeof(output) + output.size,
        USER_BUFFER_WRITE)) {
      end_endpoint_update(state, ready_before);
      user_buffer_lease_release(&lease);
      capability_reservation_release(&reservation, slots);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!forwarder_operation_start_pair(task_forwarder_operation(),
        completion ? NULL : &record->origin)) {
      bool receiver_denied = task_operation_denied();
      if (!completion && forwarder_operation_denied(&record->origin)) {
        cancel_delivery(state, record, deadline_expired(record->deadline_ns) ?
            CALL_TIMED_OUT : CALL_DENIED);
      }
      end_endpoint_update(state, ready_before);
      user_buffer_lease_release(&lease);
      if (receiver_denied) {
        capability_reservation_release(&reservation, slots);
        return (struct syscall_result){CALL_DENIED, 0};
      }
      continue;
    }
    handle_t handles[ENDPOINT_GRANTS_MAX + 1];
    /* Move the queue's initial receipt reference and captured attachment grants
     * into the claimed slots. No logical retain runs under the endpoint lock. */
    capability_install_reserved(claim, claimed_slots, grants, count + 1, handles);
    for (size_t i = 0; i < count; ++i) {
      record->request_grants[i] = (struct capability_grant){0};
      output.grants[i].handle = handles[i + 1];
    }
    record->request_count = 0;
    state->head = record->next;
    if (!state->head) {
      state->tail = NULL;
    }
    record->next = NULL;
    record->state = DELIVERY_RECEIVED;
    record->delivered = true;
    record->receipt_handle = output.receipt = handles[0];
    /* The guard keeps the published receipt from retiring/reusing this record
     * until its bounded copy finishes; the lease keeps both outputs mapped. */
    bool copied = copy_reply_to_user_leased(&lease, reply_address, &output, sizeof(output),
        reply_address + sizeof(output), record->request, output.size);
    /* Completion output was validated under this same mapping lease before
     * consuming its private claims; no later recoverable copy failure exists. */
    KASSERT(!completion || copied);
    end_endpoint_update(state, ready_before);
    user_buffer_lease_release(&lease);
    capability_reservation_release(&reservation, slots);
    if (!copied) {
      capability_close_returned(&process_current()->capabilities, handles, count + 1);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(output) + output.size};
  }
}

static struct syscall_result receive_endpoint(struct endpoint *endpoint, uintptr_t reply_address,
    size_t reply_capacity, bool nonblocking)
{
  struct endpoint_state *state = endpoint->state;
  lock_endpoint(state);
  if (state->receiving) {
    unlock_endpoint(state);
    return (struct syscall_result){CALL_BUSY, 0};
  }
  state->receiving = true;
  unlock_endpoint(state);

  struct syscall_result result = receive_endpoint_owned(endpoint, reply_address, reply_capacity,
      nonblocking);
  lock_endpoint(state);
  state->receiving = false;
  unlock_endpoint(state);
  return result;
}

static enum call_status derive_reply_children(struct endpoint_delivery_record *record,
    struct capability_grant *grants, size_t count)
{
  if (!record->target) {
    return CALL_OK;
  }
  size_t children;
  if (record->target->protocol == PROTOCOL_PROVIDER &&
      (record->operation == PROVIDER_OPEN ||
      record->operation == PROVIDER_REMOVE || record->operation == HTTP_PREPARE_ENDPOINT)) {
    children = count;
  } else if (record->target->protocol == PROTOCOL_TRANSACTION &&
      (record->operation == TRANSACTION_STAGE || record->operation == TRANSACTION_COMMIT)) {
    children = count;
  } else {
    return CALL_OK;
  }
  /* The sender's started operation owns the ancestor storage throughout REPLY.
   * STAGE and COMMIT derive their returned resources through that origin.
   * Unrelated pre-existing attachments on other operations retain their lineage. */
  for (size_t i = 0; i < children; ++i) {
    enum capability_result result = forwarder_request_derivation_from(&grants[i], &record->origin);
    if (result != CAP_OK) {
      return grant_status(result);
    }
  }
  return CALL_OK;
}

static struct syscall_result reply_endpoint_owned(struct endpoint_delivery_record *record,
    handle_t handle, uintptr_t request_address, size_t request_size, uint8_t *data)
{
  struct endpoint_message message;
  enum call_status status = read_message(request_address, request_size, &message);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (message.deadline_ns || message.protocol || message.operation) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_grant grants[ENDPOINT_GRANTS_MAX];
  status = capture_grants(&message, grants);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct endpoint_state *state = record->endpoint;
  uint64_t ready_before = begin_endpoint_update(state);
  if (record->state == DELIVERY_RECEIVED && deadline_expired(record->deadline_ns)) {
    expire_delivery(state, record);
  }
  if (record->state != DELIVERY_RECEIVED || state->closed) {
    status = record->status == CALL_TIMED_OUT ? CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    end_endpoint_update(state, ready_before);
    release_grants(grants, message.grant_count);
    return (struct syscall_result){status, 0};
  }
  end_endpoint_update(state, ready_before);
  if (!copy_from_user(data, message.buffer, message.size)) {
    release_grants(grants, message.grant_count);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  status = derive_reply_children(record, grants, message.grant_count);
  if (status != CALL_OK) {
    release_grants(grants, message.grant_count);
    return (struct syscall_result){status, 0};
  }
  ready_before = begin_endpoint_update(state);
  if (record->state == DELIVERY_RECEIVED && deadline_expired(record->deadline_ns)) {
    expire_delivery(state, record);
  }
  if (record->state != DELIVERY_RECEIVED || state->closed) {
    status = record->status == CALL_TIMED_OUT ? CALL_TIMED_OUT : CALL_ENDPOINT_CLOSED;
    end_endpoint_update(state, ready_before);
    release_grants(grants, message.grant_count);
    return (struct syscall_result){status, 0};
  }
  enum capability_result policy = capability_validate_grants(record->reply_reservation.table,
      grants, message.grant_count);
  if (policy != CAP_OK) {
    end_endpoint_update(state, ready_before);
    release_grants(grants, message.grant_count);
    return (struct syscall_result){grant_status(policy), 0};
  }
  memcpy(record->reply, data, message.size);
  record->reply_size = message.size;
  record->result = message.result;
  record->reply_count = message.grant_count;
  for (size_t i = 0; i < message.grant_count; ++i) {
    record->reply_grants[i] = grants[i];
  }
  record->status = CALL_OK;
  record->state = DELIVERY_COMPLETE;
  wake_delivery_waiter(record);
  end_endpoint_update(state, ready_before);
  /* A sibling CLOSE may already have retired this exact receipt generation. */
  (void)capability_close(&process_current()->capabilities, handle);
  return (struct syscall_result){CALL_OK, 0};
}

static struct syscall_result reply_endpoint(struct endpoint_delivery_record *record,
    handle_t handle, uintptr_t request_address, size_t request_size)
{
  struct endpoint_state *state = record->endpoint;
  lock_endpoint(state);
  if (record->replying) {
    unlock_endpoint(state);
    return (struct syscall_result){CALL_BUSY, 0};
  }
  record->replying = true;
  unlock_endpoint(state);

  uint8_t *data = kmalloc(ENDPOINT_DATA_MAX);
  struct syscall_result result = {CALL_NO_MEMORY, 0};
  if (data) {
    result = reply_endpoint_owned(record, handle, request_address, request_size, data);
    kfree(data);
  }
  lock_endpoint(state);
  record->replying = false;
  unlock_endpoint(state);
  return result;
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
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      handle_t handles[] = {reply.client, reply.controller};
      capability_close_returned(&process_current()->capabilities, handles, 2);
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
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
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    handle_t handles[] = {reply.receiver, reply.caller};
    capability_close_returned(&process_current()->capabilities, handles, 2);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
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
    if (operation == ENDPOINT_RETIRE_ACK) {
      if (!(rights & ENDPOINT_RECEIVER_RIGHT_CONTROL)) {
        return (struct syscall_result){CALL_DENIED, 0};
      }
      return acknowledge_export(endpoint, request_address, request_size);
    }
    if (operation == ENDPOINT_DESCRIBE_PUBLISH) {
      if (!(rights & ENDPOINT_RECEIVER_RIGHT_CONTROL)) {
        return (struct syscall_result){CALL_DENIED, 0};
      }
      return publish_description(endpoint, request_address, request_size);
    }
    if (operation == ENDPOINT_READ_BYTES) {
      if (!(rights & ENDPOINT_RECEIVER_RIGHT_CONTROL)) {
        return (struct syscall_result){CALL_DENIED, 0};
      }
      return read_byte_count(endpoint, request_address, request_size, reply_address, reply_capacity);
    }
    if (operation != ENDPOINT_RECEIVE && operation != ENDPOINT_TRY_RECEIVE) {
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
  size_t required = object->type == OBJECT_ENDPOINT_RECEIVER ?
      ENDPOINT_PACKET_HEADER_SIZE : sizeof(struct endpoint_packet);
  if (reply_capacity < required) {
    return (struct syscall_result){object->type == OBJECT_ENDPOINT_RECEIVER ?
        CALL_BUFFER_TOO_SMALL : CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, required, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return object->type == OBJECT_ENDPOINT_RECEIVER ?
      receive_endpoint(endpoint, reply_address, reply_capacity, operation == ENDPOINT_TRY_RECEIVE) :
      call_endpoint(endpoint, target, rights, request_address, request_size, reply_address);
}
