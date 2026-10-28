#include <kernel/object/wifi.h>
#include <kernel/object/wifi_connection.h>
#include <kernel/object/clipboard.h>
#include <kernel/object/environment.h>
#include <kernel/completion.h>
#include <kernel/bulk.h>
#include <kernel/object/endpoint.h>
#include <kernel/object/keyboard.h>
#include <kernel/object/gamepad.h>
#include <kernel/object/pointer.h>
#include <kernel/object/audio.h>
#include <kernel/object/bluetooth_hci.h>
#include <arch/smp.h>
#include <arch/paging.h>
#include <arch/cpu_local.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/vm.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/memory.h>
#include <kernel/panic.h>
#include <kernel/object/process.h>
#include <kernel/object/execution_group.h>
#include <kernel/object/display.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <kernel/user/readiness_set.h>
#include <kernel/user/launch.h>

/* Tunable storage policy, including the initial and retiring threads. */
#define PROCESS_THREAD_LIMIT 256
#define PROCESS_RESULT_KIND_SHIFT 32
#define PROCESS_RESULT_KIND_MASK UINT64_C(0xffffffff00000000)
#define PROCESS_RESULT_STATUS_MASK UINT64_C(0x00000000ffffffff)

_Static_assert(__atomic_always_lock_free(sizeof(atomic_uint_fast64_t), 0),
               "process result election must be fault-safe");

enum mm_result process_create(struct space *space, struct vm_space *address_space,
                              struct process **result)
{
  KASSERT(!cpu_interrupts_enabled());
  KASSERT(!vm_kernel_runtime() || cpu_current()->active_space == arch_kernel_space());
  if (result) {
    *result = NULL;
  }
  if (!result || !space || !address_space || address_space == vm_kernel_space()) {
    return MM_INVALID;
  }

  struct process *process = kmalloc(sizeof(*process));
  if (!process) {
    return MM_NO_MEMORY;
  }
  *process = (struct process){
    .space = space, .address_space = address_space, .lifetime = PROCESS_PREPARING,
    .preparation_actor = task_current_actor(), .preparation_cpu = arch_cpu_index(),
    .retirement_cpu = arch_cpu_index(),
  };
  atomic_init(&process->lifetime_lock.locked, false);
  if (!process_vm_activity_init(process)) {
    kfree(process);
    return MM_NO_MEMORY;
  }
  atomic_init(&process->terminal_result, 0);
  atomic_init(&process->completion_gate, NULL);
  atomic_init(&process->bulk_gate, NULL);
  process->group_member.process = process;
  process->environment = environment_table_create();
  if (!process->environment) {
    kfree(process);
    return MM_NO_MEMORY;
  }
  process->control = process_control_create();
  if (!process->control) {
    environment_table_destroy(process->environment);
    kfree(process);
    return MM_NO_MEMORY;
  }
  process_control_attach_process(process->control, process);
  *result = process;
  return MM_OK;
}

/* Pages, range records or page-table entries per interrupts-off teardown
 * step. Tuning, not a contract. */
#define TEARDOWN_STEP_BUDGET 256

/* Fixed device hooks stay on the BSP. RAM work has a separate immutable CPU
 * destination; queue links and waiter installation never allocate. */
static struct spinlock reaper_guard;
static struct process *retired_head, *retired_tail;
static struct task_wait *reaper_wait;

struct ram_reaper {
  struct spinlock guard;
  struct process *first, *last;
  struct task_wait *wait;
  bool notified;
  size_t cpu;
};
static struct ram_reaper *ram_reapers;
static void ram_reaper(void *argument);

void process_prepare_require(struct process *process)
{
  KASSERT(!cpu_interrupts_enabled() && process && process->lifetime == PROCESS_PREPARING);
  KASSERT(process->preparation_cpu == arch_cpu_index());
  if (process->preparation_transferred) {
    KASSERT(ram_reapers && kernel_task_is_current(ram_reaper,
        &ram_reapers[process->preparation_cpu]));
  } else {
    KASSERT(process->preparation_actor == task_current_actor());
    KASSERT(vm_kernel_runtime() ? cpu_current()->active_space == arch_kernel_space() :
        arch_cpu_index() == 0);
  }
}

void process_prepare_transfer(struct process *process)
{
  process_prepare_require(process);
  KASSERT(!process->preparation_transferred);
  process->preparation_actor = NULL;
  process->preparation_transferred = true;
}

void process_cleanup_notify(void)
{
  KASSERT(!cpu_interrupts_enabled());
  spin_lock(&reaper_guard);
  if (reaper_wait) {
    struct task_wait *wait = reaper_wait;
    reaper_wait = NULL;
    task_wait_wake(wait);
  }
  spin_unlock(&reaper_guard);
}

void process_ram_cleanup_notify(size_t cpu)
{
  KASSERT(!cpu_interrupts_enabled() && ram_reapers && cpu < arch_cpu_count());
  struct ram_reaper *reaper = &ram_reapers[cpu];
  spin_lock(&reaper->guard);
  reaper->notified = true;
  if (reaper->wait) {
    struct task_wait *wait = reaper->wait;
    reaper->wait = NULL;
    task_wait_wake(wait);
  }
  spin_unlock(&reaper->guard);
}

static void queue_fixed_cleanup(struct process *process)
{
  spin_lock(&reaper_guard);
  KASSERT(!process->retired_next);
  if (retired_tail) {
    retired_tail->retired_next = process;
  } else {
    retired_head = process;
  }
  retired_tail = process;
  spin_unlock(&reaper_guard);
  process_cleanup_notify();
}

static void queue_ram_cleanup(struct process *process)
{
  size_t cpu = process->retirement_cpu;
  struct ram_reaper *reaper = &ram_reapers[cpu];
  spin_lock(&reaper->guard);
  KASSERT(!process->retired_next);
  if (reaper->last) {
    reaper->last->retired_next = process;
  } else {
    reaper->first = process;
  }
  reaper->last = process;
  spin_unlock(&reaper->guard);
  process_ram_cleanup_notify(cpu);
}

/* Closes VM admission and ends device sessions. */
static void release_sessions(struct process *process)
{
  process_vm_retire(process);
  wifi_process_exit(process);
  wifi_connection_process_exit(process);
  clipboard_process_exit(process);
  keyboard_process_exit(process);
  gamepad_process_exit(process);
  pointer_process_exit(process);
  bluetooth_hci_process_exit(process);
  display_process_exit(process);
  audio_process_exit(process);
}

/* After the address space is gone; frees the process. */
static void release_records(struct process *process)
{
  private_memory_discard_records(process);
  environment_table_destroy(process->environment);
  capability_table_destroy(&process->capabilities);
  if (process->control) {
    process_control_detach_process(process->control);
    object_release(&process->control->object);
  }
  if (process->execution_group) {
    /* Unpublished preparation. The reaper takes published membership first. */
    object_release(&process->execution_group->object);
  }
  kfree(process);
}

static void release_private_vm(struct process *process)
{
  KASSERT(vm_space_retire(process->address_space) == MM_OK);
  while (!vm_space_release_step(process->address_space, TEARDOWN_STEP_BUDGET)) {
    if (task_current_actor()) {
      task_yield_masked();
    }
  }
  process->address_space = NULL;
}

enum mm_result process_destroy(struct process *process)
{
  if (!process || process->lifetime != PROCESS_PREPARING || process->task_storage_count) {
    return MM_INVALID;
  }
  process_prepare_require(process);
  KASSERT(!process->tasks && !process->task_count && !process->cleanup_pins);
  /* An unpublished child has never acquired a device session or registered a
   * receiving endpoint. Its provisioning handles still close normally below. */
  KASSERT(!process->endpoints && !process->paste_receiver && !process->readiness_account);
  KASSERT(!atomic_load_explicit(&process->completion_gate, memory_order_relaxed));
  KASSERT(!atomic_load_explicit(&process->bulk_gate, memory_order_relaxed));
  process_vm_retire(process);
  release_private_vm(process);
  release_records(process);
  return MM_OK;
}

bool process_task_available(struct process *process)
{
  process_prepare_require(process);
  return process->lifetime == PROCESS_PREPARING && !process->task_count &&
    !process->task_storage_count;
}

static void attach_task_locked(struct process *process,
    struct process_task_member *member, struct task *task)
{
  KASSERT(member && task && !member->next && !member->task);
  KASSERT(process->task_storage_count < PROCESS_THREAD_LIMIT);
  member->task = task;
  member->next = process->tasks;
  process->tasks = member;
  ++process->task_count;
  ++process->departing_tasks;
  ++process->task_storage_count;
}

void process_task_attach(struct process *process, struct process_task_member *member,
                         struct task *task)
{
  KASSERT(task && process_task_available(process));
  spin_lock(&process->lifetime_lock);
  attach_task_locked(process, member, task);
  spin_unlock(&process->lifetime_lock);
}

void process_task_discard(struct process *process, struct process_task_member *member)
{
  process_prepare_require(process);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_PREPARING && process->tasks == member &&
      process->task_count == 1 && process->task_storage_count == 1);
  process->tasks = NULL;
  process->task_count = 0;
  KASSERT(process->departing_tasks == 1);
  process->departing_tasks = 0;
  process->task_storage_count = 0;
  member->next = NULL;
  member->task = NULL;
  spin_unlock(&process->lifetime_lock);
}

void process_publish(struct process *process)
{
  process_prepare_require(process);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_PREPARING && process->tasks &&
      process->task_count == 1 && process->task_storage_count == 1);
  process->lifetime = PROCESS_SUBMITTED;
  spin_unlock(&process->lifetime_lock);
}

enum call_status process_thread_attach_locked(struct process *process,
    struct process_task_member *member, struct task *task)
{
  KASSERT(!cpu_interrupts_enabled());
  if (process->lifetime != PROCESS_SUBMITTED || process->stopping ||
      process_result_committed(process)) {
    return CALL_ENDPOINT_CLOSED;
  }
  if (process->task_storage_count >= PROCESS_THREAD_LIMIT) {
    return CALL_LIMIT;
  }
  attach_task_locked(process, member, task);
  return CALL_OK;
}

void process_request_stop(struct process *process)
{
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime != PROCESS_PREPARING);
  if (!process->stopping) {
    process->stopping = true;
    process_set_result(process, (struct process_result){.kind = PROCESS_TERMINATED});
    for (struct process_task_member *member = process->tasks;
         member; member = member->next) {
      task_request_stop(member->task);
    }
  }
  spin_unlock(&process->lifetime_lock);
  readiness_set_process_stop(process);
}

void process_set_result(struct process *process, struct process_result result)
{
  KASSERT(process->lifetime == PROCESS_SUBMITTED);
  KASSERT(result.kind == PROCESS_EXITED || result.kind == PROCESS_FAULTED ||
      result.kind == PROCESS_TERMINATED || result.kind == PROCESS_START_FAILED);
  KASSERT(result.exit_status == (int32_t)result.exit_status &&
      (result.kind == PROCESS_EXITED ||
       (result.kind == PROCESS_START_FAILED ?
        process_start_failure_valid(result.exit_status) : !result.exit_status)));
  uint_fast64_t elected = result.kind << PROCESS_RESULT_KIND_SHIFT |
    ((uint_fast64_t)(uint32_t)result.exit_status & PROCESS_RESULT_STATUS_MASK);
  uint_fast64_t expected = 0;
  atomic_compare_exchange_strong_explicit(&process->terminal_result, &expected,
      elected, memory_order_release, memory_order_relaxed);
}

bool process_result_committed(struct process *process)
{
  return atomic_load_explicit(&process->terminal_result, memory_order_acquire) != 0;
}

bool process_result_get(struct process *process, struct process_result *result)
{
  KASSERT(result);
  uint_fast64_t elected = atomic_load_explicit(&process->terminal_result,
      memory_order_acquire);
  *result = (struct process_result){
    .kind = (elected & PROCESS_RESULT_KIND_MASK) >> PROCESS_RESULT_KIND_SHIFT,
    .exit_status = (int32_t)(elected & PROCESS_RESULT_STATUS_MASK),
  };
  return elected != 0;
}

void process_task_depart(struct process *process, size_t departure_cpu)
{
  KASSERT(!cpu_interrupts_enabled() && departure_cpu == arch_cpu_index());
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED && process->departing_tasks);
  if (!--process->departing_tasks) {
    process->retirement_cpu = departure_cpu;
  }
  spin_unlock(&process->lifetime_lock);
}

void process_task_detach(struct process *process, struct process_task_member *member)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED && process->task_count &&
      process->task_storage_count >= process->task_count);
  struct process_task_member **link = &process->tasks;
  while (*link != member) {
    KASSERT(*link);
    link = &(*link)->next;
  }
  *link = member->next;
  member->next = NULL;
  member->task = NULL;
  --process->task_count;
  bool last = !process->task_count;
  if (last) {
    KASSERT(!process->departing_tasks);
    process->stopping = true;
    if (!process_result_committed(process)) {
      process_set_result(process, (struct process_result){.kind = PROCESS_EXITED});
    }
  }
  spin_unlock(&process->lifetime_lock);
  if (last) {
    /* No remaining task can hold a root, continuation or pending request. */
    readiness_set_process_stop(process);
    process_vm_retire(process);
    wifi_process_exit(process);
    wifi_connection_process_exit(process);
  }
}

static void queue_retired_process(struct process *process)
{
  /* Waiters keep the control and the group keeps its member count until the
   * reaper has released the address space and resources. */
  process_control_detach_process(process->control);
  process->retired_control = process->control;
  process->control = NULL;
  if (process->execution_group) {
    execution_group_member_detach(process->execution_group, &process->group_member);
    process->retired_group = process->execution_group;
    process->execution_group = NULL;
  }

  process->cleanup_phase = PROCESS_CLEANUP_SESSIONS;
  queue_fixed_cleanup(process);
}

bool process_cleanup_pin(struct process *process)
{
  KASSERT(!cpu_interrupts_enabled());
  spin_lock(&process->lifetime_lock);
  bool pinned = process->lifetime == PROCESS_SUBMITTED &&
      process->cleanup_pins != SIZE_MAX;
  if (pinned) {
    ++process->cleanup_pins;
  }
  spin_unlock(&process->lifetime_lock);
  return pinned;
}

void process_cleanup_unpin(struct process *process)
{
  KASSERT(!cpu_interrupts_enabled());
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED && process->cleanup_pins);
  --process->cleanup_pins;
  bool retired = !process->cleanup_pins && !process->task_storage_count;
  if (retired) {
    KASSERT(!process->tasks && !process->task_count && process_result_committed(process));
    process->lifetime = PROCESS_RETIRED;
  }
  spin_unlock(&process->lifetime_lock);
  if (retired) {
    queue_retired_process(process);
  }
}

bool process_task_reclaimed(struct process *process)
{
  KASSERT(!cpu_interrupts_enabled());
  spin_lock(&process->lifetime_lock);
  KASSERT(process->lifetime == PROCESS_SUBMITTED &&
      process->task_storage_count > process->task_count);
  --process->task_storage_count;
  if (process->task_storage_count || process->cleanup_pins) {
    spin_unlock(&process->lifetime_lock);
    return false;
  }
  KASSERT(!process->tasks && !process->task_count && process_result_committed(process));
  process->lifetime = PROCESS_RETIRED;
  spin_unlock(&process->lifetime_lock);
  queue_retired_process(process);
  return true;
}

/* Device phases retain their original BSP owner and ordering. */
static void reclaim_fixed(struct process *process)
{
  struct vm_kernel_owner owner = {0};
  vm_kernel_owner_cleanup(&owner);
  uint64_t flags = cpu_save_interrupts();
  struct execution_group *previous = object_cleanup_enter(process->retired_group);
  if (process->cleanup_phase == PROCESS_CLEANUP_SESSIONS) {
    release_sessions(process);
    process->cleanup_phase = PROCESS_CLEANUP_VM;
  } else {
    KASSERT(process->cleanup_phase == PROCESS_CLEANUP_HOOKS && !process->address_space);
    readiness_set_process_exit(process);
    endpoint_process_exit(process);
    native_completion_process_exit(process);
    bulk_process_exit(process);
    process->cleanup_phase = PROCESS_CLEANUP_RECORDS;
  }
  object_cleanup_leave(previous);
  vm_kernel_owner_release(&owner);
  queue_ram_cleanup(process);
  cpu_restore_interrupts(flags);
}

static void reclaim_ram(struct process *process)
{
  KASSERT(process->retirement_cpu == arch_cpu_index());
  struct process_control *control = process->retired_control;
  struct execution_group *group = process->retired_group;
  uint64_t flags = cpu_save_interrupts();
  struct execution_group *previous = object_cleanup_enter(group);
  if (process->cleanup_phase == PROCESS_CLEANUP_VM) {
    release_private_vm(process);
    process->cleanup_phase = PROCESS_CLEANUP_HOOKS;
    queue_fixed_cleanup(process);
  } else {
    KASSERT(process->cleanup_phase == PROCESS_CLEANUP_RECORDS);
    struct process_result result;
    KASSERT(process_result_get(process, &result));
    release_records(process);
    process_control_complete(control, result);
    object_release(&control->object);
    object_cleanup_leave(previous);
    if (group) {
      execution_group_member_complete(group);
    }
    cpu_restore_interrupts(flags);
    return;
  }
  object_cleanup_leave(previous);
  cpu_restore_interrupts(flags);
}

static void ram_reaper(void *argument)
{
  struct ram_reaper *reaper = argument;
  KASSERT(reaper->cpu == arch_cpu_index());
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    spin_lock(&reaper->guard);
    struct process *process = reaper->first;
    if (process) {
      reaper->first = process->retired_next;
      if (!reaper->first) {
        reaper->last = NULL;
      }
      process->retired_next = NULL;
    }
    bool notified = reaper->notified;
    reaper->notified = false;
    if (!process && !notified && !task_ram_retire_pending() &&
        !vm_kernel_ram_retire_pending() && !launcher_ram_retire_pending()) {
      struct task_wait *wait = task_wait_prepare();
      reaper->wait = wait;
      spin_unlock(&reaper->guard);
      task_wait_sleep(wait);
      cpu_restore_interrupts(flags);
      continue;
    }
    spin_unlock(&reaper->guard);
    cpu_restore_interrupts(flags);
    task_reap_ram();
    if (process) {
      reclaim_ram(process);
    }
    vm_kernel_reap_ram();
    launcher_reap_prepared_ram();
    kernel_task_yield_if_runnable();
  }
}

static void reaper(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    spin_lock(&reaper_guard);
    struct process *process = retired_head;
    if (!process && !vm_kernel_retire_pending() && !display_retire_pending()) {
      struct task_wait *wait = task_wait_prepare();
      reaper_wait = wait;
      spin_unlock(&reaper_guard);
      task_wait_sleep(wait);
      cpu_restore_interrupts(flags);
      continue;
    }
    if (process) {
      retired_head = process->retired_next;
      if (!retired_head) {
        retired_tail = NULL;
      }
      process->retired_next = NULL;
    }
    spin_unlock(&reaper_guard);
    cpu_restore_interrupts(flags);
    if (process) {
      reclaim_fixed(process);
    }
    vm_kernel_reap();
    display_reap_frames();
  }
}

void process_reaper_init(void)
{
  KASSERT(arch_cpu_index() == 0);
  size_t count = arch_cpu_count();
  ram_reapers = kmalloc(count * sizeof(*ram_reapers));
  if (!ram_reapers) {
    panic("cannot allocate RAM reaper owners");
  }
  for (size_t cpu = 0; cpu < count; ++cpu) {
    ram_reapers[cpu] = (struct ram_reaper){.cpu = cpu};
    atomic_init(&ram_reapers[cpu].guard.locked, false);
  }
  task_ram_reapers_init();
  vm_kernel_ram_reapers_init();
  launcher_ram_reapers_init();
  for (size_t cpu = 0; cpu < count; ++cpu) {
    enum mm_result result = kernel_ram_reaper_create(cpu, ram_reaper, &ram_reapers[cpu]);
    if (result != MM_OK) {
      panic("cannot create CPU %zu RAM reaper (error %u)", cpu, (unsigned)result);
    }
  }
  enum mm_result result = kernel_task_create(reaper, NULL);
  if (result != MM_OK) {
    panic("cannot create process reaper (error %u)", (unsigned)result);
  }
}
