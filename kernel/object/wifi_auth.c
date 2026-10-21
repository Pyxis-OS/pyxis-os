#include <abi/file.h>
#include <abi/wait.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/process.h>
#include <kernel/object/wifi_auth.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/user/readiness.h>
#include <kernel/user_memory.h>
#include <kernel/wifi.h>
#include <kernel/wifi/ax200_transport.h>

/* Tunable copied-event/instance budgets, not protocol guarantees. */
#define WIFI_AUTH_QUEUE 8u
#define WIFI_AUTH_CONTEXTS 8u
#define WIFI_AUTH_COMMAND_MS 5000u
#define WIFI_AUTH_PN_MAX UINT64_C(0xffffffffffff)

struct auth_event {
  struct wifi_auth_event event;
  struct capability_grant credential;
};

struct wifi_auth {
  struct kernel_object object;
  /* IF=0, no grant callbacks, allocation or driver calls under this guard. */
  struct spinlock lock;
  struct wifi_auth *next, **previous; /* BSP-only weak registry. */
  struct kernel_object *radio;
  struct process_control *owner;
  struct forwarder_operation lineage; /* Pending forever: CUT closes the broker. */
  size_t runners;
  bool closed;
  enum call_status status;
  struct wifi_connect_work *work; /* Borrowed until association retire. */
  uint64_t generation, deadline_ns;
  struct wifi_auth_request *pending;
  size_t head, count;
  struct auth_event events[WIFI_AUTH_QUEUE];
};

static struct wifi_auth *contexts;
static size_t context_count;

static void require_bsp(void)
{
  KASSERT(cpu_current() == cpu_bsp() && !cpu_interrupts_enabled());
}

static enum call_status cap_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_FULL:
  case CAP_LIMIT: return CALL_LIMIT;
  default: return CALL_BAD_REQUEST;
  }
}

static bool includes_lineage(const struct forwarder_operation *outer,
    const struct forwarder_operation *inner)
{
  if (!outer || !inner->count || outer->count < inner->count) {
    return false;
  }
  for (size_t i = 0; i < inner->count; ++i) {
    bool found = false;
    for (size_t j = 0; j < outer->count; ++j) {
      if (inner->nodes[i] == outer->nodes[j]) {
        found = true;
        break;
      }
    }
    if (!found) {
      return false;
    }
  }
  return true;
}

static void close_auth(struct wifi_auth *auth, enum call_status status)
{
  struct capability_grant detached[WIFI_AUTH_QUEUE] = {0};
  spin_lock(&auth->lock);
  bool changed = !auth->closed;
  if (changed) {
    auth->closed = true;
    auth->status = status;
    for (size_t i = 0; i < WIFI_AUTH_QUEUE; ++i) {
      detached[i] = auth->events[i].credential;
      memzero_explicit(&auth->events[i], sizeof(auth->events[i]));
    }
    auth->head = auth->count = 0;
  }
  spin_unlock(&auth->lock);
  for (size_t i = 0; i < WIFI_AUTH_QUEUE; ++i) {
    capability_grant_release(&detached[i]);
  }
  if (changed) {
    /* Do not dereference the weak work pointer from an AP close callback. */
    ax200_transport_notify();
    readiness_notify();
  }
}

static void destroy_auth(struct kernel_object *object)
{
  require_bsp();
  struct wifi_auth *auth = (void *)object;
  KASSERT(!auth->runners && !auth->work && !auth->pending);
  if (auth->previous) {
    *auth->previous = auth->next;
    if (auth->next) {
      auth->next->previous = auth->previous;
    }
    KASSERT(context_count);
    --context_count;
  }
  close_auth(auth, CALL_ENDPOINT_CLOSED);
  forwarder_operation_release(&auth->lineage);
  if (auth->owner) {
    object_release(&auth->owner->object);
  }
  object_release(auth->radio);
  memzero_explicit(auth, sizeof(*auth));
  kfree(auth);
}

bool wifi_auth_grant_retain(struct kernel_object *object, uint64_t rights)
{
  struct wifi_auth *auth = (void *)object;
  spin_lock(&auth->lock);
  bool valid = !(rights & WIFI_AUTH_RIGHT_RUN) ||
      (!auth->closed && !auth->owner && auth->runners != SIZE_MAX);
  if (valid && (rights & WIFI_AUTH_RIGHT_RUN)) {
    ++auth->runners;
  }
  spin_unlock(&auth->lock);
  return valid;
}

void wifi_auth_grant_release(struct kernel_object *object, uint64_t rights)
{
  if (!(rights & WIFI_AUTH_RIGHT_RUN)) {
    return;
  }
  struct wifi_auth *auth = (void *)object;
  spin_lock(&auth->lock);
  KASSERT(auth->runners);
  bool last = !--auth->runners;
  spin_unlock(&auth->lock);
  if (last) {
    close_auth(auth, CALL_ENDPOINT_CLOSED);
  }
}

bool wifi_auth_delegatable(const struct kernel_object *object, uint64_t rights)
{
  if (!(rights & WIFI_AUTH_RIGHT_RUN)) {
    return true;
  }
  struct kernel_object *target = NULL;
  if (forwarder_snapshot((struct kernel_object *)object, &target, NULL) != CAP_OK) {
    return false;
  }
  struct wifi_auth *auth = (void *)target;
  bool valid = false;
  if (target->type == OBJECT_WIFI_AUTH) {
    spin_lock(&auth->lock);
    valid = !auth->owner && !auth->closed;
    spin_unlock(&auth->lock);
  }
  object_release(target);
  return valid;
}

bool wifi_auth_owned(const struct kernel_object *object, const struct process *owner)
{
  struct wifi_auth *auth = (void *)object;
  spin_lock(&auth->lock);
  bool valid = owner && owner->control && auth->owner == owner->control;
  spin_unlock(&auth->lock);
  return valid;
}

void wifi_auth_process_exit(struct process *process)
{
  require_bsp();
  /* Reaping repeats session release after detaching the process control. An
   * unclaimed broker has no owner and must not match that absent identity. */
  if (!process->control) return;
  for (struct wifi_auth *auth = contexts; auth; auth = auth->next) {
    if (auth->owner == process->control) {
      close_auth(auth, CALL_ENDPOINT_CLOSED);
    }
  }
}

void wifi_auth_revocation_notify(void)
{
  /* CUT may originate on an AP. Logical RUN release closes the broker; the
   * worker rechecks its independently captured root before further effects. */
  ax200_transport_notify();
}

uint64_t wifi_auth_ready(struct kernel_object *object, uint64_t events)
{
  struct wifi_auth *auth = (void *)object;
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&auth->lock);
  uint64_t ready = auth->closed ? WAIT_CLOSED : auth->count ? events & WAIT_READABLE : 0;
  spin_unlock(&auth->lock);
  cpu_restore_interrupts(flags);
  return ready;
}

static void open_execute(struct wifi_auth_request *request)
{
  const struct forwarder_operation *lineage = request->request.operation;
  request->status = CALL_DENIED;
  if (!lineage || !lineage->count) {
    return;
  }
  for (struct wifi_auth *auth = contexts; auth; auth = auth->next) {
    spin_lock(&auth->lock);
    bool live = !auth->closed;
    spin_unlock(&auth->lock);
    if (live && includes_lineage(lineage, &auth->lineage) &&
        includes_lineage(&auth->lineage, lineage)) {
      request->status = CALL_ALREADY_EXISTS;
      return;
    }
  }
  if (context_count == WIFI_AUTH_CONTEXTS) {
    request->status = CALL_LIMIT;
    return;
  }
  struct wifi_auth *auth = kmalloc(sizeof(*auth));
  request->status = CALL_NO_MEMORY;
  if (!auth) {
    return;
  }
  *auth = (struct wifi_auth){.radio = request->radio, .status = CALL_OK};
  atomic_init(&auth->lock.locked, false);
  if (!object_retain(auth->radio)) {
    kfree(auth);
    request->status = CALL_LIMIT;
    return;
  }
  object_init(&auth->object, OBJECT_WIFI_AUTH, destroy_auth);
  request->status = forwarder_operation_clone(&auth->lineage, lineage);
  struct capability_grant grants[2] = {0};
  if (request->status == CALL_OK) {
    request->status = cap_status(capability_grant_retain(&auth->object, WIFI_AUTH_RIGHT_RUN, 0, &grants[0]));
  }
  if (request->status == CALL_OK) {
    request->status = cap_status(capability_grant_retain(&auth->object, WIFI_AUTH_RIGHT_OBSERVE, 0, &grants[1]));
  }
  for (size_t i = 0; request->status == CALL_OK && i < 2; ++i) {
    request->status = cap_status(forwarder_derive_grant(&grants[i], lineage));
  }
  if (request->status == CALL_OK) {
    request->status = cap_status(capability_validate_grants(request->reservation.table, grants, 2));
  }
  if (request->status == CALL_OK && (!task_operation_start() || task_wait_stop_requested(request->request.wait))) {
    request->status = task_wait_stop_requested(request->request.wait) ? CALL_ENDPOINT_CLOSED : CALL_DENIED;
  }
  if (request->status == CALL_OK) {
    auth->next = contexts;
    auth->previous = &contexts;
    if (contexts) {
      contexts->previous = &auth->next;
    }
    contexts = auth;
    ++context_count;
    handle_t handles[2];
    capability_install_reserved(&request->reservation, request->slots, grants, 2, handles);
    request->reply.opened = (struct wifi_auth_open_reply){handles[0], handles[1]};
  }
  capability_grant_release(&grants[0]);
  capability_grant_release(&grants[1]);
  object_release(&auth->object);
}

enum call_status wifi_auth_select(const struct forwarder_operation *lineage, struct wifi_auth **result)
{
  require_bsp();
  *result = NULL;
  for (struct wifi_auth *auth = contexts; auth; auth = auth->next) {
    if (forwarder_operation_denied(&auth->lineage) || !includes_lineage(lineage, &auth->lineage)) {
      continue;
    }
    spin_lock(&auth->lock);
    bool valid = !auth->closed && auth->runners && auth->owner;
    if (valid && !*result) {
      valid = object_retain(&auth->object);
      if (valid) {
        *result = auth;
      }
      spin_unlock(&auth->lock);
      continue;
    }
    spin_unlock(&auth->lock);
    if (!valid) {
      continue;
    }
    if (*result) {
      object_release(&(*result)->object);
      *result = NULL;
      return CALL_BUSY;
    }
  }
  if (!*result) {
    return CALL_UNAVAILABLE;
  }
  return CALL_OK;
}

void wifi_auth_release(struct wifi_auth *auth)
{
  if (auth) {
    object_release(&auth->object);
  }
}

static enum call_status queue_event(struct wifi_auth *auth, const struct wifi_auth_event *event,
    struct capability_grant *credential)
{
  spin_lock(&auth->lock);
  enum call_status status = auth->closed ? auth->status : auth->count == WIFI_AUTH_QUEUE ? CALL_INPUT_LOST : CALL_OK;
  bool notify = status == CALL_OK && !auth->count;
  if (status == CALL_OK) {
    struct auth_event *entry = &auth->events[(auth->head + auth->count) % WIFI_AUTH_QUEUE];
    entry->event = *event;
    if (credential) {
      entry->credential = *credential;
      *credential = (struct capability_grant){0};
    }
    ++auth->count;
  }
  spin_unlock(&auth->lock);
  if (status == CALL_INPUT_LOST) {
    close_auth(auth, status);
  } else if (notify) {
    readiness_notify();
  }
  return status;
}

enum call_status wifi_auth_begin(struct wifi_auth *auth, struct wifi_connect_work *work,
    uint64_t generation, uint64_t deadline, const struct wifi_auth_begin *begin,
    const struct capability_grant *credential)
{
  require_bsp();
  if (!auth || !work || !generation || !begin || begin->ssid_bytes > 32 ||
      begin->ap_rsn_bytes > WIFI_AUTH_RSN_BYTES || begin->association_rsn_bytes > WIFI_AUTH_RSN_BYTES ||
      begin->ap_rsn_bytes < 2 || begin->association_rsn_bytes < 2 ||
      begin->ap_rsn[0] != 48 || begin->association_rsn[0] != 48 ||
      (size_t)begin->ap_rsn[1] + 2 != begin->ap_rsn_bytes ||
      (size_t)begin->association_rsn[1] + 2 != begin->association_rsn_bytes ||
      !credential || !credential->object || credential->rights != FILE_RIGHT_READ || credential->transport) {
    return CALL_BAD_REQUEST;
  }
  if (forwarder_operation_denied(&auth->lineage)) {
    return CALL_DENIED;
  }
  struct capability_grant grant = {0};
  enum call_status status = cap_status(capability_grant_derive(credential->object, credential->rights,
      credential->transport, credential->restrictions, &grant));
  if (status != CALL_OK) {
    return status;
  }
  spin_lock(&auth->lock);
  status = auth->closed ? auth->status : auth->work || auth->pending ? CALL_BUSY :
      generation <= auth->generation ? CALL_BAD_REQUEST : CALL_OK;
  if (status == CALL_OK) {
    auth->work = work;
    auth->generation = generation;
    auth->deadline_ns = deadline;
  }
  spin_unlock(&auth->lock);
  if (status == CALL_OK) {
    struct wifi_auth_event event = {.generation = generation, .deadline_ns = deadline,
        .kind = WIFI_AUTH_BEGIN, .length = sizeof(*begin)};
    event.body.begin = *begin;
    status = queue_event(auth, &event, &grant);
    memzero_explicit(&event, sizeof(event));
  }
  capability_grant_release(&grant);
  return status;
}

enum call_status wifi_auth_receive(struct wifi_auth *auth, uint64_t generation,
    const void *eapol, size_t bytes, uint32_t flags)
{
  require_bsp();
  if (!auth || !eapol || bytes < 4 || bytes > WIFI_AUTH_EAPOL_BYTES || (flags & ~WIFI_AUTH_EVENT_ENCRYPTED)) {
    return CALL_BAD_REQUEST;
  }
  spin_lock(&auth->lock);
  enum call_status status = auth->closed ? auth->status :
      !auth->work || auth->generation != generation ? CALL_ENDPOINT_CLOSED :
      auth->count == WIFI_AUTH_QUEUE ? CALL_WOULD_BLOCK : CALL_OK;
  bool notify = status == CALL_OK && !auth->count;
  if (status == CALL_OK) {
    struct auth_event *entry = &auth->events[(auth->head + auth->count) % WIFI_AUTH_QUEUE];
    entry->event = (struct wifi_auth_event){.generation = generation, .kind = WIFI_AUTH_EAPOL,
        .length = bytes, .flags = flags};
    memcpy(entry->event.body.data, eapol, bytes);
    ++auth->count;
  }
  spin_unlock(&auth->lock);
  /* A radio burst can exceed this bounded queue before the worker can run.
   * Drop newest and count at the RX caller; do not close the auth service. */
  if (notify) {
    readiness_notify();
  }
  return status;
}

void wifi_auth_retire(struct wifi_auth *auth, uint64_t generation, enum call_status status)
{
  require_bsp();
  struct capability_grant detached[WIFI_AUTH_QUEUE] = {0};
  spin_lock(&auth->lock);
  bool active = auth->work && auth->generation == generation;
  struct wifi_auth_request *request = active ? auth->pending : NULL;
  if (active) {
    KASSERT(!request || !request->in_flight);
    auth->pending = NULL;
    auth->work = NULL;
    for (size_t i = 0; i < WIFI_AUTH_QUEUE; ++i) {
      detached[i] = auth->events[i].credential;
      memzero_explicit(&auth->events[i], sizeof(auth->events[i]));
    }
    auth->head = auth->count = 0;
  }
  spin_unlock(&auth->lock);
  for (size_t i = 0; i < WIFI_AUTH_QUEUE; ++i) {
    capability_grant_release(&detached[i]);
  }
  if (request) {
    request->status = status == CALL_OK ? CALL_ENDPOINT_CLOSED : status;
    request->auth = NULL;
    memzero_explicit(&request->input, sizeof(request->input));
    bsp_request_complete(&request->request);
  }
  if (active) {
    struct wifi_auth_event event = {.kind = WIFI_AUTH_DISASSOC, .generation = generation, .status = status};
    queue_event(auth, &event, NULL);
  }
}

enum call_status wifi_auth_stop_reason(struct wifi_auth *auth, uint64_t generation)
{
  if (!auth) {
    return CALL_UNAVAILABLE;
  }
  uint64_t flags = cpu_save_interrupts();
  if (forwarder_operation_denied(&auth->lineage)) {
    close_auth(auth, CALL_DENIED);
  }
  spin_lock(&auth->lock);
  enum call_status status = auth->closed ? auth->status : generation && auth->generation != generation ?
      CALL_ENDPOINT_CLOSED : CALL_OK;
  spin_unlock(&auth->lock);
  cpu_restore_interrupts(flags);
  return status;
}

struct wifi_auth_request *wifi_auth_worker_take(struct wifi_connect_work *work, uint64_t generation)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  uint64_t flags = cpu_save_interrupts();
  struct wifi_auth_request *request = NULL;
  enum call_status status = CALL_OK;
  for (struct wifi_auth *auth = contexts; auth; auth = auth->next) {
    spin_lock(&auth->lock);
    if (auth->work == work && auth->generation == generation && auth->pending && !auth->pending->in_flight) {
      request = auth->pending;
      request->in_flight = true;
      status = auth->closed ? auth->status : CALL_OK;
    }
    spin_unlock(&auth->lock);
    if (request) {
      break;
    }
  }
  cpu_restore_interrupts(flags);
  if (request && status == CALL_OK && (task_wait_stop_requested(request->request.wait) ||
      arch_monotonic_ns() >= request->deadline_ns)) {
    status = task_wait_stop_requested(request->request.wait) ? CALL_ENDPOINT_CLOSED : CALL_TIMED_OUT;
  }
  if (request && status != CALL_OK) {
    wifi_auth_worker_complete(request, status);
    return NULL;
  }
  return request;
}

void wifi_auth_worker_complete(struct wifi_auth_request *request, enum call_status status)
{
  KASSERT(cpu_current() == cpu_bsp() && cpu_interrupts_enabled());
  uint64_t flags = cpu_save_interrupts();
  struct wifi_auth *auth = request->auth;
  spin_lock(&auth->lock);
  KASSERT(auth->pending == request && request->in_flight);
  auth->pending = NULL;
  spin_unlock(&auth->lock);
  request->status = status;
  request->auth = NULL;
  memzero_explicit(&request->input, sizeof(request->input));
  bsp_request_complete(&request->request);
  cpu_restore_interrupts(flags);
}

static void read_execute(struct wifi_auth_request *request)
{
  struct wifi_auth *auth = request->auth;
  struct capability_grant credential = {0};
  size_t head;
  spin_lock(&auth->lock);
  request->status = auth->closed ? auth->status : !auth->count ? CALL_WOULD_BLOCK : CALL_OK;
  if (request->status != CALL_OK) {
    spin_unlock(&auth->lock);
    return;
  }
  head = auth->head;
  struct auth_event *entry = &auth->events[head];
  credential = entry->credential;
  bool retained = !credential.object || object_retain(credential.object);
  spin_unlock(&auth->lock);
  request->status = retained ? CALL_OK : CALL_LIMIT;
  if (request->status == CALL_OK && credential.object) {
    request->status = cap_status(capability_validate_grants(request->reservation.table, &credential, 1));
  }
  if (request->status == CALL_OK && (!task_operation_start() || task_wait_stop_requested(request->request.wait))) {
    request->status = task_wait_stop_requested(request->request.wait) ? CALL_ENDPOINT_CLOSED : CALL_DENIED;
  }
  struct capability_grant taken = {0};
  if (request->status == CALL_OK) {
    spin_lock(&auth->lock);
    request->status = auth->closed ? auth->status : !auth->count || auth->head != head ? CALL_WOULD_BLOCK : CALL_OK;
    if (request->status == CALL_OK) {
      entry = &auth->events[head];
      request->reply.event = entry->event;
      taken = entry->credential;
      memzero_explicit(entry, sizeof(*entry));
      auth->head = (head + 1) % WIFI_AUTH_QUEUE;
      --auth->count;
    }
    spin_unlock(&auth->lock);
  }
  if (credential.object && retained) {
    object_release(credential.object);
  }
  if (taken.object) {
    capability_install_reserved(&request->reservation, request->slots, &taken, 1,
        &request->reply.event.credential);
  }
}

void wifi_auth_execute(struct wifi_auth_request *request)
{
  require_bsp();
  if (!request->auth) {
    open_execute(request);
    goto complete;
  }
  struct wifi_auth *auth = request->auth;
  if (forwarder_operation_denied(&auth->lineage)) {
    close_auth(auth, CALL_DENIED);
  }
  if (request->operation == WIFI_AUTH_STATUS) {
    spin_lock(&auth->lock);
    request->reply.status = (struct wifi_auth_status_reply){
      .generation = auth->generation,
      .flags = (auth->owner ? WIFI_AUTH_CLAIMED : 0) | (auth->closed ? WIFI_AUTH_CLOSED : 0) |
          (auth->work ? WIFI_AUTH_ACTIVE : 0) | (auth->pending ? WIFI_AUTH_BUSY : 0),
      .queued = auth->count, .status = auth->status,
    };
    spin_unlock(&auth->lock);
    request->status = CALL_OK;
    goto complete;
  }
  if (!task_operation_start() || task_wait_stop_requested(request->request.wait)) {
    request->status = task_wait_stop_requested(request->request.wait) ? CALL_ENDPOINT_CLOSED : CALL_DENIED;
    goto complete;
  }
  spin_lock(&auth->lock);
  request->status = auth->closed ? auth->status : CALL_OK;
  if (request->status == CALL_OK && request->operation == WIFI_AUTH_CLAIM) {
    if (auth->owner && auth->owner != request->owner->control) {
      request->status = CALL_DENIED;
    } else if (!auth->owner) {
      if (object_retain(&request->owner->control->object)) {
        auth->owner = request->owner->control;
      } else {
        request->status = CALL_LIMIT;
      }
    }
    spin_unlock(&auth->lock);
    goto complete;
  }
  if (request->status == CALL_OK && auth->owner != request->owner->control) {
    request->status = CALL_DENIED;
  }
  spin_unlock(&auth->lock);
  if (request->status != CALL_OK) {
    goto complete;
  }
  if (request->operation == WIFI_AUTH_READ) {
    read_execute(request);
    goto complete;
  }
  spin_lock(&auth->lock);
  request->status = auth->closed ? auth->status : !auth->work || request->generation != auth->generation ?
      CALL_ENDPOINT_CLOSED : auth->pending ? CALL_BUSY : CALL_OK;
  if (request->status == CALL_OK) {
    auth->pending = request;
    request->deadline_ns = task_deadline_after_ms(WIFI_AUTH_COMMAND_MS);
    request->owner = NULL;
    request->radio = NULL;
    request->request.state = BSP_REQUEST_FORWARDED;
  }
  spin_unlock(&auth->lock);
  if (request->status == CALL_OK) {
    ax200_transport_notify();
    return;
  }
complete:
  capability_reservation_release(&request->reservation, request->slots);
  memzero_explicit(&request->input, sizeof(request->input));
  request->owner = NULL;
  request->radio = NULL;
  request->auth = NULL;
}

static bool failure_status_valid(uint32_t status)
{
  return status == CALL_DENIED || status == CALL_IO || status == CALL_CONNECTION_REFUSED ||
      status == CALL_CONNECTION_RESET || status == CALL_TIMED_OUT || status == CALL_ABANDONED ||
      status == CALL_UNAVAILABLE || status == CALL_INPUT_LOST || status == CALL_OUTCOME_UNKNOWN;
}

static enum call_status validate_command(struct wifi_auth_request *request)
{
  switch (request->operation) {
  case WIFI_AUTH_SEND: {
    struct wifi_auth_send_request *send = &request->input.send;
    request->generation = send->generation;
    if (!send->generation || send->length < 4 || send->length > WIFI_AUTH_EAPOL_BYTES ||
        (send->flags & ~WIFI_AUTH_EAPOL_ENCRYPT) ||
        ((unsigned)send->data[2] << 8 | send->data[3]) != send->length - 4) {
      return CALL_BAD_REQUEST;
    }
    break;
  }
  case WIFI_AUTH_KEY: {
    struct wifi_auth_key_request *key = &request->input.key;
    request->generation = key->generation;
    if (!key->generation || key->reserved || key->group > 1 || key->key_id > 3 ||
        (!key->group && key->key_id) || key->receive_pn >= WIFI_AUTH_PN_MAX ||
        (key->action != WIFI_AUTH_KEY_INSTALL && key->action != WIFI_AUTH_KEY_REMOVE)) {
      return CALL_BAD_REQUEST;
    }
    break;
  }
  case WIFI_AUTH_AUTHORIZE:
    request->generation = request->input.authorize.generation;
    if (!request->generation || request->input.authorize.enabled > 1 || request->input.authorize.reserved) {
      return CALL_BAD_REQUEST;
    }
    break;
  case WIFI_AUTH_FAIL:
    request->generation = request->input.fail.generation;
    if (!request->generation || request->input.fail.reserved || !failure_status_valid(request->input.fail.status)) {
      return CALL_BAD_REQUEST;
    }
    break;
  default:
    break;
  }
  return CALL_OK;
}

static struct syscall_result exchange(struct wifi_auth_request *request, uintptr_t output, size_t output_size)
{
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  if (status == CALL_OK && output_size) {
    handle_t returned[2] = {0};
    size_t count = 0;
    if (request->operation == WIFI_OPEN_AUTH) {
      returned[count++] = request->reply.opened.auth;
      returned[count++] = request->reply.opened.observation;
    } else if (request->operation == WIFI_AUTH_READ && request->reply.event.credential) {
      returned[count++] = request->reply.event.credential;
    }
    bool copied = count ? copy_returned_handles_to_user(output, &request->reply, output_size, returned, count) :
        copy_to_user(output, &request->reply, output_size);
    if (!copied) {
      status = CALL_BAD_BUFFER;
    }
  }
  memzero_explicit(&request->reply, sizeof(request->reply));
  memzero_explicit(&request->input, sizeof(request->input));
  bsp_request_release(&request->request);
  return (struct syscall_result){status, status == CALL_OK ? output_size : 0};
}

struct syscall_result wifi_auth_open_call(struct kernel_object *radio, uint64_t rights,
    size_t size, uintptr_t output, size_t capacity)
{
  if (!(rights & WIFI_RIGHT_SUPPLICANT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (size || capacity < sizeof(struct wifi_auth_open_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(output, sizeof(struct wifi_auth_open_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation;
  struct capability_reserved_slot slots[2];
  enum call_status status = cap_status(capability_request_reservation(2, &reservation, slots));
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, slots);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct wifi_auth_request *request = (void *)bsp_request_prepare(BSP_SERVICE_WIFI_AUTH);
  request->radio = radio;
  request->owner = process_current();
  request->operation = WIFI_OPEN_AUTH;
  request->reservation = reservation;
  memcpy(request->slots, slots, sizeof(slots));
  return exchange(request, output, sizeof(struct wifi_auth_open_reply));
}

struct syscall_result wifi_auth_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t input, size_t size, uintptr_t output, size_t capacity)
{
  uint64_t required = operation == WIFI_AUTH_STATUS ? WIFI_AUTH_RIGHT_RUN | WIFI_AUTH_RIGHT_OBSERVE : WIFI_AUTH_RIGHT_RUN;
  if (!(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (!operation || operation > WIFI_AUTH_FAIL) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  size_t expected = 0;
  if (operation == WIFI_AUTH_SEND) {
    expected = sizeof(struct wifi_auth_send_request);
  } else if (operation == WIFI_AUTH_KEY) {
    expected = sizeof(struct wifi_auth_key_request);
  } else if (operation == WIFI_AUTH_AUTHORIZE) {
    expected = sizeof(struct wifi_auth_authorize_request);
  } else if (operation == WIFI_AUTH_FAIL) {
    expected = sizeof(struct wifi_auth_fail_request);
  }
  if (expected) {
    expected -= sizeof(struct message_header);
  }
  size_t output_size = operation == WIFI_AUTH_STATUS ? sizeof(struct wifi_auth_status_reply) :
      operation == WIFI_AUTH_READ ? sizeof(struct wifi_auth_event) : 0;
  if (size != expected || capacity < output_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (output_size && !user_buffer_check(output, output_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot = {0};
  if (operation == WIFI_AUTH_READ) {
    enum call_status status = cap_status(capability_request_reservation(1, &reservation, &slot));
    if (status != CALL_OK) {
      return (struct syscall_result){status, 0};
    }
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct wifi_auth_request *request = (void *)bsp_request_prepare(BSP_SERVICE_WIFI_AUTH);
  request->auth = (void *)object;
  request->owner = process_current();
  request->operation = operation;
  request->reservation = reservation;
  request->slots[0] = slot;
  enum call_status status = CALL_OK;
  if (expected && !copy_from_user((uint8_t *)&request->input + sizeof(struct message_header), input, expected)) {
    status = CALL_BAD_BUFFER;
  }
  if (status == CALL_OK) {
    status = validate_command(request);
  }
  if (status != CALL_OK) {
    capability_reservation_release(&request->reservation, request->slots);
    memzero_explicit(&request->input, sizeof(request->input));
    bsp_request_cancel_prepared(&request->request);
    return (struct syscall_result){status, 0};
  }
  return exchange(request, output, output_size);
}
