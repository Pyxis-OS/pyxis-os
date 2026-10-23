#include <abi/file.h>
#include <abi/wait.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/memory.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/file.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/process.h>
#include <kernel/object/wifi.h>
#include <kernel/object/wifi_auth.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <kernel/user/readiness.h>
#include <kernel/user_memory.h>
#include <kernel/wifi.h>

struct wifi_object {
  struct kernel_object object;
  struct process_control *policy_owner;
  uint64_t generation; /* BSP admission only. Zero is never a work generation. */
};

struct wifi_scan {
  struct kernel_object object;
  /* IF=0, no nested guards, allocation, grant callbacks or driver calls. */
  struct spinlock lock;
  struct process_control *owner;
  struct wifi_scan *next, **previous; /* BSP-only active-work lifetime list. */
  struct wifi_object *radio;
  size_t grants;
  uint32_t record_count;
  bool terminal, taken, closed;
  enum call_status status;
  struct wifi_scan_work work;
  struct forwarder_operation lineage;
  struct execution_group *cleanup_group;
  struct file_object *file; /* Private prepared backing until terminal success. */
  struct capability_grant result;
  struct {
    struct wifi_scan_file_header header;
    struct wifi_bss_record records[WIFI_SCAN_RECORDS];
  } bytes;
};

static struct wifi_scan *active_scans; /* BSP/IF=0, backed by driver ownership. */

static void untrack_scan(struct wifi_scan *scan)
{
  KASSERT(scan->previous);
  *scan->previous = scan->next;
  if (scan->next) {
    scan->next->previous = scan->previous;
  }
  scan->next = NULL;
  scan->previous = NULL;
}

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

static struct wifi_scan *scan_from_work(struct wifi_scan_work *work)
{
  return (struct wifi_scan *)((uint8_t *)work - offsetof(struct wifi_scan, work));
}

bool wifi_grant_delegatable(const struct kernel_object *object, uint64_t rights)
{
  enum object_type type = object_effective_type(object);
  if (type == OBJECT_WIFI_AUTH) {
    return wifi_auth_delegatable(object, rights);
  }
  return type != OBJECT_WIFI_SCAN && type != OBJECT_WIFI_CONNECT &&
      (type != OBJECT_WIFI || !(rights & WIFI_RIGHT_POLICY_ONCE));
}

bool wifi_policy_owned(const struct kernel_object *object, const struct process *process)
{
  return object->type == OBJECT_WIFI && process &&
      ((const struct wifi_object *)object)->policy_owner == process->control;
}

bool wifi_scan_owned(const struct kernel_object *object, const struct process *process)
{
  return object->type == OBJECT_WIFI_SCAN && process &&
      ((const struct wifi_scan *)object)->owner == process->control;
}

static void destroy_wifi(struct kernel_object *object)
{
  struct wifi_object *radio = (struct wifi_object *)object;
  object_release(&radio->policy_owner->object);
  kfree(radio);
}

struct kernel_object *wifi_create(struct process *policy_owner)
{
  KASSERT(arch_cpu_index() == 0 && policy_owner);
  struct wifi_object *radio = kmalloc(sizeof(*radio));
  if (!radio) {
    return NULL;
  }
  if (!object_retain(&policy_owner->control->object)) {
    kfree(radio);
    return NULL;
  }
  *radio = (struct wifi_object){.policy_owner = policy_owner->control};
  object_init(&radio->object, OBJECT_WIFI, destroy_wifi);
  return &radio->object;
}

static void destroy_scan(struct kernel_object *object)
{
  struct wifi_scan *scan = (struct wifi_scan *)object;
  KASSERT(!scan->grants && !scan->previous);
  struct execution_group *group = scan->cleanup_group;
  struct execution_group *previous = object_cleanup_enter(group);
  capability_grant_release(&scan->result);
  if (scan->file) {
    object_release(&scan->file->object);
  }
  forwarder_operation_release(&scan->lineage);
  object_release(&scan->owner->object);
  object_release(&scan->radio->object);
  memzero_explicit(&scan->bytes, sizeof(scan->bytes));
  kfree(scan);
  object_cleanup_leave(previous);
  if (group) {
    execution_group_cleanup_end(group);
  }
}

bool wifi_scan_grant_retain(struct kernel_object *object)
{
  struct wifi_scan *scan = (struct wifi_scan *)object;
  spin_lock(&scan->lock);
  bool valid = !scan->closed && scan->grants != SIZE_MAX;
  if (valid) {
    ++scan->grants;
  }
  spin_unlock(&scan->lock);
  return valid;
}

static void cancel_scan(struct wifi_scan *scan)
{
  atomic_store_explicit(&scan->work.cancel, true, memory_order_release);
  wifi_driver_cancel(&scan->work);
}

void wifi_process_exit(struct process *process)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  wifi_auth_process_exit(process);
  for (struct wifi_scan *scan = active_scans; scan; scan = scan->next) {
    if (scan->owner == process->control) {
      cancel_scan(scan);
    }
  }
}

void wifi_scan_grant_release(struct kernel_object *object)
{
  struct wifi_scan *scan = (struct wifi_scan *)object;
  spin_lock(&scan->lock);
  KASSERT(scan->grants);
  bool last = --scan->grants == 0;
  if (last) {
    scan->closed = true;
  }
  spin_unlock(&scan->lock);
  if (last) {
    cancel_scan(scan);
    readiness_notify();
  }
}

uint64_t wifi_scan_ready(struct kernel_object *object, uint64_t events)
{
  struct wifi_scan *scan = (struct wifi_scan *)object;
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&scan->lock);
  uint64_t ready = !scan->terminal ? 0 : scan->closed || scan->taken ? WAIT_CLOSED :
      events & WAIT_COMPLETE;
  spin_unlock(&scan->lock);
  cpu_restore_interrupts(flags);
  return ready;
}

enum call_status wifi_scan_stop_reason(struct wifi_scan_work *work)
{
  KASSERT(arch_cpu_index() == 0);
  struct wifi_scan *scan = scan_from_work(work);
  if (forwarder_operation_denied(&scan->lineage)) {
    return CALL_DENIED;
  }
  if (arch_monotonic_ns() >= work->deadline_ns) {
    return CALL_TIMED_OUT;
  }
  return atomic_load_explicit(&work->cancel, memory_order_acquire) ?
      CALL_ABANDONED : CALL_OK;
}

bool wifi_scan_begin(struct wifi_scan_work *work)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct wifi_scan *scan = scan_from_work(work);
  return wifi_scan_stop_reason(work) == CALL_OK &&
      forwarder_operation_start(&scan->lineage);
}

void wifi_scan_finished(struct wifi_scan_work *work, enum call_status status,
    uint32_t channels, const struct wifi_bss_record *records, size_t count)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct wifi_scan *scan = scan_from_work(work);
  KASSERT(!scan->terminal);
  untrack_scan(scan);
  struct execution_group *previous = object_cleanup_enter(scan->cleanup_group);
  if (status == CALL_OK) {
    status = wifi_scan_stop_reason(work);
  }
  if (status == CALL_OK && count > WIFI_SCAN_RECORDS) {
    status = CALL_FILE_TOO_LARGE;
  }
  if (status == CALL_OK && ((!records && count) || !channels ||
      (channels & ~WIFI_SCAN_CHANNELS) || (work->channels && (channels & ~work->channels)))) {
    status = CALL_IO;
  }
  for (size_t i = 0; status == CALL_OK && i < count; ++i) {
    const struct wifi_bss_record *record = &records[i];
    if (!record->channel || record->channel > WIFI_CHANNEL_24_COUNT || record->ssid_bytes > 32 ||
        !(channels & (UINT32_C(1) << (record->channel - 1))) ||
        (record->security_flags & ~WIFI_BSS_RSN_FLAGS) ||
        (record->security_flags && !(record->security_flags & WIFI_BSS_RSN_PRESENT))) {
      status = CALL_IO;
      break;
    }
    struct wifi_bss_record *copy = &scan->bytes.records[i];
    memcpy(copy->bssid, record->bssid, sizeof(copy->bssid));
    copy->channel = record->channel;
    copy->ssid_bytes = record->ssid_bytes;
    copy->signal_dbm = record->signal_dbm;
    copy->capabilities = record->capabilities;
    copy->security_flags = record->security_flags;
    memcpy(copy->ssid, record->ssid, record->ssid_bytes);
  }
  if (status == CALL_OK) {
    scan->bytes.header = (struct wifi_scan_file_header){
      .count = count, .record_bytes = sizeof(struct wifi_bss_record), .channels = channels,
    };
    size_t length = sizeof(scan->bytes.header) + count * sizeof(scan->bytes.records[0]);
    file_fill_snapshot_size(scan->file, &scan->bytes, length);
  } else {
    capability_grant_release(&scan->result);
  }
  memzero_explicit(&scan->bytes, sizeof(scan->bytes));
  spin_lock(&scan->lock);
  scan->status = status;
  scan->terminal = true;
  if (status == CALL_OK) {
    scan->record_count = count;
  }
  spin_unlock(&scan->lock);
  readiness_notify();
  object_release(&scan->object); /* Return the driver's independent ownership. */
  object_cleanup_leave(previous);
}

static struct wifi_scan *create_scan(struct wifi_request *request)
{
  struct wifi_scan *scan = kmalloc(sizeof(*scan));
  if (!scan) {
    return NULL;
  }
  *scan = (struct wifi_scan){.status = CALL_WOULD_BLOCK};
  atomic_init(&scan->lock.locked, false);
  atomic_init(&scan->work.cancel, false);
  if (!object_retain(&request->owner->control->object)) {
    kfree(scan);
    return NULL;
  }
  if (!object_retain(&request->radio->object)) {
    object_release(&request->owner->control->object);
    kfree(scan);
    return NULL;
  }
  scan->owner = request->owner->control;
  scan->radio = request->radio;
  object_init(&scan->object, OBJECT_WIFI_SCAN, destroy_scan);
  forwarder_operation_init(&scan->lineage);
  return scan;
}

static void start_execute(struct wifi_request *request)
{
  uint64_t now = arch_monotonic_ns();
  uint64_t deadline = request->start.deadline_ns;
  request->status = CALL_BAD_REQUEST;
  if ((deadline && deadline > now && deadline - now > WIFI_SCAN_MAX_NS) ||
      request->start.reserved || (request->start.channels & ~WIFI_SCAN_CHANNELS)) {
    return;
  }
  if (deadline && deadline <= now) {
    request->status = CALL_TIMED_OUT;
    return;
  }
  if (request->radio->generation == UINT64_MAX ||
      (!deadline && now > UINT64_MAX - WIFI_SCAN_MAX_NS)) {
    request->status = CALL_LIMIT;
    return;
  }
  struct wifi_scan *scan = create_scan(request);
  request->status = CALL_NO_MEMORY;
  if (!scan) {
    return;
  }
  scan->work.deadline_ns = deadline ? deadline : now + WIFI_SCAN_MAX_NS;
  scan->work.channels = request->start.channels;
  scan->work.generation = request->radio->generation + 1;
  scan->file = file_prepare_snapshot(sizeof(scan->bytes));
  enum capability_result result = scan->file ? CAP_OK : CAP_NO_MEMORY;
  struct capability_grant grant = {0};
  if (result == CAP_OK) {
    request->status = forwarder_operation_clone(&scan->lineage, request->request.operation);
    result = request->status == CALL_OK ? CAP_OK :
        request->status == CALL_DENIED ? CAP_DENIED : CAP_LIMIT;
  }
  if (result == CAP_OK) {
    result = capability_grant_retain(&scan->file->object, FILE_RIGHT_READ, 0, &scan->result);
  }
  if (result == CAP_OK) {
    result = forwarder_derive_grant(&scan->result, request->request.operation);
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, &scan->result, 1);
  }
  if (result == CAP_OK) {
    result = capability_grant_retain(&scan->object, WIFI_SCAN_RIGHT_COLLECT, 0, &grant);
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, &grant, 1);
  }
  request->status = cap_status(result);
  if (result == CAP_OK && (!task_operation_start() ||
      task_wait_stop_requested(request->request.wait))) {
    request->status = task_wait_stop_requested(request->request.wait) ?
        CALL_ENDPOINT_CLOSED : CALL_DENIED;
  }
  if (request->status == CALL_OK) {
    scan->cleanup_group = object_cleanup_defer();
    KASSERT(object_retain(&scan->object));
    scan->next = active_scans;
    scan->previous = &active_scans;
    if (active_scans) {
      active_scans->previous = &scan->next;
    }
    active_scans = scan;
    request->status = wifi_driver_scan(&scan->work);
    if (request->status == CALL_OK) {
      request->radio->generation = scan->work.generation;
      capability_install_reserved(&request->reservation, &request->slot, &grant, 1,
          &request->reply.started.operation);
    } else {
      untrack_scan(scan);
      object_release(&scan->object);
    }
  }
  capability_grant_release(&grant);
  object_release(&scan->object);
}

static void take_execute(struct wifi_request *request)
{
  struct wifi_scan *scan = request->scan;
  spin_lock(&scan->lock);
  request->status = !scan->terminal ? CALL_WOULD_BLOCK : scan->closed || scan->taken ?
      CALL_ENDPOINT_CLOSED : scan->status;
  spin_unlock(&scan->lock);
  if (request->status != CALL_OK) {
    return;
  }
  enum capability_result result = capability_validate_grants(request->reservation.table,
      &scan->result, 1);
  if (result != CAP_OK) {
    request->status = cap_status(result);
    return;
  }
  if (!task_operation_start() || task_wait_stop_requested(request->request.wait)) {
    request->status = task_wait_stop_requested(request->request.wait) ?
        CALL_ENDPOINT_CLOSED : CALL_DENIED;
    return;
  }
  spin_lock(&scan->lock);
  if (scan->closed || scan->taken) {
    request->status = CALL_ENDPOINT_CLOSED;
    spin_unlock(&scan->lock);
    return;
  }
  scan->taken = true;
  spin_unlock(&scan->lock);
  capability_install_reserved(&request->reservation, &request->slot, &scan->result, 1,
      &request->reply.taken.file);
  readiness_notify();
}

static void seal_credential(struct wifi_request *request)
{
  struct file_object *file = file_prepare_secret_snapshot(sizeof(request->credential));
  request->status = CALL_NO_MEMORY;
  if (!file) {
    return;
  }
  file_fill_snapshot(file, &request->credential);
  struct capability_grant grant = {0};
  enum capability_result result = capability_grant_retain(&file->object,
      FILE_RIGHT_READ, 0, &grant);
  if (result == CAP_OK) {
    result = forwarder_derive_grant(&grant, request->request.operation);
  }
  if (result == CAP_OK) {
    result = capability_validate_grants(request->reservation.table, &grant, 1);
  }
  request->status = cap_status(result);
  if (result == CAP_OK) {
    capability_install_reserved(&request->reservation, &request->slot, &grant, 1,
        &request->reply.credential.file);
  }
  capability_grant_release(&grant);
  object_release(&file->object);
}

void wifi_request_execute(struct wifi_request *request)
{
  KASSERT(arch_cpu_index() == 0 && !(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (request->scan) {
    take_execute(request);
  } else if (request->operation == WIFI_SCAN_START) {
    start_execute(request);
  } else if (!task_operation_start() || task_wait_stop_requested(request->request.wait)) {
    request->status = task_wait_stop_requested(request->request.wait) ?
        CALL_ENDPOINT_CLOSED : CALL_DENIED;
  } else if (request->operation == WIFI_STATUS) {
    wifi_driver_status(&request->reply.status);
    request->status = CALL_OK;
  } else if (request->operation == WIFI_CREDENTIAL_SEAL) {
    seal_credential(request);
  } else {
    KASSERT(request->operation == WIFI_INSTALL_POLICY);
    request->status = wifi_policy_owned(&request->radio->object, request->owner) ?
        wifi_driver_policy(&request->policy) : CALL_DENIED;
  }
  capability_reservation_release(&request->reservation, &request->slot);
  request->owner = NULL;
  request->radio = NULL;
  request->scan = NULL;
  memzero_explicit(&request->credential, sizeof(request->credential));
}

static bool credential_valid(const struct wifi_credential *credential)
{
  bool passphrase = credential->format == WIFI_CREDENTIAL_PASSPHRASE;
  if (passphrase ? credential->length < 8 || credential->length > 63 :
      credential->format != WIFI_CREDENTIAL_PMK || credential->length != 32) {
    return false;
  }
  for (size_t i = 0; i < sizeof(credential->bytes); ++i) {
    uint8_t byte = credential->bytes[i];
    if (i >= credential->length ? byte != 0 :
        passphrase && (!byte || byte == '\r' || byte == '\n')) {
      return false;
    }
  }
  return true;
}

static struct syscall_result credential_call(struct wifi_object *radio, uint64_t rights,
    uintptr_t address, size_t size, uintptr_t output, size_t capacity)
{
  if (!(rights & WIFI_RIGHT_CONNECT)) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (size != sizeof(struct wifi_credential) || capacity < sizeof(struct wifi_credential_reply)) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (!user_buffer_check(output, sizeof(struct wifi_credential_reply), USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct wifi_credential credential = {0};
  enum call_status status = CALL_BAD_BUFFER;
  if (!copy_from_user(&credential, address, sizeof(credential))) {
    goto clear;
  }
  status = CALL_BAD_REQUEST;
  if (!credential_valid(&credential)) {
    goto clear;
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot = {0};
  enum capability_result result = capability_request_reservation(1, &reservation, &slot);
  if (result != CAP_OK) {
    status = cap_status(result);
    goto clear;
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    status = CALL_ENDPOINT_CLOSED;
    goto clear;
  }
  struct wifi_request *request = (void *)bsp_request_prepare(BSP_SERVICE_WIFI);
  request->radio = radio;
  request->owner = process_current();
  request->operation = WIFI_CREDENTIAL_SEAL;
  request->credential = credential;
  request->reservation = reservation;
  request->slot = slot;
  memzero_explicit(&credential, sizeof(credential));
  bsp_request_submit_and_wait(&request->request);
  status = request->status;
  struct wifi_credential_reply reply = request->reply.credential;
  memzero_explicit(&request->credential, sizeof(request->credential));
  bsp_request_release(&request->request);
  if (status == CALL_OK && !copy_returned_handles_to_user(output, &reply,
      sizeof(reply), &reply.file, 1)) {
    status = CALL_BAD_BUFFER;
  }
clear:
  memzero_explicit(&credential, sizeof(credential));
  return (struct syscall_result){status, status == CALL_OK ? sizeof(struct wifi_credential_reply) : 0};
}

struct syscall_result wifi_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, uintptr_t address, size_t size,
    uintptr_t reply_address, size_t reply_capacity)
{
  if (operation == WIFI_OPEN_AUTH) {
    return wifi_auth_open_call(object, rights, size, reply_address, reply_capacity);
  }
  if (operation == WIFI_CREDENTIAL_SEAL) {
    return credential_call((struct wifi_object *)object, rights, address, size,
        reply_address, reply_capacity);
  }
  uint64_t required;
  size_t input_size, output_size;
  if (operation == WIFI_STATUS) {
    required = WIFI_RIGHT_READ;
    input_size = 0;
    output_size = sizeof(struct wifi_status_reply);
  } else if (operation == WIFI_SCAN_START) {
    required = WIFI_RIGHT_SCAN;
    input_size = sizeof(struct wifi_scan_request) - sizeof(struct message_header);
    output_size = sizeof(struct wifi_scan_start_reply);
  } else if (operation == WIFI_INSTALL_POLICY) {
    required = WIFI_RIGHT_POLICY_ONCE;
    input_size = sizeof(struct wifi_policy_request) - sizeof(struct message_header);
    output_size = 0;
  } else {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  if (!(rights & required) || (operation == WIFI_INSTALL_POLICY &&
      !wifi_policy_owned(object, process_current()))) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (size != input_size || reply_capacity < output_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (output_size && !user_buffer_check(reply_address, output_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct wifi_scan_request start = {0};
  struct wifi_policy_request policy = {.header = {PROTOCOL_WIFI, WIFI_INSTALL_POLICY}};
  if (input_size && !copy_from_user(operation == WIFI_SCAN_START ?
      (void *)&start.deadline_ns : (void *)&policy.channels, address, input_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == WIFI_INSTALL_POLICY && (!policy.channels ||
      (policy.channels & ~WIFI_SCAN_CHANNELS) || policy.reserved[0] || policy.reserved[1] ||
      !wifi_policy_country_valid(policy.country))) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot = {0};
  if (operation == WIFI_SCAN_START) {
    enum capability_result result = capability_request_reservation(1, &reservation, &slot);
    if (result != CAP_OK) {
      return (struct syscall_result){cap_status(result), 0};
    }
  }
  if (task_stop_requested()) {
    capability_reservation_release(&reservation, &slot);
    return (struct syscall_result){CALL_ENDPOINT_CLOSED, 0};
  }
  struct wifi_request *request = (void *)bsp_request_prepare(BSP_SERVICE_WIFI);
  request->radio = (struct wifi_object *)object;
  request->owner = process_current();
  request->operation = operation;
  request->start = start;
  request->policy = policy;
  request->reservation = reservation;
  request->slot = slot;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  typeof(request->reply) reply = request->reply;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (operation == WIFI_SCAN_START) {
    if (!copy_returned_handles_to_user(reply_address, &reply.started, output_size,
        &reply.started.operation, 1)) {
      return (struct syscall_result){CALL_BAD_BUFFER, 0};
    }
  } else if (output_size && !copy_to_user(reply_address, &reply.status, output_size)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, output_size};
}

struct syscall_result wifi_scan_call(struct kernel_object *object, uint64_t rights,
    uint64_t operation, size_t size, uintptr_t reply_address, size_t reply_capacity)
{
  if (!(rights & WIFI_SCAN_RIGHT_COLLECT) || !wifi_scan_owned(object, process_current())) {
    return (struct syscall_result){CALL_DENIED, 0};
  }
  if (operation < WIFI_SCAN_STATUS || operation > WIFI_SCAN_CANCEL) {
    return (struct syscall_result){CALL_BAD_OPERATION, 0};
  }
  size_t output_size = operation == WIFI_SCAN_STATUS ? sizeof(struct wifi_scan_status_reply) :
      operation == WIFI_SCAN_TAKE ? sizeof(struct wifi_scan_take_reply) : 0;
  if (size || reply_capacity < output_size) {
    return (struct syscall_result){CALL_BAD_REQUEST, 0};
  }
  if (output_size && !user_buffer_check(reply_address, output_size, USER_BUFFER_WRITE)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  struct wifi_scan *scan = (struct wifi_scan *)object;
  if (operation == WIFI_SCAN_STATUS) {
    spin_lock(&scan->lock);
    struct wifi_scan_status_reply reply = {
      .flags = (scan->terminal ? WIFI_SCAN_COMPLETE : 0) |
          (scan->taken ? WIFI_SCAN_TAKEN : 0) |
          (atomic_load_explicit(&scan->work.cancel, memory_order_acquire) ?
              WIFI_SCAN_CANCEL_REQUESTED : 0),
      .status = scan->status, .record_count = scan->record_count,
    };
    spin_unlock(&scan->lock);
    return copy_to_user(reply_address, &reply, sizeof(reply)) ?
        (struct syscall_result){CALL_OK, sizeof(reply)} : (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  if (operation == WIFI_SCAN_CANCEL) {
    cancel_scan(scan);
    return (struct syscall_result){CALL_OK, 0};
  }
  spin_lock(&scan->lock);
  enum call_status ready = !scan->terminal ? CALL_WOULD_BLOCK : scan->closed || scan->taken ?
      CALL_ENDPOINT_CLOSED : scan->status;
  spin_unlock(&scan->lock);
  if (ready != CALL_OK) {
    return (struct syscall_result){ready, 0};
  }
  struct capability_reservation reservation = {0};
  struct capability_reserved_slot slot;
  enum capability_result result = capability_request_reservation(1, &reservation, &slot);
  if (result != CAP_OK) {
    return (struct syscall_result){cap_status(result), 0};
  }
  struct wifi_request *request = (void *)bsp_request_prepare(BSP_SERVICE_WIFI);
  request->scan = scan;
  request->owner = process_current();
  request->reservation = reservation;
  request->slot = slot;
  bsp_request_submit_and_wait(&request->request);
  enum call_status status = request->status;
  struct wifi_scan_take_reply reply = request->reply.taken;
  bsp_request_release(&request->request);
  if (status != CALL_OK) {
    return (struct syscall_result){status, 0};
  }
  if (!copy_returned_handles_to_user(reply_address, &reply, sizeof(reply), &reply.file, 1)) {
    return (struct syscall_result){CALL_BAD_BUFFER, 0};
  }
  return (struct syscall_result){CALL_OK, sizeof(reply)};
}
