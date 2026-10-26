#include <kernel/object/wifi.h>
#include <abi/output.h>
#include <abi/wait.h>
#include <arch/smp.h>
#include <kernel/object/output.h>
#include <kernel/object/retention.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/user/readiness.h>
#include <kernel/user_memory.h>

/* IF=0. This guard nests no other locks. Capture only pointers/counters here;
 * grant callbacks, allocation, user access and notification run after unlock. */
struct output_pair {
  struct spinlock lock;
  struct output_end producer, collector;
  struct process *owner; /* Identity only; handle teardown closes the collector. */
  size_t grants[2], remaining, active;
  uint64_t mode;
  bool sealed, closed, failed, taken, admitting, taking;
  struct file_object *bytes; /* Owned private RAM backing; never writable outside. */
  struct capability_grant value[2];
};

struct output_discard {
  struct file_object *bytes;
  struct capability_grant value[2];
};

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

static void detach_output(struct output_pair *pair, struct output_discard *discard)
{
  *discard = (struct output_discard){.bytes = pair->bytes,
    .value = {pair->value[0], pair->value[1]}};
  pair->bytes = NULL;
  pair->value[0] = (struct capability_grant){0};
  pair->value[1] = (struct capability_grant){0};
}

static void discard_output(struct output_discard *discard)
{
  capability_grant_release(&discard->value[0]);
  capability_grant_release(&discard->value[1]);
  if (discard->bytes) {
    object_release(&discard->bytes->object);
    discard->bytes = NULL;
  }
}

static void close_collector(struct output_pair *pair)
{
  struct output_discard discard;
  spin_lock(&pair->lock);
  pair->sealed = true;
  pair->closed = true;
  detach_output(pair, &discard);
  spin_unlock(&pair->lock);
  discard_output(&discard);
  readiness_notify();
}

bool output_grant_retain(struct output_end *end)
{
  struct output_pair *pair = end->pair;
  spin_lock(&pair->lock);
  size_t index = end->collector ? 1 : 0;
  bool valid = pair->grants[index] != SIZE_MAX;
  if (valid) {
    ++pair->grants[index];
  }
  spin_unlock(&pair->lock);
  return valid;
}

void output_grant_release(struct output_end *end)
{
  struct output_pair *pair = end->pair;
  spin_lock(&pair->lock);
  size_t index = end->collector ? 1 : 0;
  KASSERT(pair->grants[index]);
  bool last = --pair->grants[index] == 0;
  if (last && !end->collector) {
    pair->sealed = true;
  }
  spin_unlock(&pair->lock);
  if (last && end->collector) {
    close_collector(pair);
  } else if (last) {
    readiness_notify();
  }
}

static void destroy_output_end(struct kernel_object *object)
{
  struct output_end *end = (struct output_end *)object;
  struct output_pair *pair = end->pair;
  if (end->collector) {
    close_collector(pair);
  }
  KASSERT(!pair->grants[end->collector ? 1 : 0] && pair->remaining);
  if (!--pair->remaining) {
    KASSERT(!pair->active && !pair->bytes && !pair->value[0].object &&
        !pair->value[1].object);
    kfree(pair);
  }
}

bool output_collector_owned(const struct kernel_object *object, struct process *caller)
{
  return object->type == OBJECT_OUTPUT_COLLECTOR &&
      ((const struct output_end *)object)->pair->owner == caller;
}

static uint64_t flags_locked(struct output_pair *pair)
{
  return (pair->sealed ? OUTPUT_SEALED : 0) |
      (pair->failed ? OUTPUT_FAILED : 0) |
      (pair->taken ? OUTPUT_TAKEN : 0) |
      (pair->sealed && !pair->active && !pair->closed && !pair->taken &&
          !pair->taking ? OUTPUT_READY : 0);
}

uint64_t output_ready(struct output_end *end, uint64_t events)
{
  uint64_t flags = cpu_save_interrupts();
  struct output_pair *pair = end->pair;
  spin_lock(&pair->lock);
  uint64_t ready = pair->closed || pair->taken ? WAIT_CLOSED :
      ((events & WAIT_COMPLETE) && (flags_locked(pair) & OUTPUT_READY)) ? WAIT_COMPLETE : 0;
  spin_unlock(&pair->lock);
  cpu_restore_interrupts(flags);
  return ready;
}

static struct output_pair *create_output(struct process *owner,
    struct retention_account *account)
{
  struct output_pair *pair = kmalloc(sizeof(*pair));
  if (!pair) {
    return NULL;
  }
  memset(pair, 0, sizeof(*pair));
  pair->bytes = file_create_retained(account);
  if (!pair->bytes) {
    kfree(pair);
    return NULL;
  }
  atomic_init(&pair->lock.locked, false);
  pair->owner = owner;
  pair->remaining = 2;
  object_init(&pair->producer.object, OBJECT_OUTPUT_PRODUCER, destroy_output_end);
  object_init(&pair->collector.object, OBJECT_OUTPUT_COLLECTOR, destroy_output_end);
  pair->producer.pair = pair;
  pair->collector.pair = pair;
  pair->collector.collector = true;
  return pair;
}

static void create_execute(struct output_request *request)
{
  struct output_pair *pair = create_output(request->owner, request->account);
  request->status = CALL_NO_MEMORY;
  if (!pair) {
    return;
  }
  struct capability_grant grants[2] = {0};
  enum capability_result result = capability_grant_retain(&pair->collector.object,
      OUTPUT_RIGHT_COLLECT, 0, &grants[0]);
  if (result == CAP_OK) {
    result = capability_grant_retain(&pair->producer.object, FILE_RIGHT_WRITE, 0,
        &grants[1]);
  }
  for (size_t i = 0; result == CAP_OK && i < 2; ++i) {
    result = forwarder_derive_grant(&grants[i], task_forwarder_operation());
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, grants, 2);
  }
  if (result == CAP_OK && !task_operation_start()) {
    result = CAP_DENIED;
  }
  if (result == CAP_OK) {
    handle_t handles[2];
    capability_install_reserved(&request->reservation, request->slots, grants, 2, handles);
    request->created = (struct output_create_reply){handles[0], handles[1]};
  }
  request->status = cap_status(result);
  capability_grant_release(&grants[0]);
  capability_grant_release(&grants[1]);
  object_release(&pair->collector.object);
  object_release(&pair->producer.object);
}

static enum call_status take_status_locked(struct output_pair *pair)
{
  if (pair->closed) {
    return CALL_ABANDONED;
  }
  if (pair->taken) {
    return CALL_ENDPOINT_CLOSED;
  }
  if (!(flags_locked(pair) & OUTPUT_READY)) {
    return CALL_WOULD_BLOCK;
  }
  return pair->failed ? CALL_IO : CALL_OK;
}

static void take_execute(struct output_request *request)
{
  struct output_pair *pair = request->pair;
  struct capability_grant grants[2] = {0};
  struct file_object *backing = NULL;
  size_t count = 0;
  spin_lock(&pair->lock);
  request->status = take_status_locked(pair);
  if (request->status == CALL_OK) {
    pair->taking = true;
    if (pair->mode == OUTPUT_MODE_VALUE) {
      grants[0] = pair->value[0];
      grants[1] = pair->value[1];
      pair->value[0] = (struct capability_grant){0};
      pair->value[1] = (struct capability_grant){0};
      count = grants[1].object ? 2 : 1;
    } else {
      backing = pair->bytes;
      KASSERT(backing && object_retain(&backing->object));
      count = 1;
    }
  }
  spin_unlock(&pair->lock);
  if (request->status != CALL_OK) {
    return;
  }

  enum capability_result result = CAP_OK;
  if (backing) {
    /* Sealed admission and zero active calls give exclusive backing ownership.
     * Only the producer wrapper was delegated, never this mutable FILE grant. */
    backing->immutable_snapshot = true;
    result = capability_grant_retain(&backing->object, FILE_RIGHT_READ, 0, &grants[0]);
    if (result == CAP_OK) {
      result = forwarder_derive_grant(&grants[0], task_forwarder_operation());
    }
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, grants, count);
  }
  if (result == CAP_OK && !task_operation_start()) {
    result = CAP_DENIED;
  }
  struct file_object *released_backing = NULL;
  spin_lock(&pair->lock);
  pair->taking = false;
  if (result == CAP_OK) {
    pair->taken = true;
    released_backing = pair->bytes;
    pair->bytes = NULL;
  } else if (!backing && !pair->closed && !pair->failed) {
    /* Failed destination admission preserves the queued logical grants. */
    pair->value[0] = grants[0];
    pair->value[1] = grants[1];
    grants[0] = (struct capability_grant){0};
    grants[1] = (struct capability_grant){0};
  }
  spin_unlock(&pair->lock);
  if (result == CAP_OK) {
    handle_t handles[2] = {0};
    capability_install_reserved(&request->reservation, request->slots, grants, count, handles);
    request->taken = (struct output_take_reply){handles[0], handles[1]};
  }
  request->status = cap_status(result);
  capability_grant_release(&grants[0]);
  capability_grant_release(&grants[1]);
  if (backing) {
    object_release(&backing->object);
  }
  if (released_backing) {
    object_release(&released_backing->object);
  }
  readiness_notify();
}

void output_execute(struct output_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  if (request->operation == OUTPUT_REQUEST_CREATE) {
    create_execute(request);
  } else {
    KASSERT(request->operation == OUTPUT_REQUEST_TAKE);
    take_execute(request);
  }
}

static void prepare_request(struct output_request *request,
    enum output_request_operation operation, struct capability_reservation *reservation,
    const struct capability_reserved_slot *slots, struct output_pair *pair)
{
  request->operation = operation;
  request->reservation = *reservation;
  *reservation = (struct capability_reservation){0};
  for (size_t i = 0; i < request->reservation.count; ++i) {
    request->slots[i] = slots[i];
  }
  request->owner = process_current();
  request->account = NULL;
  request->pair = pair;
  request->created = (struct output_create_reply){0};
  request->taken = (struct output_take_reply){0};
  request->status = CALL_NO_MEMORY;
}

struct syscall_result output_create_call(struct retention_account *account, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (request_size || reply_capacity < sizeof(struct output_create_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct output_create_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  enum capability_result result = capability_request_reservation(2, &reservation, slots);
  if (result != CAP_OK) {
    return (struct syscall_result){cap_status(result), 0};
  }
  struct output_request *request =
      (struct output_request *)bsp_request_prepare(BSP_SERVICE_OUTPUT);
  prepare_request(request, OUTPUT_REQUEST_CREATE, &reservation, slots, NULL);
  request->account = account;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct output_create_reply reply = request->created;
  capability_reservation_release(&request->reservation, request->slots);
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    handle_t handles[] = {reply.collector, reply.producer};
    capability_close_returned(&process_current()->capabilities, handles, 2);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}

static struct syscall_result handoff(struct output_pair *pair,
    uintptr_t request_address, size_t request_size)
{
  struct output_handoff_request request;
  if (request_size != sizeof(request)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request, request_address, sizeof(request))) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  handle_t handles[2] = {request.value, request.observation};
  size_t count = request.observation ? 2 : 1;
  struct capability_reference references[2] = {0};
  enum capability_result result = CAP_OK;
  for (size_t i = 0; result == CAP_OK && i < count; ++i) {
    if ((i && handles[0] == handles[1]) || !handles[i]) {
      result = CAP_INVALID;
      break;
    }
    result = capability_acquire_metadata(&process_current()->capabilities,
        handles[i], &references[i]);
    if (result == CAP_OK) {
      enum object_type type = object_effective_type(references[i].object);
      if (type == OBJECT_OUTPUT_COLLECTOR || type == OBJECT_ENDPOINT_RECEIVER ||
          type == OBJECT_ENDPOINT_RECEIPT ||
          !wifi_grant_delegatable(references[i].object, references[i].rights)) {
        result = CAP_DENIED;
      }
    }
  }
  enum call_status status = cap_status(result);
  bool admitted = false;
  struct output_discard discard = {0};
  if (status == CALL_OK && !task_operation_start()) {
    status = CALL_DENIED;
  }
  if (status == CALL_OK) {
    spin_lock(&pair->lock);
    if (pair->closed || pair->sealed || pair->taken) {
      status = CALL_ENDPOINT_CLOSED;
    } else if (pair->failed) {
      status = CALL_IO;
    } else if (pair->mode != OUTPUT_MODE_EMPTY || pair->admitting) {
      /* Bytes, a value or another admitted handoff conflict in any order.
       * Calls still active in EMPTY mode change nothing and order first. */
      pair->failed = true;
      detach_output(pair, &discard);
      status = CALL_IO;
    } else {
      pair->admitting = true;
      ++pair->active;
      admitted = true;
    }
    spin_unlock(&pair->lock);
  }
  discard_output(&discard);
  if (admitted) {
    struct capability_grant grants[2] = {0};
    result = capability_grant_take(&process_current()->capabilities, handles,
        references, count, grants);
    status = cap_status(result);
    spin_lock(&pair->lock);
    KASSERT(pair->active && pair->admitting);
    --pair->active;
    pair->admitting = false;
    if (result == CAP_OK && !pair->closed && !pair->failed) {
      pair->mode = OUTPUT_MODE_VALUE;
      pair->value[0] = grants[0];
      pair->value[1] = grants[1];
      grants[0] = (struct capability_grant){0};
      grants[1] = (struct capability_grant){0};
      discard.bytes = pair->bytes;
      pair->bytes = NULL;
    }
    spin_unlock(&pair->lock);
    capability_grant_release(&grants[0]);
    capability_grant_release(&grants[1]);
    discard_output(&discard);
  }
  for (size_t i = 0; i < count; ++i) {
    capability_release(&references[i]);
  }
  readiness_notify();
  return (struct syscall_result){status, 0};
}

static struct syscall_result producer_call(struct output_pair *pair, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity, bool *append_probe)
{
  if (operation == FILE_HANDOFF) {
    return handoff(pair, request_address, request_size);
  }
  if (operation == FILE_OUTPUT_STATUS) {
    if (request_size || reply_capacity < sizeof(struct output_status_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    spin_lock(&pair->lock);
    struct output_status_reply reply = {pair->mode, flags_locked(pair)};
    spin_unlock(&pair->lock);
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (operation != FILE_WRITE && operation != FILE_APPEND && operation != FILE_RESIZE && operation != FILE_SIZE &&
      operation != FILE_INFO && operation != FILE_SYNC) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  bool output = false;
  bool zero_write = false;
  struct file_write_request request = {0};
  struct file_append_request append = {0};
  struct file_resize_request resize = {0};
  uint8_t data[FILE_WRITE_MAX_BYTES];
  if (operation == FILE_WRITE) {
    if (request_size < sizeof(request) || request_size > FILE_PAYLOAD_MAX) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&request, request_address, sizeof(request))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (request.size != request_size - sizeof(request) ||
        request.offset > SIZE_MAX - request.size ||
        reply_capacity < sizeof(struct file_write_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(request_address, request_size, USER_BUFFER_READ) ||
        !user_buffer_check(reply_address, sizeof(struct file_write_reply), USER_BUFFER_WRITE)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (!copy_from_user(data, request_address + sizeof(request), request.size)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    output = request.size != 0;
    zero_write = !output;
  } else if (operation == FILE_APPEND) {
    if (request_size < sizeof(append) || request_size > FILE_PAYLOAD_MAX) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&append, request_address, sizeof(append))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    if (append.reserved || append.size != request_size - sizeof(append) ||
        reply_capacity < sizeof(struct file_append_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!user_buffer_check(reply_address, sizeof(struct file_append_reply), USER_BUFFER_WRITE) ||
        !copy_from_user(data, request_address + sizeof(append), append.size)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    output = append.size != 0;
    zero_write = !output;
    *append_probe = zero_write;
  } else if (operation == FILE_RESIZE) {
    if (request_size != sizeof(resize)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    if (!copy_from_user(&resize, request_address, sizeof(resize))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    output = resize.size != 0;
  }
  if (!task_operation_start()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct file_object *backing = NULL;
  struct output_discard discard = {0};
  enum call_status status = CALL_OK;
  spin_lock(&pair->lock);
  if (pair->closed || pair->sealed || pair->taken) {
    status = CALL_ENDPOINT_CLOSED;
  } else if (pair->failed) {
    status = CALL_IO;
  } else if (pair->admitting && output) {
    /* Output conflicts with the admitted handoff in either order. Queries,
     * zero writes and RESIZE to zero order before it on the empty backing. */
    pair->failed = true;
    detach_output(pair, &discard);
    status = CALL_IO;
  } else if (pair->mode == OUTPUT_MODE_VALUE) {
    if (output || operation == FILE_RESIZE) {
      pair->failed = true;
      detach_output(pair, &discard);
      status = CALL_IO;
    } else if (!zero_write || operation == FILE_APPEND) {
      status = CALL_WRONG_TYPE;
    }
  } else if (!object_retain(&pair->bytes->object)) {
    status = CALL_LIMIT;
  } else {
    backing = pair->bytes;
    ++pair->active;
    if (output) {
      pair->mode = OUTPUT_MODE_BYTES;
    }
  }
  spin_unlock(&pair->lock);
  discard_output(&discard);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!backing) {
    struct file_append_reply reply = {0};
    size_t size = operation == FILE_APPEND ? sizeof(reply) : sizeof(reply.written);
    if (!copy_to_user(reply_address, &reply, size)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, size};
  }
  struct syscall_result result;
  if (operation == FILE_WRITE) {
    result = file_output_write(backing, &request, data, reply_address, reply_capacity);
  } else if (operation == FILE_APPEND) {
    result = file_output_append(backing, &append, data, reply_address, reply_capacity);
  } else if (operation == FILE_RESIZE) {
    result = file_output_resize(backing, resize.size);
  } else {
    result = file_call(backing, rights, operation,
        request_address, request_size, reply_address, reply_capacity);
  }
  spin_lock(&pair->lock);
  KASSERT(pair->active);
  --pair->active;
  bool mutation_failed = result.status != CALL_OK && !*append_probe &&
      (operation == FILE_WRITE || operation == FILE_APPEND || operation == FILE_RESIZE);
  /* Budget exhaustion already replaced the private prefix with a failed FILE.
   * Keep that typed value collectible; other admitted errors fail transport. */
  bool retained_failure = result.status == CALL_FILE_TOO_LARGE &&
      file_failure_status(backing) == CALL_FILE_TOO_LARGE;
  if (mutation_failed && !retained_failure) {
    pair->failed = true;
    detach_output(pair, &discard);
  }
  bool notify = pair->sealed && !pair->active;
  spin_unlock(&pair->lock);
  discard_output(&discard);
  object_release(&backing->object);
  if (notify) {
    readiness_notify();
  }
  return result;
}

struct syscall_result output_producer_call(struct output_end *end, uint64_t rights,
    uint64_t operation, uintptr_t request_address, size_t request_size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & FILE_RIGHT_WRITE)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct output_pair *pair = end->pair;
  bool append_probe = false;
  struct syscall_result result = producer_call(pair, rights, operation,
      request_address, request_size, reply_address, reply_capacity, &append_probe);
  if (result.status != CALL_OK && !append_probe &&
      (operation == FILE_WRITE || operation == FILE_APPEND || operation == FILE_RESIZE)) {
    struct output_discard discard = {0};
    spin_lock(&pair->lock);
    bool retained_failure = result.status == CALL_FILE_TOO_LARGE && pair->bytes &&
        file_failure_status(pair->bytes) == CALL_FILE_TOO_LARGE;
    /* Even rejected requests invalidate capture; no underlying mutation is
     * needed. An already admitted TAKE retains its ordinary completion rule. */
    if (!retained_failure) {
      pair->failed = true;
      detach_output(pair, &discard);
    }
    spin_unlock(&pair->lock);
    discard_output(&discard);
    readiness_notify();
  }
  return result;
}

struct syscall_result output_collector_call(struct output_end *end, uint64_t rights,
    uint64_t operation, size_t request_size, uintptr_t reply_address,
    size_t reply_capacity)
{
  struct output_pair *pair = end->pair;
  if (!(rights & OUTPUT_RIGHT_COLLECT) || pair->owner != process_current()) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (request_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (operation == OUTPUT_STATUS) {
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (reply_capacity < sizeof(struct output_status_reply)) {
      return (struct syscall_result){CALL_BAD_REQUEST, 0};
    }
    spin_lock(&pair->lock);
    struct output_status_reply reply = {pair->mode, flags_locked(pair)};
    spin_unlock(&pair->lock);
    if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
    return (struct syscall_result){CALL_OK, sizeof(reply)};
  }
  if (operation == OUTPUT_SEAL || operation == OUTPUT_CANCEL) {
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    if (operation == OUTPUT_CANCEL) {
      close_collector(pair);
    } else {
      spin_lock(&pair->lock);
      pair->sealed = true;
      spin_unlock(&pair->lock);
      readiness_notify();
    }
    return (struct syscall_result){CALL_OK, 0};
  }
  if (operation != OUTPUT_TAKE) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (reply_capacity < sizeof(struct output_take_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(reply_address, sizeof(struct output_take_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  spin_lock(&pair->lock);
  enum call_status status = take_status_locked(pair);
  size_t count = pair->value[1].object ? 2 : 1;
  spin_unlock(&pair->lock);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slots[2];
  enum capability_result cap = capability_request_reservation(count, &reservation, slots);
  if (cap != CAP_OK) {
    return (struct syscall_result){cap_status(cap), 0};
  }
  struct output_request *request =
      (struct output_request *)bsp_request_prepare(BSP_SERVICE_OUTPUT);
  prepare_request(request, OUTPUT_REQUEST_TAKE, &reservation, slots, pair);
  bsp_request_submit_and_wait(&request->request);
  status = request->status;
  struct output_take_reply reply = request->taken;
  capability_reservation_release(&request->reservation, request->slots);
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_to_user(reply_address, &reply, sizeof(reply))) {
    handle_t handles[2] = {reply.value, reply.observation};
    capability_close_returned(&process_current()->capabilities, handles, reply.observation ? 2 : 1);
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
