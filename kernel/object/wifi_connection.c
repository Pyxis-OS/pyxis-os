#include <abi/wait.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/process.h>
#include <kernel/object/wifi_connection.h>
#include <kernel/object/wifi_auth.h>
#include <kernel/object/file.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/user/readiness.h>
#include <kernel/user_memory.h>
#include <kernel/wifi.h>

struct wifi_attempt;
struct wifi_link {
  struct kernel_object object;
  /* IF=0, no allocation, grant callbacks or driver calls under this guard. */
  struct spinlock lock;
  size_t controllers;
  bool closed, finished, retired, admitted;
  enum call_status status;
  uint32_t flags;
  struct kernel_object *radio;
  struct wifi_attempt *attempt; /* Driver-owned reference through setup callback. */
  struct wifi_connect_work work;
  struct forwarder_operation lineage;
  /* Setup/drain only. A published link must not keep the launching subgroup alive. */
  struct execution_group *cleanup_group;
  struct execution_group *retirement_group; /* Guarded; final-owner drain attribution. */
};

struct wifi_attempt {
  struct kernel_object object;
  struct spinlock lock;
  struct process_control *owner;
  struct wifi_attempt *next, **previous; /* BSP pending setup list. */
  struct wifi_link *link; /* Storage, independent of the result's logical grant. */
  struct capability_grant result;
  size_t grants;
  enum call_status status;
  bool complete, taken, closed;
};

static struct wifi_attempt *attempts;
static uint64_t next_generation; /* BSP admission; never reused or wrapped. */

static enum call_status cap_status(enum capability_result result)
{
  switch (result) {
  case CAP_OK: return CALL_OK;
  case CAP_BAD_HANDLE: return CALL_BAD_HANDLE;
  case CAP_DENIED: return CALL_DENIED;
  case CAP_NO_MEMORY: return CALL_NO_MEMORY;
  case CAP_LIMIT:
  case CAP_FULL: return CALL_LIMIT;
  default: return CALL_BAD_REQUEST;
  }
}

static struct wifi_link *link_from_work(struct wifi_connect_work *work)
{
  return (void *)((uint8_t *)work - offsetof(struct wifi_link, work));
}

static void disconnect(struct wifi_link *link)
{
  struct execution_group *group = object_cleanup_defer();
  spin_lock(&link->lock);
  bool adopted = group && link->admitted && !link->retired && !link->retirement_group;
  if (adopted) {
    link->retirement_group = group;
  }
  spin_unlock(&link->lock);
  if (group && !adopted) {
    execution_group_cleanup_end(group);
  }
  atomic_store_explicit(&link->work.cancel, true, memory_order_release);
  wifi_driver_disconnect(&link->work);
}

static void destroy_link(struct kernel_object *object)
{
  struct wifi_link *link = (void *)object;
  KASSERT(!link->controllers && !link->attempt && !link->cleanup_group && !link->retirement_group);
  capability_grant_release(&link->work.credential);
  wifi_auth_release(link->work.auth);
  forwarder_operation_release(&link->lineage);
  object_release(link->radio);
  memzero_explicit(&link->work.target, sizeof(link->work.target));
  kfree(link);
}

static void destroy_attempt(struct kernel_object *object)
{
  struct wifi_attempt *attempt = (void *)object;
  KASSERT(!attempt->grants && !attempt->previous);
  capability_grant_release(&attempt->result);
  object_release(&attempt->link->object);
  object_release(&attempt->owner->object);
  kfree(attempt);
}

bool wifi_connection_owned(const struct kernel_object *object, const struct process *owner)
{
  return object->type == OBJECT_WIFI_CONNECT && owner &&
      ((const struct wifi_attempt *)object)->owner == owner->control;
}

bool wifi_connection_grant_retain(struct kernel_object *object, uint64_t rights)
{
  if (object->type == OBJECT_WIFI_LINK) {
    if (!(rights & WIFI_LINK_RIGHT_CONTROL)) {
      return true;
    }
    struct wifi_link *link = (void *)object;
    spin_lock(&link->lock);
    bool allowed = !link->closed && link->controllers != SIZE_MAX;
    if (allowed) {
      ++link->controllers;
    }
    spin_unlock(&link->lock);
    return allowed;
  }
  struct wifi_attempt *attempt = (void *)object;
  spin_lock(&attempt->lock);
  bool allowed = !attempt->closed && attempt->grants != SIZE_MAX;
  if (allowed) {
    ++attempt->grants;
  }
  spin_unlock(&attempt->lock);
  return allowed;
}

void wifi_connection_grant_release(struct kernel_object *object, uint64_t rights)
{
  bool stop;
  struct wifi_link *link;
  if (object->type == OBJECT_WIFI_LINK) {
    if (!(rights & WIFI_LINK_RIGHT_CONTROL)) {
      return;
    }
    link = (void *)object;
    spin_lock(&link->lock);
    KASSERT(link->controllers);
    stop = !--link->controllers;
    if (stop) {
      link->closed = true;
    }
    spin_unlock(&link->lock);
  } else {
    struct wifi_attempt *attempt = (void *)object;
    link = attempt->link;
    spin_lock(&attempt->lock);
    KASSERT(attempt->grants);
    stop = !--attempt->grants && !attempt->taken;
    if (!attempt->grants) {
      attempt->closed = true;
    }
    spin_unlock(&attempt->lock);
  }
  if (stop) {
    disconnect(link);
    readiness_notify();
  }
}

uint64_t wifi_connection_ready(struct kernel_object *object, uint64_t events)
{
  uint64_t flags = cpu_save_interrupts(), ready;
  if (object->type == OBJECT_WIFI_CONNECT) {
    struct wifi_attempt *attempt = (void *)object;
    spin_lock(&attempt->lock);
    ready = !attempt->complete ? 0 : attempt->closed || attempt->taken ?
        WAIT_CLOSED : events & WAIT_COMPLETE;
    spin_unlock(&attempt->lock);
  } else {
    struct wifi_link *link = (void *)object;
    spin_lock(&link->lock);
    ready = link->retired ? WAIT_CLOSED : 0;
    spin_unlock(&link->lock);
  }
  cpu_restore_interrupts(flags);
  return ready;
}

void wifi_connection_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  for (struct wifi_attempt *attempt = attempts; attempt; attempt = attempt->next) {
    if (attempt->owner == process->control) {
      disconnect(attempt->link);
    }
  }
}

enum call_status wifi_connect_stop_reason(struct wifi_connect_work *work)
{
  KASSERT(arch_cpu_index() == 0);
  struct wifi_link *link = link_from_work(work);
  if (forwarder_operation_denied(&link->lineage)) {
    return CALL_DENIED;
  }
  if (atomic_load_explicit(&work->cancel, memory_order_acquire)) {
    return CALL_ABANDONED;
  }
  return !link->finished && arch_monotonic_ns() >= work->deadline_ns ? CALL_TIMED_OUT : CALL_OK;
}

bool wifi_connect_begin(struct wifi_connect_work *work)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct wifi_link *link = link_from_work(work);
  return wifi_connect_stop_reason(work) == CALL_OK && forwarder_operation_start(&link->lineage);
}

void wifi_connection_state(struct wifi_connect_work *work, bool associated,
    bool authenticated, bool data_ready)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  struct wifi_link *link = link_from_work(work);
  spin_lock(&link->lock);
  link->flags = (!associated ? WIFI_LINK_ATTEMPTING : WIFI_LINK_ASSOCIATED) |
      (associated && work->target.security == WIFI_SECURITY_OPEN ? WIFI_LINK_OPEN : 0) |
      (authenticated ? WIFI_LINK_AUTHENTICATED : 0) |
      (data_ready ? WIFI_LINK_DATA_READY : 0);
  spin_unlock(&link->lock);
}

void wifi_connect_finished(struct wifi_connect_work *work, enum call_status status)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct wifi_link *link = link_from_work(work);
  struct wifi_attempt *attempt = link->attempt;
  KASSERT(attempt && !link->finished && attempt->previous);
  if (status == CALL_OK) {
    status = wifi_connect_stop_reason(work);
  }
  *attempt->previous = attempt->next;
  if (attempt->next) {
    attempt->next->previous = attempt->previous;
  }
  attempt->next = NULL;
  attempt->previous = NULL;
  spin_lock(&link->lock);
  link->finished = true;
  link->status = status;
  link->flags = status == CALL_OK ?
      WIFI_LINK_ASSOCIATED | WIFI_LINK_DATA_READY |
      (work->target.security == WIFI_SECURITY_OPEN ? WIFI_LINK_OPEN : WIFI_LINK_AUTHENTICATED) : 0;
  spin_unlock(&link->lock);
  spin_lock(&attempt->lock);
  attempt->complete = true;
  attempt->status = status;
  spin_unlock(&attempt->lock);
  link->attempt = NULL;
  if (status != CALL_OK) {
    capability_grant_release(&attempt->result);
    disconnect(link);
  }
  readiness_notify();
  /* Successful ownership has detached from the short-lived launching group. */
  if (status == CALL_OK && link->cleanup_group) {
    struct execution_group *group = link->cleanup_group;
    link->cleanup_group = NULL;
    execution_group_cleanup_end(group);
  }
  object_release(&attempt->object);
}

void wifi_connection_retired(struct wifi_connect_work *work, enum call_status status)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct wifi_link *link = link_from_work(work);
  KASSERT(link->finished && !link->attempt && !link->retired);
  struct execution_group *group = link->cleanup_group;
  link->cleanup_group = NULL;
  struct execution_group *previous = object_cleanup_enter(group);
  spin_lock(&link->lock);
  link->retired = true;
  struct execution_group *retirement_group = link->retirement_group;
  link->retirement_group = NULL;
  link->closed = true;
  link->flags = WIFI_LINK_TERMINAL;
  link->status = status;
  spin_unlock(&link->lock);
  readiness_notify();
  struct execution_group *retirement_previous = object_cleanup_enter(retirement_group ? retirement_group : group);
  object_release(&link->object); /* Radio's storage reference, never a controller. */
  object_cleanup_leave(retirement_previous);
  object_cleanup_leave(previous);
  if (group) {
    execution_group_cleanup_end(group);
  }
  if (retirement_group) {
    execution_group_cleanup_end(retirement_group);
  }
}

static struct wifi_attempt *create_attempt(struct wifi_connection_request *request)
{
  struct wifi_attempt *attempt = kmalloc(sizeof(*attempt));
  struct wifi_link *link = kmalloc(sizeof(*link));
  if (!attempt || !link) {
    kfree(attempt);
    kfree(link);
    return NULL;
  }
  *attempt = (struct wifi_attempt){.status = CALL_WOULD_BLOCK, .link = link};
  *link = (struct wifi_link){.status = CALL_WOULD_BLOCK, .flags = WIFI_LINK_ATTEMPTING};
  atomic_init(&attempt->lock.locked, false);
  atomic_init(&link->lock.locked, false);
  atomic_init(&link->work.cancel, false);
  if (!object_retain(request->radio)) {
    kfree(link);
    kfree(attempt);
    return NULL;
  }
  if (!object_retain(&request->owner->control->object)) {
    object_release(request->radio);
    kfree(link);
    kfree(attempt);
    return NULL;
  }
  link->radio = request->radio;
  attempt->owner = request->owner->control;
  forwarder_operation_init(&link->lineage);
  object_init(&link->object, OBJECT_WIFI_LINK, destroy_link);
  object_init(&attempt->object, OBJECT_WIFI_CONNECT, destroy_attempt);
  return attempt;
}

static void start_execute(struct wifi_connection_request *request)
{
  uint64_t now = arch_monotonic_ns(), deadline = request->start.deadline_ns;
  request->status = CALL_BAD_REQUEST;
  if ((deadline > now && deadline - now > WIFI_CONNECT_MAX_NS) ||
      (request->start.target.security != WIFI_SECURITY_OPEN &&
       request->start.target.security != WIFI_SECURITY_WPA2_PSK) || request->start.target.reserved ||
      ((request->start.target.security == WIFI_SECURITY_OPEN) != !request->credential.object) ||
      !request->start.target.channel || request->start.target.channel > WIFI_CHANNEL_24_COUNT ||
      !request->start.target.ssid_bytes || request->start.target.ssid_bytes > 32 ||
      (request->start.target.bssid[0] & 1)) {
    return;
  }
  uint8_t bssid = 0;
  for (size_t i = 0; i < 6; ++i) {
    bssid |= request->start.target.bssid[i];
  }
  if (!bssid) {
    return;
  }
  for (size_t i = request->start.target.ssid_bytes; i < 32; ++i) {
    if (request->start.target.ssid[i]) {
      return;
    }
  }
  if (deadline && deadline <= now) {
    request->status = CALL_TIMED_OUT;
    return;
  }
  if (next_generation == UINT64_MAX || (!deadline && now > UINT64_MAX - WIFI_CONNECT_MAX_NS)) {
    request->status = CALL_LIMIT;
    return;
  }
  struct wifi_attempt *attempt = create_attempt(request);
  request->status = CALL_NO_MEMORY;
  if (!attempt) {
    return;
  }
  struct wifi_link *link = attempt->link;
  link->work.generation = ++next_generation;
  link->work.deadline_ns = deadline ? deadline : now + WIFI_CONNECT_MAX_NS;
  link->work.target = request->start.target;
  request->status = forwarder_operation_clone(&link->lineage, request->request.operation);
  if (request->status == CALL_OK && request->credential.object) {
    struct capability_reference target = {0};
    request->status = forwarder_resolve(request->credential.object, request->credential.rights,
        request->credential.transport, &link->lineage, &target);
    if (request->status == CALL_OK) {
      struct file_object *file = (void *)target.object;
      if (target.object->type != OBJECT_FILE || target.transport ||
          target.rights != FILE_RIGHT_READ || file->backing != FILE_RAM ||
          !file->erase_backing || !atomic_load_explicit(&file->immutable_snapshot, memory_order_acquire) ||
          file->size != sizeof(struct wifi_credential)) {
        request->status = CALL_DENIED;
      }
    }
    capability_release(&target);
    if (request->status == CALL_OK) {
      request->status = wifi_auth_select(&link->lineage, &link->work.auth);
    }
    if (request->status == CALL_OK) {
      link->work.credential = request->credential;
      request->credential = (struct capability_grant){0};
    }
  }
  enum capability_result result = CAP_OK;
  struct capability_grant grant = {0};
  if (request->status == CALL_OK) {
    result = capability_grant_retain(&link->object,
        WIFI_LINK_RIGHT_READ | WIFI_LINK_RIGHT_CONTROL, 0, &attempt->result);
  }
  if (request->status == CALL_OK && result == CAP_OK) {
    /* Prepare the close-bearing descendant before TX: CUT must cancel even
     * when the future result is still held only inside the attempt object. */
    result = forwarder_derive_grant(&attempt->result, &link->lineage);
  }
  if (request->status == CALL_OK && result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, &attempt->result, 1);
  }
  if (request->status == CALL_OK && result == CAP_OK) {
    result = capability_grant_retain(&attempt->object, WIFI_CONNECT_RIGHT_COLLECT, 0, &grant);
  }
  if (request->status == CALL_OK && result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, &grant, 1);
  }
  if (request->status == CALL_OK) {
    request->status = cap_status(result);
  }
  if (request->status == CALL_OK && (!task_operation_start() ||
      task_wait_stop_requested(request->request.wait))) {
    request->status = task_wait_stop_requested(request->request.wait) ? CALL_ENDPOINT_CLOSED : CALL_DENIED;
  }
  if (request->status == CALL_OK) {
    link->cleanup_group = object_cleanup_defer();
    KASSERT(object_retain(&link->object));
    KASSERT(object_retain(&attempt->object));
    link->attempt = attempt;
    attempt->next = attempts;
    attempt->previous = &attempts;
    if (attempts) {
      attempts->previous = &attempt->next;
    }
    attempts = attempt;
    request->status = wifi_driver_connect(&link->work);
    if (request->status == CALL_OK) {
      spin_lock(&link->lock);
      link->admitted = true;
      spin_unlock(&link->lock);
      capability_install_reserved(&request->reservation, &request->slot, &grant, 1, &request->result);
    } else {
      attempts = attempt->next;
      if (attempts) {
        attempts->previous = &attempts;
      }
      attempt->next = NULL;
      attempt->previous = NULL;
      link->attempt = NULL;
      if (link->cleanup_group) {
        execution_group_cleanup_end(link->cleanup_group);
        link->cleanup_group = NULL;
      }
      object_release(&attempt->object);
      object_release(&link->object);
    }
  }
  capability_grant_release(&grant);
  object_release(&attempt->object);
}

static void take_execute(struct wifi_connection_request *request)
{
  struct wifi_attempt *attempt = (void *)request->attempt;
  spin_lock(&attempt->lock);
  request->status = !attempt->complete ? CALL_WOULD_BLOCK : attempt->closed || attempt->taken ?
      CALL_ENDPOINT_CLOSED : attempt->status;
  spin_unlock(&attempt->lock);
  if (request->status != CALL_OK) {
    return;
  }
  request->status = cap_status(capability_validate_grants(request->reservation.table, &attempt->result, 1));
  if (request->status != CALL_OK) {
    return;
  }
  if (!task_operation_start() || task_wait_stop_requested(request->request.wait)) {
    request->status = task_wait_stop_requested(request->request.wait) ? CALL_ENDPOINT_CLOSED : CALL_DENIED;
    return;
  }
  spin_lock(&attempt->lock);
  if (attempt->taken || attempt->closed) {
    request->status = CALL_ENDPOINT_CLOSED;
    spin_unlock(&attempt->lock);
    return;
  }
  attempt->taken = true;
  spin_unlock(&attempt->lock);
  capability_install_reserved(&request->reservation, &request->slot, &attempt->result, 1, &request->result);
  readiness_notify();
}

void wifi_connection_execute(struct wifi_connection_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (request->attempt) {
    take_execute(request);
  } else {
    start_execute(request);
  }
  capability_reservation_release(&request->reservation, &request->slot);
  capability_grant_release(&request->credential);
  request->owner = NULL;
  request->attempt = NULL;
  request->radio = NULL;
}

static struct syscall_result exchange(struct wifi_connection_request *input, uintptr_t output)
{
  struct wifi_connection_request *request = (void *)bsp_request_prepare(BSP_SERVICE_WIFI_CONNECTION);
  /* Preserve the catalog's owned transport header. */
  request->radio = input->radio;
  request->attempt = input->attempt;
  request->owner = process_current();
  request->start = input->start;
  request->credential = input->credential;
  input->credential = (struct capability_grant){0};
  request->reservation = input->reservation;
  request->slot = input->slot;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  handle_t result = request->result;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(output, &result, sizeof(result), &result, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(result)};
}

struct syscall_result wifi_connection_start_call(struct kernel_object *radio, uint64_t rights,
    uintptr_t input, size_t size, uintptr_t output, size_t capacity)
{
  if (!(rights & WIFI_RIGHT_CONNECT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  struct wifi_connection_request request = {.radio = radio};
  size_t expected = sizeof(request.start) - sizeof(request.start.header);
  if (size != expected || capacity < sizeof(handle_t)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!copy_from_user(&request.start.deadline_ns, input, expected) ||
      !user_buffer_check(output, sizeof(handle_t), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  enum capability_result result = capability_request_reservation(1, &request.reservation, &request.slot);
  if (result != CAP_OK) {
    return (struct syscall_result){cap_status(result), 0};
  }
  if (request.start.credential) {
    result = capability_grant_acquire(&process_current()->capabilities,
        request.start.credential, FILE_RIGHT_READ, 0, &request.credential);
    if (result != CAP_OK) {
      capability_reservation_release(&request.reservation, &request.slot);
      return (struct syscall_result){cap_status(result), 0};
    }
  }
  if (task_stop_requested()) {
    capability_grant_release(&request.credential);
    capability_reservation_release(&request.reservation, &request.slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  return exchange(&request, output);
}

struct syscall_result wifi_connection_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t size, uintptr_t output, size_t capacity)
{
  bool attempt_type = object->type == OBJECT_WIFI_CONNECT;
  if (attempt_type && (!(rights & WIFI_CONNECT_RIGHT_COLLECT) ||
      !wifi_connection_owned(object, process_current()))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  uint64_t required = operation == WIFI_LINK_STATUS ? WIFI_LINK_RIGHT_READ : WIFI_LINK_RIGHT_CONTROL;
  if (!attempt_type && !(rights & required)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (!operation || operation > (attempt_type ? WIFI_CONNECT_CANCEL : WIFI_LINK_DISCONNECT)) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  size_t expected = operation == 1 ? (attempt_type ? sizeof(struct wifi_connect_status_reply) :
      sizeof(struct wifi_link_status_reply)) : attempt_type && operation == WIFI_CONNECT_TAKE ?
      sizeof(handle_t) : 0;
  if (size || capacity < expected) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (expected && !user_buffer_check(output, expected, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (!attempt_type) {
    struct wifi_link *link = (void *)object;
    if (operation == WIFI_LINK_DISCONNECT) {
      if (!task_operation_start()) {
        return (struct syscall_result){CALL_DENIED, 0};
      }
      disconnect(link);
      return (struct syscall_result){CALL_OK, 0};
    }
    spin_lock(&link->lock);
    struct wifi_link_status_reply reply = {
      .generation = link->work.generation, .interface_id = link->work.interface_id,
      .controller_id = link->work.controller_id, .flags = link->flags |
          (atomic_load_explicit(&link->work.cancel, memory_order_acquire) ? WIFI_LINK_CLOSE_REQUESTED : 0),
      .status = link->status,
    };
    spin_unlock(&link->lock);
    return copy_to_user(output, &reply, sizeof(reply)) ?
        (struct syscall_result){CALL_OK, sizeof(reply)} : (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct wifi_attempt *attempt = (void *)object;
  if (operation == WIFI_CONNECT_CANCEL) {
    if (!task_operation_start()) {
      return (struct syscall_result){CALL_DENIED, 0};
    }
    spin_lock(&attempt->lock);
    bool taken = attempt->taken;
    spin_unlock(&attempt->lock);
    if (taken) {
      return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
    }
    disconnect(attempt->link);
    return (struct syscall_result){CALL_OK, 0};
  }
  if (operation == WIFI_CONNECT_STATUS) {
    spin_lock(&attempt->lock);
    struct wifi_connect_status_reply reply = {
      .flags = (attempt->complete ? WIFI_SCAN_COMPLETE : 0) |
          (attempt->taken ? WIFI_SCAN_TAKEN : 0) |
          (atomic_load_explicit(&attempt->link->work.cancel, memory_order_acquire) ? WIFI_SCAN_CANCEL_REQUESTED : 0),
      .status = attempt->status,
    };
    spin_unlock(&attempt->lock);
    return copy_to_user(output, &reply, sizeof(reply)) ?
        (struct syscall_result){CALL_OK, sizeof(reply)} : (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct wifi_connection_request request = {.attempt = object};
  enum capability_result result = capability_request_reservation(1, &request.reservation, &request.slot);
  if (result != CAP_OK) {
    return (struct syscall_result){cap_status(result), 0};
  }
  return exchange(&request, output);
}
