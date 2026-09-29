#include <kernel/object/namespace.h>
#include <kernel/object/endpoint.h>
#include <abi/profile.h>
#include <kernel/object/display.h>
#include <kernel/object/memory.h>
#include <kernel/service/profile.h>
#include <arch/cpu.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <arch/user.h>
#include <kernel/log.h>
#include <kernel/fs/ramfs.h>
#include <kernel/fs/hostfs.h>
#include <kernel/memory.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vm.h>
#include <kernel/object/object.h>
#include <kernel/object/file.h>
#include <kernel/object/console.h>
#include <kernel/object/process.h>
#include <kernel/object/launcher.h>
#include <kernel/object/pipe.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/user.h>
#include <kernel/task.h>
#include <stdatomic.h>
#include <arch/cpu_local.h>

#define TASK_STACK_SIZE (16 * 1024)

enum task_kind {
  TASK_USER,
  TASK_KERNEL,
};

struct task_wait {
  struct task *task;
  bool notified;
  bool timed;
  uint64_t deadline;
  struct task_wait *timeout_next;
};

enum launch_action {
  LAUNCH_ALLOCATE, LAUNCH_DISCARD, LAUNCH_START,
  LAUNCH_GROUP_CREATE, LAUNCH_GROUP_PREPARE, LAUNCH_GROUP_PUBLISH,
  LAUNCH_GROUP_DISCARD,
};
enum directory_action { DIRECTORY_ALLOCATE_ENTRY, DIRECTORY_ALLOCATE_NAME, DIRECTORY_DISCARD };

struct task {
  struct task *next;
  struct task *growth_next;
  enum capability_result growth_result;
  struct task *directory_next;
  struct directory_entry *directory_entry;
  uint64_t directory_kind;
  size_t directory_name_length;
  enum directory_action directory_action;
  struct hostfs_request hostfs_request;
  struct file_wait file_wait;
  struct console_wait console_wait;
  struct process_wait process_wait;
  struct pipe_wait pipe_wait;
  struct task *endpoint_next;
  struct endpoint_create_reply endpoint_reply;
  struct endpoint_export_message export_request;
  struct endpoint_export_reply export_reply;
  bool endpoint_exporting;
  enum call_status endpoint_result;
  struct task *namespace_next;
  handle_t namespace_handle;
  enum call_status namespace_result;
  /* Temporary typed storage until the reusable user-request area is provisioned. */
  struct pipe_create_request pipe_request;
  struct memory_request memory_request;
  struct display_request display_request;
  struct bsp_request *bsp_request; /* Reserved through result consumption. */
  struct bsp_request *deferred_request; /* Published only after the safe handoff. */
  struct task *file_next;
  struct file_object *file;
  size_t file_capacity;
  bool file_result;
  struct task *launch_next;
  enum launch_action launch_action;
  struct launch_capture *launch_capture;
  struct launch_group *launch_group;
  enum call_status launch_result;
  handle_t launch_child;
  handle_t launch_children[LAUNCH_BATCH_MAX];
  /* Only the caller updates aggregates. The parked request lends timestamps
   * to the BSP until wakeup transfers ownership back. */
  struct profile_snapshot profile;
  struct profile_file_snapshot file_profile;
  struct profile_host_snapshot host_profile;
  struct file_buffer_profile file_buffer_profile;
  uint64_t file_started_ns, file_published_ns;
  uint64_t file_service_started_ns, file_service_ended_ns;
  enum task_kind kind;
  struct process *process; /* Owned by a user task; NULL for a kernel task. */
  uintptr_t kernel_stack;
  uintptr_t saved_stack;
  uintptr_t entry, user_stack;
  struct arch_user_state cpu;
  size_t cpu_index;
  bool exited, faulted;
  int exit_status;
  void (*kernel_entry)(void *);
  void *argument;
  uint64_t sleep_deadline;
  struct task_wait wait_record;
  struct task_wait *wait;
  bool parked; /* queues_locked: stack saved and no CPU is executing this task. */
};

struct scheduler {
  struct task *ready_head, *ready_tail;
  struct task *current_task;
  uintptr_t stack;
};

static struct scheduler *schedulers;
static struct task *completed_head;
static struct task *growth_head, *growth_tail;
static struct task *directory_head, *directory_tail;
static struct task *file_head, *file_tail;
static struct hostfs_request *hostfs_head, *hostfs_tail;
static struct task *launch_head, *launch_tail;
static struct task *endpoint_head, *endpoint_tail;
static struct task *namespace_head, *namespace_tail;
static atomic_bool started;
static atomic_bool queues_locked;
static struct task_wait *timed_waits; /* queues_locked, expired by the BSP. */

/* Sleeping kernel tasks belong to the BSP and are accessed only with IF=0. */
static struct task *sleeping_tasks;

/* IF=0 on every caller. Protects queue links and wait state; never allocate, log,
 * switch contexts or wait for another CPU while holding this lock. */
static void lock_queues(void)
{
  while (atomic_exchange_explicit(&queues_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_queues(void)
{
  atomic_store_explicit(&queues_locked, false, memory_order_release);
}

static struct scheduler *local_scheduler(void)
{
  return &schedulers[arch_cpu_index()];
}

struct process *process_current(void)
{
  struct task *task = local_scheduler()->current_task;
  return task && task->kind == TASK_USER ? task->process : NULL;
}

bool kernel_task_is_current(void (*entry)(void *))
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = schedulers ? local_scheduler()->current_task : NULL;
  return task && task->kind == TASK_KERNEL && task->kernel_entry == entry;
}

void task_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !schedulers);
  size_t count = arch_cpu_count();
  KASSERT(count && count <= SIZE_MAX / sizeof(*schedulers));
  schedulers = kmalloc(count * sizeof(*schedulers));
  if (!schedulers) {
    panic("cannot allocate CPU schedulers");
  }
  memset(schedulers, 0, count * sizeof(*schedulers));
}

static void enqueue_locked(struct scheduler *scheduler, struct task *task)
{
  task->next = NULL;
  if (scheduler->ready_tail) {
    scheduler->ready_tail->next = task;
  } else {
    scheduler->ready_head = task;
  }
  scheduler->ready_tail = task;
}

static void notify_remote_cpu(size_t cpu_index)
{
  if (cpu_index != arch_cpu_index() &&
      atomic_load_explicit(&started, memory_order_acquire)) {
    arch_cpu_reschedule(cpu_index);
  }
}

static void enqueue(struct scheduler *scheduler, struct task *task)
{
  size_t cpu_index = task->cpu_index;
  lock_queues();
  enqueue_locked(scheduler, task);
  unlock_queues();
  /* Publication transfers ownership; only the saved CPU index is safe here. */
  notify_remote_cpu(cpu_index);
}

struct task_wait *task_wait_prepare(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && !task->exited);
  KASSERT(task->kind == TASK_USER || arch_cpu_index() == 0);
  KASSERT(!task->wait && !task->parked);
  task->wait_record = (struct task_wait){.task = task};
  return &task->wait_record;
}

/* Process-specific service requests still require a user task. */
static struct task_wait *prepare_user_wait(void)
{
  struct task_wait *wait = task_wait_prepare();
  KASSERT(wait->task->kind == TASK_USER);
  return wait;
}

struct process_wait *task_prepare_process_wait(void)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  task->process_wait = (struct process_wait){.wait = wait};
  return &task->process_wait;
}

struct console_wait *task_prepare_console_wait(void)
{
  struct task_wait *wait = prepare_user_wait();
  struct console_wait *record = &wait->task->console_wait;
  *record = (struct console_wait){.wait = wait};
  return record;
}

struct pipe_wait *task_prepare_pipe_wait(void)
{
  struct task_wait *wait = prepare_user_wait();
  struct pipe_wait *record = &wait->task->pipe_wait;
  *record = (struct pipe_wait){.wait = wait};
  return record;
}

uint64_t task_deadline_after_ms(uint32_t milliseconds)
{
  uint64_t now = arch_monotonic_ns();
  uint64_t duration = (uint64_t)milliseconds * UINT64_C(1000000);
  return duration > UINT64_MAX - now ? UINT64_MAX : now + duration;
}

bool task_deadline_expired(uint64_t deadline)
{
  return arch_monotonic_ns() >= deadline;
}

static void sleep_wait(struct task_wait *wait, bool timed, uint64_t deadline)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && wait == &task->wait_record && wait->task == task && !task->exited);
  KASSERT(task->kind == TASK_USER || arch_cpu_index() == 0);

  lock_queues();
  if (wait->notified || (timed && task_deadline_expired(deadline))) {
    unlock_queues();
    return;
  }
  KASSERT(!task->wait && !task->parked);
  task->wait = wait;
  if (timed) {
    wait->timed = true;
    wait->deadline = deadline;
    wait->timeout_next = timed_waits;
    timed_waits = wait;
  }
  unlock_queues();

  if (task->kind == TASK_USER) {
    arch_user_save(&task->cpu);
  }
  arch_context_switch(&task->saved_stack, scheduler->stack);
}

void task_wait_sleep(struct task_wait *wait)
{
  sleep_wait(wait, false, 0);
}

void task_wait_sleep_until(struct task_wait *wait, uint64_t deadline)
{
  sleep_wait(wait, true, deadline);
}

static void wake_wait_locked(struct task_wait *wait)
{
  wait->notified = true;
  struct task *task = wait->task;
  if (task->parked) {
    KASSERT(task->wait == wait && !task->exited);
    task->parked = false;
    task->wait = NULL;
    enqueue_locked(&schedulers[task->cpu_index], task);
  }
  /* A wake before the switch only records notification. Enqueueing a task
   * before its stack has been saved could run it on two contexts at once. */
}

void task_wait_wake(struct task_wait *wait)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  lock_queues();
  if (wait->timed) {
    struct task_wait **link = &timed_waits;
    while (*link != wait) {
      KASSERT(*link);
      link = &(*link)->timeout_next;
    }
    *link = wait->timeout_next;
    wait->timeout_next = NULL;
    wait->timed = false;
  }
  bool parked = wait->task->parked;
  size_t cpu_index = wait->task->cpu_index;
  wake_wait_locked(wait);
  unlock_queues();
  /* The target can consume its wait/task immediately after queue publication.
   * An early wake needs no IPI: the running task observes notified when parking. */
  if (parked) {
    notify_remote_cpu(cpu_index);
  }
}

static void expire_timed_waits(void)
{
  lock_queues();
  uint64_t now = arch_monotonic_ns();
  struct task_wait **link = &timed_waits;
  while (*link) {
    struct task_wait *wait = *link;
    if (now < wait->deadline) {
      link = &wait->timeout_next;
      continue;
    }
    *link = wait->timeout_next;
    wait->timeout_next = NULL;
    wait->timed = false;
    wake_wait_locked(wait);
  }
  unlock_queues();
}

enum capability_result task_grow_capabilities(void)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;

  lock_queues();
  task->growth_next = NULL;
  if (growth_tail) {
    growth_tail->growth_next = task;
  } else {
    growth_head = task;
  }
  growth_tail = task;
  unlock_queues();

  /* Do not touch the table after publication. The BSP may finish before
   * sleep; the wait record preserves that early completion. */
  task_wait_sleep(wait);
  return task->growth_result;
}

static void grow_requested_tables(void)
{
  lock_queues();
  struct task *task = growth_head;
  growth_head = growth_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->growth_next;
    task->growth_result = capability_grow(&task->process->capabilities);
    task_wait_wake(&task->wait_record);
    /* Waking returns table ownership; the task may immediately exit. */
    task = next;
  }
}

enum call_status task_create_namespace(handle_t *handle)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  lock_queues();
  task->namespace_next = NULL;
  if (namespace_tail) {
    namespace_tail->namespace_next = task;
  } else {
    namespace_head = task;
  }
  namespace_tail = task;
  unlock_queues();
  task_wait_sleep(wait);
  *handle = task->namespace_result == CALL_OK ? task->namespace_handle : HANDLE_INVALID;
  return task->namespace_result;
}

static void service_namespace_requests(void)
{
  lock_queues();
  struct task *task = namespace_head;
  namespace_head = namespace_tail = NULL;
  unlock_queues();
  while (task) {
    struct task *next = task->namespace_next;
    task->namespace_result = namespace_create(task->process, &task->namespace_handle);
    task_wait_wake(&task->wait_record);
    task = next;
  }
}

static struct task *current_user_task(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER && !task->exited);
  return task;
}

struct bsp_request *task_bsp_request_acquire(enum bsp_service service)
{
  struct task *task = current_user_task();
  KASSERT(!task->bsp_request);
  struct bsp_request *request;
  switch (service) {
  case BSP_SERVICE_PIPE_CREATE:
    request = &task->pipe_request.request;
    break;
  case BSP_SERVICE_MEMORY:
    request = &task->memory_request.request;
    break;
  case BSP_SERVICE_DISPLAY:
    request = &task->display_request.request;
    break;
  default:
    panic("unknown BSP service %u", (unsigned)service);
  }
  KASSERT(request->state == BSP_REQUEST_FREE);
  task->bsp_request = request;
  return request;
}

struct bsp_request *task_bsp_request_current(void)
{
  return current_user_task()->bsp_request;
}

void task_bsp_request_release(struct bsp_request *request)
{
  struct task *task = current_user_task();
  KASSERT(task->bsp_request == request && !task->deferred_request);
  KASSERT(request->state == BSP_REQUEST_COMPLETE);
  task->bsp_request = NULL;
}

void task_bsp_request_defer(struct bsp_request *request)
{
  struct task *task = current_user_task();
  KASSERT(task->bsp_request == request && !task->deferred_request);
  KASSERT(request->state == BSP_REQUEST_DEFERRED && request->wait == &task->wait_record);
  task->deferred_request = request;
}

static void publish_deferred_request(struct task *task)
{
  struct bsp_request *request = task->deferred_request;
  lock_queues();
  KASSERT(task->kind == TASK_USER && request && request == task->bsp_request);
  KASSERT(task->wait == &task->wait_record && !task->wait->notified && !task->parked);
  task->deferred_request = NULL;
  task->parked = true;
  unlock_queues();
  /* The permanent stack and kernel root are active. Publication lends the
   * inactive process; no task/request accesses may follow this call. */
  bsp_request_publish_deferred(request);
}

struct profile_snapshot *task_memory_profile(void)
{
  return &current_user_task()->profile;
}

static void submit_endpoint_request(struct task *task, struct task_wait *wait)
{
  lock_queues();
  task->endpoint_next = NULL;
  if (endpoint_tail) {
    endpoint_tail->endpoint_next = task;
  } else {
    endpoint_head = task;
  }
  endpoint_tail = task;
  unlock_queues();
  task_wait_sleep(wait);
}

enum call_status task_create_endpoint(struct endpoint_create_reply *reply)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  task->endpoint_exporting = false;
  submit_endpoint_request(task, wait);
  if (task->endpoint_result == CALL_OK) {
    *reply = task->endpoint_reply;
  }
  return task->endpoint_result;
}

enum call_status task_export_endpoint(const struct endpoint_export_message *request,
    struct endpoint_export_reply *reply)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  task->endpoint_exporting = true;
  task->export_request = *request;
  submit_endpoint_request(task, wait);
  if (task->endpoint_result == CALL_OK) {
    *reply = task->export_reply;
  }
  return task->endpoint_result;
}

static void service_endpoint_requests(void)
{
  lock_queues();
  struct task *task = endpoint_head;
  endpoint_head = endpoint_tail = NULL;
  unlock_queues();
  while (task) {
    struct task *next = task->endpoint_next;
    task->endpoint_result = task->endpoint_exporting ?
        endpoint_export_create(task->process, &task->export_request, &task->export_reply) :
        endpoint_create(task->process, &task->endpoint_reply);
    task_wait_wake(&task->wait_record);
    task = next;
  }
}

static void queue_directory_request(struct task *task, struct task_wait *wait)
{
  lock_queues();
  task->directory_next = NULL;
  if (directory_tail) {
    directory_tail->directory_next = task;
  } else {
    directory_head = task;
  }
  directory_tail = task;
  unlock_queues();
  task_wait_sleep(wait);
}

struct directory_entry *task_allocate_directory_entry(uint64_t kind, size_t name_length)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  task->directory_kind = kind;
  task->directory_name_length = name_length;
  task->directory_action = DIRECTORY_ALLOCATE_ENTRY;
  task->directory_entry = NULL;
  queue_directory_request(task, wait);
  struct directory_entry *entry = task->directory_entry;
  task->directory_entry = NULL;
  return entry;
}

struct directory_entry *task_allocate_directory_name(size_t name_length)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  task->directory_name_length = name_length;
  task->directory_action = DIRECTORY_ALLOCATE_NAME;
  task->directory_entry = NULL;
  queue_directory_request(task, wait);
  struct directory_entry *entry = task->directory_entry;
  task->directory_entry = NULL;
  return entry;
}

void task_discard_directory_entry(struct directory_entry *entry)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  task->directory_action = DIRECTORY_DISCARD;
  task->directory_entry = entry;
  queue_directory_request(task, wait);
}

static void service_directory_requests(void)
{
  lock_queues();
  struct task *task = directory_head;
  directory_head = directory_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->directory_next;
    if (task->directory_action == DIRECTORY_DISCARD) {
      ramfs_discard_entry(task->directory_entry);
      task->directory_entry = NULL;
    } else if (task->directory_action == DIRECTORY_ALLOCATE_NAME) {
      task->directory_entry = ramfs_allocate_name(task->directory_name_length);
    } else {
      task->directory_entry = ramfs_allocate_entry(task->directory_kind,
          task->directory_name_length);
    }
    task_wait_wake(&task->wait_record);
    /* All result storage is published before waking; task may now exit. */
    task = next;
  }
}

struct hostfs_request *task_prepare_hostfs(enum hostfs_operation operation)
{
  struct task *task = local_scheduler()->current_task;
  bool profiled = (task->host_profile.flags & PROFILE_ACTIVE) &&
      (operation == HOSTFS_READ || operation == HOSTFS_WRITE);
  uint64_t started = profiled ? arch_monotonic_ns() : 0;
  struct task_wait *wait = prepare_user_wait();
  struct hostfs_request *request = &wait->task->hostfs_request;
  *request = (struct hostfs_request){.wait = wait, .operation = operation,
      .profile = {.active = profiled, .started_ns = started}};
  return request;
}

static void finish_host_profile(struct task *task);

void task_submit_hostfs(struct hostfs_request *request)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(request == &task->hostfs_request);
  struct task_wait *wait = request->wait;
  bool profiled = request->profile.active;
  if (profiled) {
    request->profile.requested_bytes = request->count;
    request->profile.published_ns = arch_monotonic_ns();
  }
  lock_queues();
  if (hostfs_tail) {
    hostfs_tail->next = request;
  } else {
    hostfs_head = request;
  }
  hostfs_tail = request;
  unlock_queues();
  /* Publication may miss the BSP's queue sweep; wake it before parking. */
  notify_remote_cpu(0);
  task_wait_sleep(wait);
  if (profiled) {
    finish_host_profile(task);
  }
}

static void service_hostfs_requests(void)
{
  lock_queues();
  struct hostfs_request *request = hostfs_head;
  hostfs_head = hostfs_tail = NULL;
  unlock_queues();

  while (request) {
    struct hostfs_request *next = request->next;
    if (request->profile.active) {
      request->profile.forwarded_ns = arch_monotonic_ns();
    }
    hostfs_submit(request);
    /* Submission can complete immediately; never access it after wakeup. */
    request = next;
  }
}

struct file_wait *task_prepare_file_wait(void)
{
  struct task_wait *wait = prepare_user_wait();
  struct file_wait *record = &wait->task->file_wait;
  *record = (struct file_wait){.wait = wait};
  return record;
}

static void finish_file_profile(struct task *task);

bool task_replace_file_buffer(struct file_object *file, size_t capacity)
{
  struct task *task = local_scheduler()->current_task;
  bool profiled = task->file_profile.flags & PROFILE_ACTIVE;
  if (profiled) {
    task->file_started_ns = arch_monotonic_ns();
    task->file_buffer_profile = (struct file_buffer_profile){0};
  }
  struct task_wait *wait = prepare_user_wait();
  task->file = file;
  task->file_capacity = capacity;

  if (profiled) {
    task->file_published_ns = arch_monotonic_ns();
  }
  lock_queues();
  task->file_next = NULL;
  if (file_tail) {
    file_tail->file_next = task;
  } else {
    file_head = task;
  }
  file_tail = task;
  unlock_queues();

  task_wait_sleep(wait);
  if (profiled) {
    finish_file_profile(task);
  }
  task->file = NULL;
  return task->file_result;
}

static void service_file_requests(void)
{
  lock_queues();
  struct task *task = file_head;
  file_head = file_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->file_next;
    bool profiled = task->file_profile.flags & PROFILE_ACTIVE;
    if (profiled) {
      task->file_service_started_ns = arch_monotonic_ns();
    }
    task->file_result = file_replace_buffer(task->file, task->file_capacity,
        profiled ? &task->file_buffer_profile : NULL);
    if (profiled) {
      task->file_service_ended_ns = arch_monotonic_ns();
    }
    task_wait_wake(&task->wait_record);
    /* The requester owns the file again and may immediately exit. */
    task = next;
  }
}

enum call_status task_profile_control(uint64_t operation, struct profile_snapshot *reply)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER);
  bool active = task->profile.flags & PROFILE_ACTIVE;
  if (operation == PROFILE_BEGIN) {
    if (active) {
      return CALL_BUSY;
    }
    task->profile = (struct profile_snapshot){.flags = PROFILE_ACTIVE};
  } else {
    if (operation == PROFILE_END) {
      if (!active) {
        return CALL_BAD_REQUEST;
      }
      task->profile.flags &= ~PROFILE_ACTIVE;
    } else {
      KASSERT(operation == PROFILE_SNAPSHOT);
    }
    *reply = task->profile;
  }
  return CALL_OK;
}

enum call_status task_profile_file_control(uint64_t operation, struct profile_file_snapshot *reply)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER);
  bool active = task->file_profile.flags & PROFILE_ACTIVE;
  if (operation == PROFILE_FILE_BEGIN) {
    if (active) {
      return CALL_BUSY;
    }
    task->file_profile = (struct profile_file_snapshot){.flags = PROFILE_ACTIVE};
  } else {
    if (operation == PROFILE_FILE_END) {
      if (!active) {
        return CALL_BAD_REQUEST;
      }
      task->file_profile.flags &= ~PROFILE_ACTIVE;
    } else {
      KASSERT(operation == PROFILE_FILE_SNAPSHOT);
    }
    *reply = task->file_profile;
  }
  return CALL_OK;
}

enum call_status task_profile_host_control(uint64_t operation, struct profile_host_snapshot *reply)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(task && task->kind == TASK_USER);
  bool active = task->host_profile.flags & PROFILE_ACTIVE;
  if (operation == PROFILE_HOST_BEGIN) {
    if (active) {
      return CALL_BUSY;
    }
    task->host_profile = (struct profile_host_snapshot){.flags = PROFILE_ACTIVE};
  } else {
    if (operation == PROFILE_HOST_END) {
      if (!active) {
        return CALL_BAD_REQUEST;
      }
      task->host_profile.flags &= ~PROFILE_ACTIVE;
    } else {
      KASSERT(operation == PROFILE_HOST_SNAPSHOT);
    }
    *reply = task->host_profile;
  }
  return CALL_OK;
}

static void profile_duration_merge(uint64_t *flags, struct profile_duration *duration,
    uint64_t total, uint64_t maximum)
{
  profile_add(flags, &duration->total_ns, total);
  if (maximum > duration->maximum_ns) {
    duration->maximum_ns = maximum;
  }
}

static void finish_host_profile(struct task *task)
{
  uint64_t resumed = arch_monotonic_ns();
  struct hostfs_request *request = &task->hostfs_request;
  struct hostfs_profile *sample = &request->profile;
  struct profile_host_operation *stats = request->operation == HOSTFS_READ ?
      &task->host_profile.read : &task->host_profile.write;
  uint64_t *flags = &task->host_profile.flags;
  profile_add(flags, &stats->requests, 1);
  profile_add(flags, &stats->requested_bytes, sample->requested_bytes);
  if (request->status == CALL_OK) {
    profile_add(flags, &stats->completed_bytes, request->count);
    if (request->count && request->count < sample->requested_bytes) {
      profile_add(flags, &stats->short_transfers, 1);
    }
    if (!request->count && sample->requested_bytes && request->operation == HOSTFS_READ) {
      profile_add(flags, &stats->eof, 1);
    }
  } else {
    profile_add(flags, &stats->failures, 1);
  }
  profile_duration_add(flags, &stats->publication, sample->started_ns, sample->published_ns);
  profile_duration_add(flags, &stats->bsp_queue, sample->published_ns, sample->forwarded_ns);
  profile_duration_add(flags, &stats->worker_queue, sample->forwarded_ns, sample->service_started_ns);
  profile_duration_add(flags, &stats->service, sample->service_started_ns, sample->service_ended_ns);
  profile_duration_add(flags, &stats->resume, sample->service_ended_ns, resumed);
  profile_duration_add(flags, &stats->total, sample->started_ns, resumed);
  struct virtio_fs_profile *transport = &sample->transport;
  profile_add(flags, &stats->submissions, transport->submissions);
  profile_add(flags, &stats->completions, transport->completions);
  profile_add(flags, &stats->transport_failures, transport->failures);
  profile_duration_merge(flags, &stats->transport, transport->completed_ns, transport->completed_max_ns);
  profile_duration_merge(flags, &stats->transport_failed, transport->failed_ns, transport->failed_max_ns);
  if (transport->saturated) {
    *flags |= PROFILE_SATURATED;
  }
}

static void finish_file_profile(struct task *task)
{
  uint64_t resumed = arch_monotonic_ns();
  struct profile_file_snapshot *stats = &task->file_profile;
  struct file_buffer_profile *service = &task->file_buffer_profile;
  profile_add(&stats->flags, &stats->requests, 1);
  profile_add(&stats->flags, task->file_result ? &stats->successes : &stats->failures, 1);
  profile_add(&stats->flags, &stats->requested_capacity, task->file_capacity);
  profile_add(&stats->flags, &stats->copied_bytes, service->copied_bytes);
  profile_duration_add(&stats->flags, &stats->publication, task->file_started_ns, task->file_published_ns);
  profile_duration_add(&stats->flags, &stats->queue, task->file_published_ns, task->file_service_started_ns);
  profile_duration_add(&stats->flags, &stats->service, task->file_service_started_ns, task->file_service_ended_ns);
  profile_duration_add(&stats->flags, &stats->resume, task->file_service_ended_ns, resumed);
  profile_duration_add(&stats->flags, &stats->total, task->file_started_ns, resumed);
  profile_duration_add(&stats->flags, &stats->allocation, service->allocation_started, service->allocation_ended);
  profile_duration_add(&stats->flags, &stats->copy, service->copy_started, service->copy_ended);
  profile_duration_add(&stats->flags, &stats->release, service->release_started, service->release_ended);
}

static struct task *request_launch_service(enum launch_action action,
    struct launch_capture *capture, struct launch_group *group)
{
  struct task_wait *wait = prepare_user_wait();
  struct task *task = wait->task;
  task->launch_action = action;
  task->launch_capture = capture;
  task->launch_group = group;
  task->launch_child = HANDLE_INVALID;

  lock_queues();
  task->launch_next = NULL;
  if (launch_tail) {
    launch_tail->launch_next = task;
  } else {
    launch_head = task;
  }
  launch_tail = task;
  unlock_queues();
  task_wait_sleep(wait);
  return task;
}

struct launch_capture *task_allocate_launch_capture(void)
{
  struct task *task = request_launch_service(LAUNCH_ALLOCATE, NULL, NULL);
  struct launch_capture *capture = task->launch_capture;
  task->launch_capture = NULL;
  return capture;
}

void task_discard_launch_capture(struct launch_capture *capture)
{
  request_launch_service(LAUNCH_DISCARD, capture, NULL);
}

enum call_status task_launch_process(struct launch_capture *capture, handle_t *child)
{
  struct task *task = request_launch_service(LAUNCH_START, capture, NULL);
  *child = task->launch_child;
  return task->launch_result;
}

struct launch_group *task_create_launch_group(void)
{
  struct task *task = request_launch_service(LAUNCH_GROUP_CREATE, NULL, NULL);
  struct launch_group *group = task->launch_group;
  task->launch_group = NULL;
  return group;
}

enum call_status task_prepare_launch_group(struct launch_group *group,
    struct launch_capture *capture)
{
  struct task *task = request_launch_service(LAUNCH_GROUP_PREPARE, capture, group);
  return task->launch_result;
}

void task_publish_launch_group(struct launch_group *group, handle_t *children)
{
  struct task *task = request_launch_service(LAUNCH_GROUP_PUBLISH, NULL, group);
  memcpy(children, task->launch_children, sizeof(task->launch_children));
}

void task_discard_launch_group(struct launch_group *group)
{
  request_launch_service(LAUNCH_GROUP_DISCARD, NULL, group);
}

static void service_launch_requests(void)
{
  lock_queues();
  struct task *task = launch_head;
  launch_head = launch_tail = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->launch_next;
    if (task->launch_action == LAUNCH_ALLOCATE) {
      task->launch_capture = kmalloc(sizeof(*task->launch_capture));
      if (task->launch_capture) {
        memset(task->launch_capture, 0, sizeof(*task->launch_capture));
      }
    } else if (task->launch_action == LAUNCH_GROUP_CREATE) {
      task->launch_group = launcher_group_create();
    } else if (task->launch_action == LAUNCH_GROUP_PUBLISH) {
      memset(task->launch_children, 0, sizeof(task->launch_children));
      launcher_group_publish(task->launch_group, task->launch_children);
      launcher_group_discard(task->launch_group);
      task->launch_group = NULL;
    } else if (task->launch_action == LAUNCH_GROUP_DISCARD) {
      launcher_group_discard(task->launch_group);
      task->launch_group = NULL;
    } else {
      if (task->launch_action == LAUNCH_START) {
        task->launch_result = launcher_start(task->launch_capture, task->process,
            task->cpu_index, &task->launch_child);
      } else if (task->launch_action == LAUNCH_GROUP_PREPARE) {
        task->launch_result = launcher_group_prepare(task->launch_group,
            task->launch_capture, task->process, task->cpu_index);
      } else {
        KASSERT(task->launch_action == LAUNCH_DISCARD);
      }
      kfree(task->launch_capture->host_image);
      kfree(task->launch_capture);
      task->launch_capture = NULL;
    }
    task_wait_wake(&task->wait_record);
    /* Caller regains its table and may immediately exit; do not touch task. */
    task = next;
  }
}

static struct task *dequeue(struct scheduler *scheduler)
{
  lock_queues();
  struct task *task = scheduler->ready_head;
  if (task) {
    scheduler->ready_head = task->next;
    if (!scheduler->ready_head) {
      scheduler->ready_tail = NULL;
    }
    task->next = NULL;
  }
  unlock_queues();
  return task;
}

[[noreturn]] static void enter_task(void)
{
  struct task *task = local_scheduler()->current_task;
  if (task->kind == TASK_USER) {
    arch_enter_user(task->entry, task->user_stack, task->process->startup_address);
  }

  cpu_enable_interrupts();
  task->kernel_entry(task->argument);
  cpu_disable_interrupts();
  task->exited = true;
  arch_context_switch(&task->saved_stack, local_scheduler()->stack);
  panic("resumed an exited kernel task");
}

static enum mm_result allocate_task(struct task **result)
{
  struct task *task = kmalloc(sizeof(*task));
  if (!task) {
    return MM_NO_MEMORY;
  }
  memset(task, 0, sizeof(*task));

  enum mm_result status = vm_alloc(vm_kernel_space(), TASK_STACK_SIZE,
                                    PAGE_SIZE, PAGE_WRITE, &task->kernel_stack);
  if (status != MM_OK) {
    kfree(task);
    return status;
  }

  task->saved_stack = arch_context_prepare(task->kernel_stack + TASK_STACK_SIZE,
                                           enter_task);
  *result = task;
  return MM_OK;
}

enum mm_result kernel_task_create(void (*entry)(void *), void *argument)
{
  KASSERT(arch_cpu_index() == 0);
  if (!schedulers || !entry) {
    return MM_INVALID;
  }

  struct task *task;
  enum mm_result result = allocate_task(&task);
  if (result != MM_OK) {
    return result;
  }

  task->kind = TASK_KERNEL;
  task->kernel_entry = entry;
  task->argument = argument;
  enqueue(&schedulers[0], task);
  return MM_OK;
}

enum mm_result user_task_prepare_on(size_t cpu_index, struct process *process,
                                    uintptr_t entry, uintptr_t stack_top,
                                    struct task **result)
{
  KASSERT(arch_cpu_index() == 0);
  *result = NULL;
  if (!schedulers || cpu_index >= arch_cpu_count() || !process ||
      !process->startup_address ||
      process->space != arch_cpu_at(cpu_index)->space ||
      !arch_user_entry_valid(entry, stack_top)) {
    return MM_INVALID;
  }

  struct page_translation code, stack;
  if (vm_query(process->address_space, entry, &code) != MM_OK ||
      vm_query(process->address_space, stack_top - 1, &stack) != MM_OK ||
      (code.permissions & (PAGE_USER | PAGE_EXEC)) != (PAGE_USER | PAGE_EXEC) ||
      (stack.permissions & (PAGE_USER | PAGE_WRITE)) != (PAGE_USER | PAGE_WRITE)) {
    return MM_INVALID;
  }

  struct task *task;
  enum mm_result status = allocate_task(&task);
  if (status != MM_OK) {
    return status;
  }

  task->kind = TASK_USER;
  task->process = process;
  task->entry = entry;
  task->user_stack = stack_top;
  task->cpu_index = cpu_index;
  arch_user_state_init(&task->cpu);
  *result = task;
  return MM_OK;
}

void user_task_discard_prepared(struct task *task)
{
  KASSERT(arch_cpu_index() == 0 && task && task->kind == TASK_USER);
  KASSERT(!task->bsp_request && !task->deferred_request);
  KASSERT(vm_free(vm_kernel_space(), task->kernel_stack, TASK_STACK_SIZE) == MM_OK);
  kfree(task);
}

void user_task_publish_group(struct task **tasks, size_t count)
{
  KASSERT(arch_cpu_index() == 0 && count && count <= LAUNCH_BATCH_MAX);
  size_t cpu_index = tasks[0]->cpu_index;
  lock_queues();
  for (size_t i = 0; i < count; ++i) {
    KASSERT(tasks[i] && tasks[i]->cpu_index == cpu_index);
    enqueue_locked(&schedulers[cpu_index], tasks[i]);
  }
  unlock_queues();
  notify_remote_cpu(cpu_index);
}

enum mm_result user_task_create_on(size_t cpu_index, struct process *process,
                                   uintptr_t entry, uintptr_t stack_top)
{
  struct task *task;
  enum mm_result status = user_task_prepare_on(cpu_index, process, entry,
      stack_top, &task);
  if (status != MM_OK) {
    return status;
  }
  /* Publication transfers process and stack ownership. */
  enqueue(&schedulers[cpu_index], task);
  return MM_OK;
}

enum mm_result user_task_create(struct process *process, uintptr_t entry,
                                uintptr_t stack_top)
{
  return user_task_create_on(0, process, entry, stack_top);
}

static void complete_task(struct task *task)
{
  KASSERT(!task->bsp_request && !task->deferred_request);
  lock_queues();
  task->next = completed_head;
  completed_head = task;
  unlock_queues();
  /* The BSP can free task immediately after the unlock. */
}

static void reap_completed(void)
{
  lock_queues();
  struct task *task = completed_head;
  completed_head = NULL;
  unlock_queues();

  while (task) {
    struct task *next = task->next;
    struct process_control *control = NULL;
    struct process_result result = {0};
    if (task->kind == TASK_USER) {
      /* Transfer the execution owner's reference before freeing the process. */
      control = task->process->control;
      task->process->control = NULL;
      result.kind = task->faulted ? PROCESS_FAULTED : PROCESS_EXITED;
      result.exit_status = task->faulted ? 0 : task->exit_status;
      KASSERT(process_destroy(task->process) == MM_OK);
    }
    KASSERT(vm_free(vm_kernel_space(), task->kernel_stack, TASK_STACK_SIZE) == MM_OK);
    if (task->kind == TASK_USER) {
      if (task->faulted) {
        klog("userspace: CPU %zu faulted task released\n", task->cpu_index);
      } else {
        klog("userspace: CPU %zu exited with status %d; address space released\n",
             task->cpu_index, task->exit_status);
      }
    }
    kfree(task);
    if (control) {
      process_control_complete(control, result);
      object_release(&control->object);
    }
    task = next;
  }
}

static void wake_sleepers(void)
{
  uint64_t now = arch_monotonic_ns();
  struct task **link = &sleeping_tasks;

  while (*link) {
    struct task *task = *link;
    if (now < task->sleep_deadline) {
      link = &task->next;
      continue;
    }

    *link = task->next;
    task->sleep_deadline = 0;
    enqueue(&schedulers[0], task);
  }
}

void kernel_task_sleep_until(uint64_t deadline)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && (flags & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_KERNEL && !task->exited);

  task->sleep_deadline = task_deadline_expired(deadline) ? 0 : deadline;
  arch_context_switch(&task->saved_stack, scheduler->stack);
  cpu_restore_interrupts(flags);
}

void kernel_task_yield_if_runnable(void)
{
  uint64_t flags = cpu_save_interrupts();
  KASSERT(arch_cpu_index() == 0 && (flags & RFLAGS_INTERRUPT_ENABLE));
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_KERNEL && !task->exited);

  lock_queues();
  bool runnable = scheduler->ready_head != NULL;
  unlock_queues();
  if (runnable) {
    arch_context_switch(&task->saved_stack, scheduler->stack);
  }
  cpu_restore_interrupts(flags);
}

[[noreturn]] void task_schedule(void)
{
  size_t cpu_index = arch_cpu_index();
  if (cpu_index == 0) {
    KASSERT(schedulers && !atomic_load_explicit(&started, memory_order_relaxed));
    /* Publish scheduler storage and initialized console state to every AP. */
    atomic_store_explicit(&started, true, memory_order_release);
  } else {
    while (!atomic_load_explicit(&started, memory_order_acquire)) {
      cpu_wait_interrupt();
    }
  }

  struct scheduler *scheduler = local_scheduler();
  bool idle_reported = false;
  for (;;) {
    if (cpu_index == 0) {
      expire_timed_waits();
      grow_requested_tables();
      service_namespace_requests();
      service_endpoint_requests();
      service_directory_requests();
      service_file_requests();
      service_hostfs_requests();
      service_launch_requests();
      reap_completed();
      object_reap();
      wake_sleepers();
    }

    struct task *task = dequeue(scheduler);
    scheduler->current_task = task;
    if (!task) {
      if (!idle_reported) {
        ktrace("scheduler: CPU %zu no runnable tasks, idle\n", cpu_index);
        idle_reported = true;
      }
      /* Remote ready-queue publication is followed by an IPI. With IF=0 here,
       * STI/HLT also handles a notification arriving after the empty check.
       * The timer still services deadlines and BSP-only request/cleanup queues. */
      cpu_wait_interrupt();
      continue;
    }
    if (task->kind == TASK_USER) {
      idle_reported = false;
    }

    /* Reload CR3 before touching a newly published task stack. This also
     * discards translations from a previous use of its kernel virtual range. */
    struct vm_space *address_space = task->kind == TASK_USER ?
                                    task->process->address_space : vm_kernel_space();
    KASSERT(vm_space_activate(address_space) == MM_OK);
    if (task->kind == TASK_USER) {
      arch_user_set_kernel_stack(task->kernel_stack + TASK_STACK_SIZE);
      arch_user_restore(&task->cpu);
    }
    arch_context_switch(&scheduler->stack, task->saved_stack);

    /* We are back on this CPU's permanent stack. Drop the private root and
     * flush translations before publishing completion to the BSP reaper. */
    KASSERT(vm_space_activate(vm_kernel_space()) == MM_OK);
    arch_user_set_kernel_stack(0);
    scheduler->current_task = NULL;
    if (task->exited) {
      complete_task(task);
    } else if (task->deferred_request) {
      publish_deferred_request(task);
    } else if (task->wait) {
      lock_queues();
      if (task->wait->notified) {
        task->wait = NULL;
        enqueue_locked(scheduler, task);
      } else {
        task->parked = true;
      }
      unlock_queues();
    } else if (task->sleep_deadline) {
      task->next = sleeping_tasks;
      sleeping_tasks = task;
    } else {
      enqueue(scheduler, task);
    }
  }
}

void task_preempt(bool user_mode)
{
  /* AP kernel execution remains non-preemptible, including startup waits.
   * It must not inspect scheduler storage before acquiring started. */
  if (!user_mode && arch_cpu_index() != 0) {
    return;
  }
  if (!schedulers) {
    return;
  }

  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  if (!task) {
    return;
  }
  KASSERT(!task->exited);
  if (task->kind == TASK_USER && !user_mode) {
    return;
  }

  if (arch_cpu_index() == 0) {
    expire_timed_waits();
    wake_sleepers();
  }

  lock_queues();
  bool schedule_needed = scheduler->ready_head != NULL ||
    (arch_cpu_index() == 0 &&
     (completed_head != NULL || growth_head != NULL ||
      endpoint_head != NULL || namespace_head != NULL ||
      directory_head != NULL ||
      file_head != NULL || launch_head != NULL || hostfs_head != NULL));
  unlock_queues();
  if (arch_cpu_index() == 0 && object_reap_pending()) {
    schedule_needed = true;
  }
  if (!schedule_needed) {
    return;
  }

  if (task->kind == TASK_USER) {
    arch_user_save(&task->cpu);
  }
  arch_context_switch(&task->saved_stack, scheduler->stack);
}

[[noreturn]] void user_exit(int status)
{
  struct scheduler *scheduler = local_scheduler();
  struct task *task = scheduler->current_task;
  KASSERT(task && task->kind == TASK_USER && !task->exited);
  task->exit_status = status;
  task->exited = true;
  arch_context_switch(&task->saved_stack, scheduler->stack);
  panic("resumed an exited user task");
}

[[noreturn]] void user_fault(void)
{
  struct task *task = local_scheduler()->current_task;
  KASSERT(task);
  task->faulted = true;
  user_exit(-1);
}
