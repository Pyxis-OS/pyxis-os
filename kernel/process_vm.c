#include <kernel/object/forwarder.h>
#include <arch/clock.h>
#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/object/display.h>
#include <kernel/object/memory.h>
#include <kernel/object/jit.h>
#include <kernel/object/thread.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/mm/vm.h>
#include <kernel/service/request.h>
#include <kernel/task.h>

/* A quiescence budget, not a public operation deadline or ABI promise. */
#define VM_QUIESCENCE_NS UINT64_C(1000000000)

static atomic_uint_fast64_t next_incarnation = 1;

bool process_vm_activity_init(struct process *process)
{
  uint64_t value = atomic_load_explicit(&next_incarnation, memory_order_relaxed);
  for (;;) {
    if (!value) {
      return false;
    }
    if (atomic_compare_exchange_weak_explicit(&next_incarnation, &value,
        value == UINT64_MAX ? 0 : value + 1, memory_order_relaxed,
        memory_order_relaxed)) {
      break;
    }
  }
  atomic_init(&process->vm.lock.locked, false);
  atomic_init(&process->vm.phase, PROCESS_VM_OPEN);
  process->vm.incarnation = value;
  process->vm.generation = 1;
  return true;
}

static bool resident(const struct process *process, size_t cpu)
{
  KASSERT(cpu < ARCH_CPU_LIMIT);
  return process->vm.resident_cpus[cpu / 64] & (UINT64_C(1) << (cpu % 64));
}

static bool residency_empty(const struct process *process)
{
  for (size_t i = 0; i < sizeof(process->vm.resident_cpus) /
      sizeof(process->vm.resident_cpus[0]); ++i) {
    if (process->vm.resident_cpus[i]) {
      return false;
    }
  }
  return true;
}

static bool access_allowed(struct process *process, struct task *task)
{
  return process->vm.phase == PROCESS_VM_OPEN ||
    (process->vm.phase == PROCESS_VM_DRAINING && task_vm_continuation(task)) ||
    (process->vm.phase == PROCESS_VM_REPLY && process->vm.mutator == task);
}

static void external_ready(struct process *process)
{
  struct process_vm_external *external = process->vm.external;
  if (external && external->state == PROCESS_VM_EXTERNAL_QUEUED &&
      !process->vm.mutator && !process->vm.pending && !process->vm.roots &&
      !process->vm.borrowers && residency_empty(process)) {
    KASSERT(process->vm.phase == PROCESS_VM_DRAINING ||
        process->vm.phase == PROCESS_VM_RETIRED);
    shared_process_vm_ready(external);
  }
}

bool process_vm_dispatch(struct process *process, struct task *task,
                          struct process_vm_root_epoch *epoch)
{
  spin_lock(&process->vm.lock);
  bool admitted = access_allowed(process, task);
  if (admitted) {
    KASSERT(process->vm.roots != SIZE_MAX);
    size_t cpu = arch_cpu_index();
    KASSERT(process->vm.incarnation && process->vm.generation && !resident(process, cpu));
    process->vm.resident_cpus[cpu / 64] |= UINT64_C(1) << (cpu % 64);
    ++process->vm.roots;
    *epoch = (struct process_vm_root_epoch){process->vm.incarnation,
                                            process->vm.generation};
  } else {
    struct process_vm_held *held = task_vm_hold(task);
    held->next = process->vm.held;
    process->vm.held = held;
  }
  spin_unlock(&process->vm.lock);
  return admitted;
}

void process_vm_root_depart(struct process *process, struct process_vm_root_epoch epoch)
{
  spin_lock(&process->vm.lock);
  size_t cpu = arch_cpu_index();
  KASSERT(process->vm.roots && resident(process, cpu) &&
      process->vm.incarnation == epoch.incarnation &&
      process->vm.generation == epoch.generation);
  process->vm.resident_cpus[cpu / 64] &= ~(UINT64_C(1) << (cpu % 64));
  --process->vm.roots;
  if (process->vm.local_wait) {
    struct task_wait *wait = process->vm.local_wait;
    process->vm.local_wait = NULL;
    /* VM guard joins this access with timeout detachment before record reuse. */
    task_wait_wake(wait);
  }
  external_ready(process);
  spin_unlock(&process->vm.lock);
}

bool process_vm_execution_closed(struct process *process)
{
  /* Timer entry only observes closure; admission itself still uses the guard. */
  return atomic_load_explicit(&process->vm.phase, memory_order_acquire) != PROCESS_VM_OPEN;
}

bool process_vm_syscall_enter(struct process *process, struct task *task)
{
  spin_lock(&process->vm.lock);
  bool admitted = process->vm.phase == PROCESS_VM_OPEN;
  if (admitted) {
    KASSERT(!task_vm_continuation(task));
    task_vm_set_continuation(task, true);
  }
  spin_unlock(&process->vm.lock);
  return admitted;
}

void process_vm_syscall_leave(struct process *process, struct task *task)
{
  spin_lock(&process->vm.lock);
  KASSERT(task_vm_continuation(task) && process->vm.mutator != task);
  KASSERT(!task_vm_borrow_count(task));
  task_vm_set_continuation(task, false);
  spin_unlock(&process->vm.lock);
}

bool process_vm_borrow_acquire(struct process *process)
{
  struct task *task = task_current_user();
  if (!process || !task || process != process_current()) {
    return false;
  }
  spin_lock(&process->vm.lock);
  bool admitted = access_allowed(process, task);
  if (admitted) {
    KASSERT(process->vm.roots && resident(process, arch_cpu_index()) &&
        process->vm.borrowers != SIZE_MAX);
    ++process->vm.borrowers;
    task_vm_borrow_enter(task);
  }
  spin_unlock(&process->vm.lock);
  return admitted;
}

void process_vm_borrow_release(struct process *process)
{
  struct task *task = task_current_user();
  KASSERT(task && process == process_current());
  spin_lock(&process->vm.lock);
  KASSERT(process->vm.borrowers);
  task_vm_borrow_leave(task);
  --process->vm.borrowers;
  spin_unlock(&process->vm.lock);
  /* Every borrower retains an active root; only root departure can finish
   * drainage. Keep this guard a leaf for copies under object ownership. */
}

static void resume_held(struct process_vm_held *held)
{
  while (held) {
    struct process_vm_held *next = held->next;
    held->next = NULL;
    task_vm_resume(held->task);
    /* Resume transfers task storage; only the saved next pointer remains ours. */
    held = next;
  }
}

void process_vm_unhold(struct process *process, struct task *task)
{
  spin_lock(&process->vm.lock);
  struct process_vm_held **link = &process->vm.held;
  while (*link && (*link)->task != task) {
    link = &(*link)->next;
  }
  struct process_vm_held *held = *link;
  if (held) {
    *link = held->next;
    held->next = NULL;
  }
  spin_unlock(&process->vm.lock);
  if (held) {
    task_vm_resume(task);
  }
}

static void refuse_mutation(struct bsp_request *request, enum call_status status)
{
  if (request->service == BSP_SERVICE_MEMORY) {
    struct memory_request *memory = (void *)request;
    memory->status = status;
    memory->process = NULL;
  } else if (request->service == BSP_SERVICE_JIT) {
    struct jit_request *jit = (void *)request;
    jit->status = status;
    jit->process = NULL;
  } else if (request->service == BSP_SERVICE_THREAD) {
    struct thread_request *thread = (void *)request;
    thread->status = status;
    thread->process = NULL;
    thread->caller = NULL;
  } else {
    KASSERT(request->service == BSP_SERVICE_DISPLAY);
    struct display_request *display = (void *)((char *)request -
        offsetof(struct display_request, vm_request));
    display->result = status;
    display->loan = NULL;
    display->display = NULL;
  }
  request->vm_process = NULL;
  request->vm_mutator = NULL;
}

void process_vm_require_mutation(struct process *process)
{
  spin_lock(&process->vm.lock);
  KASSERT(arch_cpu_index() == process->vm.mutation_cpu);
  struct task *actor = task_current_user();
  KASSERT(actor && task_vm_kernel_active() && actor == process->vm.mutator);
  KASSERT(process->vm.phase == PROCESS_VM_MUTATING && !process->vm.roots &&
      !process->vm.borrowers && residency_empty(process) &&
      process->vm.pending && process->vm.mutator);
  spin_unlock(&process->vm.lock);
}

bool process_vm_buffer_check(struct process *process, uintptr_t address,
                             size_t bytes, bool write)
{
  process_vm_require_mutation(process);
  if (!bytes) {
    return true;
  }
  if (address < PAGE_SIZE || bytes - 1 > UINTPTR_MAX - address) {
    return false;
  }
  uintptr_t page = address & ~(PAGE_SIZE - 1);
  uintptr_t last = (address + bytes - 1) & ~(PAGE_SIZE - 1);
  unsigned required = PAGE_USER | (write ? PAGE_WRITE : 0);
  for (;;) {
    struct page_translation translation;
    if (vm_query(process->address_space, page, &translation) != MM_OK ||
        (translation.permissions & required) != required) {
      return false;
    }
    if (page == last) {
      return true;
    }
    page += PAGE_SIZE;
  }
}

void process_vm_reply_finish(struct process *process)
{
  struct task *task = task_current_user();
  spin_lock(&process->vm.lock);
  KASSERT(process->vm.phase == PROCESS_VM_REPLY && process->vm.mutator == task &&
      !process->vm.pending && !process->vm.borrowers);
  process->vm.mutator = NULL;
  process->vm.phase = process->vm.external ? PROCESS_VM_DRAINING : PROCESS_VM_OPEN;
  task_vm_set_continuation(task, true);
  struct process_vm_held *held = NULL;
  if (!process->vm.external) {
    held = process->vm.held;
    process->vm.held = NULL;
  }
  /* The reply's dispatched root still owns its captured generation. External
   * acquisition waits for its real permanent-stack departure, not just reply. */
  external_ready(process);
  spin_unlock(&process->vm.lock);
  resume_held(held);
}

void process_vm_external_request(struct process *process,
    struct process_vm_external *external, size_t cpu)
{
  KASSERT(!cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  KASSERT(process && external && cpu < arch_cpu_count());
  spin_lock(&process->vm.lock);
  KASSERT(!process->vm.external && !external->process && !external->actor &&
      external->state == PROCESS_VM_EXTERNAL_IDLE);
  *external = (struct process_vm_external){
    .process = process, .cpu = cpu, .state = PROCESS_VM_EXTERNAL_QUEUED,
  };
  process->vm.external = external;
  if (process->vm.phase == PROCESS_VM_OPEN) {
    KASSERT(!process->vm.pending && !process->vm.mutator && !process->vm.local_wait);
    process->vm.phase = PROCESS_VM_DRAINING;
  }
  external_ready(process);
  spin_unlock(&process->vm.lock);
}

enum process_vm_external_progress
process_vm_external_acquire(struct process_vm_external *external)
{
  KASSERT(!cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  struct task *actor = task_current_actor();
  KASSERT(external && external->process && actor && !task_current_user());
  struct process *process = external->process;
  spin_lock(&process->vm.lock);
  KASSERT(process->vm.external == external && external->cpu == arch_cpu_index());
  if (external->state == PROCESS_VM_EXTERNAL_OWNED ||
      external->state == PROCESS_VM_EXTERNAL_OWNED_FINAL) {
    KASSERT(external->actor == actor);
    enum process_vm_external_progress progress =
        external->state == PROCESS_VM_EXTERNAL_OWNED_FINAL ?
        PROCESS_VM_EXTERNAL_FINAL : PROCESS_VM_EXTERNAL_READY;
    spin_unlock(&process->vm.lock);
    return progress;
  }
  KASSERT(external->state == PROCESS_VM_EXTERNAL_QUEUED && !external->actor);
  if (process->vm.mutator || process->vm.pending || process->vm.roots ||
      process->vm.borrowers || !residency_empty(process)) {
    spin_unlock(&process->vm.lock);
    return PROCESS_VM_EXTERNAL_WAIT;
  }
  KASSERT(!process->vm.local_wait);
  bool retired = process->vm.phase == PROCESS_VM_RETIRED;
  KASSERT(retired || process->vm.phase == PROCESS_VM_DRAINING);
  if (!retired && process->vm.generation == UINT64_MAX) {
    spin_unlock(&process->vm.lock);
    return PROCESS_VM_EXTERNAL_EXHAUSTED;
  }
  if (!retired) {
    ++process->vm.generation;
    process->vm.phase = PROCESS_VM_MUTATING;
  }
  external->actor = actor;
  external->state = retired ? PROCESS_VM_EXTERNAL_OWNED_FINAL : PROCESS_VM_EXTERNAL_OWNED;
  process->vm.mutation_cpu = arch_cpu_index();
  spin_unlock(&process->vm.lock);
  return retired ? PROCESS_VM_EXTERNAL_FINAL : PROCESS_VM_EXTERNAL_READY;
}

static void require_external_locked(struct process_vm_external *external)
{
  struct process *process = external->process;
  KASSERT(process->vm.external == external && external->actor == task_current_actor() &&
      !task_current_user() && external->cpu == arch_cpu_index() &&
      process->vm.mutation_cpu == arch_cpu_index());
  KASSERT(!process->vm.mutator && !process->vm.pending && !process->vm.local_wait &&
      !process->vm.roots && !process->vm.borrowers && residency_empty(process));
  KASSERT((external->state == PROCESS_VM_EXTERNAL_OWNED &&
      process->vm.phase == PROCESS_VM_MUTATING) ||
      (external->state == PROCESS_VM_EXTERNAL_OWNED_FINAL &&
      process->vm.phase == PROCESS_VM_RETIRED));
}

void process_vm_require_external(struct process_vm_external *external)
{
  KASSERT(!cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  KASSERT(external && external->process);
  struct process *process = external->process;
  spin_lock(&process->vm.lock);
  require_external_locked(external);
  spin_unlock(&process->vm.lock);
}

bool process_vm_external_finish(struct process_vm_external *external, bool reopen)
{
  KASSERT(!cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  KASSERT(external && external->process);
  struct process *process = external->process;
  spin_lock(&process->vm.lock);
  require_external_locked(external);
  process->vm.external = NULL;
  bool retired = process->vm.phase == PROCESS_VM_RETIRED;
  struct process_vm_held *held = NULL;
  if (!retired) {
    process->vm.phase = reopen ? PROCESS_VM_OPEN : PROCESS_VM_DRAINING;
    /* A stopped continuation may have been held again after stop fanout while
     * this owner was mutating. Let it drain cleanup without reopening user entry. */
    held = process->vm.held;
    process->vm.held = NULL;
  }
  *external = (struct process_vm_external){0};
  spin_unlock(&process->vm.lock);
  resume_held(held);
  return retired;
}

void process_vm_retire(struct process *process)
{
  KASSERT(!cpu_interrupts_enabled());
  spin_lock(&process->vm.lock);
  KASSERT((process->vm.phase == PROCESS_VM_OPEN ||
      process->vm.phase == PROCESS_VM_DRAINING ||
      process->vm.phase == PROCESS_VM_RETIRED ||
      (process->vm.phase == PROCESS_VM_MUTATING && process->vm.external &&
      process->vm.external->state == PROCESS_VM_EXTERNAL_OWNED)) && !process->vm.roots &&
      !process->vm.borrowers && !process->vm.pending && !process->vm.mutator &&
      !process->vm.held && residency_empty(process));
  process->vm.phase = PROCESS_VM_RETIRED;
  if (process->vm.external &&
      process->vm.external->state == PROCESS_VM_EXTERNAL_OWNED) {
    process->vm.external->state = PROCESS_VM_EXTERNAL_OWNED_FINAL;
  }
  external_ready(process);
  spin_unlock(&process->vm.lock);
}

static void finish_mutation(struct process *process, bool expired)
{
  struct bsp_request *request = process->vm.pending;
  KASSERT(request && request->state == BSP_REQUEST_FORWARDED);
  struct forwarder_operation *operation_previous = task_forwarder_set(request->operation);
  bool denied = !expired && !task_operation_start();
  bool exhausted = false;
  if (!expired && !denied) {
    spin_lock(&process->vm.lock);
    KASSERT(process->vm.phase == PROCESS_VM_MUTATING && !process->vm.roots &&
        !process->vm.borrowers && residency_empty(process));
    exhausted = process->vm.generation == UINT64_MAX;
    if (!exhausted) {
      /* Even a later rollback may have changed PTEs. Never reuse its epoch.
       * No root can refill until reply; every later entry reloads CR3. */
      ++process->vm.generation;
    }
    spin_unlock(&process->vm.lock);
  }
  bool refused = expired || denied || exhausted;
  if (refused) {
    refuse_mutation(request, denied ? CALL_DENIED : exhausted ? CALL_NO_MEMORY : CALL_BUSY);
  } else {
    struct execution_group *previous = object_cleanup_enter(request->cleanup_group);
    if (request->service == BSP_SERVICE_MEMORY) {
      memory_request_execute((struct memory_request *)request);
    } else if (request->service == BSP_SERVICE_JIT) {
      jit_request_execute((struct jit_request *)request);
    } else if (request->service == BSP_SERVICE_THREAD) {
      thread_request_execute((struct thread_request *)request);
    } else {
      KASSERT(request->service == BSP_SERVICE_DISPLAY);
      display_vm_request_execute(request);
    }
    object_cleanup_leave(previous);
    request->vm_reply_owned = true;
    request->vm_process = NULL;
    request->vm_mutator = NULL;
  }
  task_forwarder_set(operation_previous);
  spin_lock(&process->vm.lock);
  process->vm.pending = NULL;
  process->vm.deadline = 0;
  struct process_vm_held *held = NULL;
  if (refused) {
    process->vm.phase = process->vm.external ? PROCESS_VM_DRAINING : PROCESS_VM_OPEN;
    task_vm_set_continuation(process->vm.mutator, true);
    process->vm.mutator = NULL;
    if (!process->vm.external) {
      held = process->vm.held;
      process->vm.held = NULL;
    }
    external_ready(process);
  } else {
    KASSERT(!process->vm.roots && !process->vm.borrowers);
    process->vm.phase = PROCESS_VM_REPLY;
  }
  spin_unlock(&process->vm.lock);
  resume_held(held);
  /* No endpoint publication or waiter owns this record: its caller executed
   * it synchronously under kernel CR3 and now consumes the staged result. */
  request->wait = NULL;
  request->cleanup_group = NULL;
  request->state = BSP_REQUEST_COMPLETE;
}

void process_vm_private_call(struct bsp_request *request)
{
  struct process *process = request->vm_process;
  struct task *mutator = request->vm_mutator;
  KASSERT((request->service == BSP_SERVICE_MEMORY || request->service == BSP_SERVICE_JIT ||
      request->service == BSP_SERVICE_THREAD || request->service == BSP_SERVICE_DISPLAY) &&
      request->state == BSP_REQUEST_PREPARED && mutator == task_current_user());
  task_vm_kernel_enter();
  spin_lock(&process->vm.lock);
  if (process->vm.phase != PROCESS_VM_OPEN) {
    spin_unlock(&process->vm.lock);
    refuse_mutation(request, CALL_BUSY);
    request->wait = NULL;
    request->cleanup_group = NULL;
    request->state = BSP_REQUEST_COMPLETE;
    task_vm_kernel_leave();
    return;
  }
  KASSERT(!process->vm.pending && !process->vm.mutator && !process->vm.local_wait);
  task_vm_set_continuation(mutator, false);
  process->vm.phase = PROCESS_VM_DRAINING;
  process->vm.mutator = mutator;
  process->vm.mutation_cpu = arch_cpu_index();
  process->vm.pending = request;
  request->state = BSP_REQUEST_FORWARDED;
  process->vm.deadline = 0;
  if (process->vm.roots || process->vm.borrowers) {
    uint64_t now = arch_monotonic_ns();
    process->vm.deadline = now > UINT64_MAX - VM_QUIESCENCE_NS ?
      UINT64_MAX : now + VM_QUIESCENCE_NS;
  }
  bool expired;
  for (;;) {
    bool drained = !process->vm.roots && !process->vm.borrowers;
    expired = !drained && arch_monotonic_ns() >= process->vm.deadline;
    if (drained || expired) {
      if (drained) {
        process->vm.phase = PROCESS_VM_MUTATING;
      }
      spin_unlock(&process->vm.lock);
      break;
    }
    struct task_wait *wait = task_wait_prepare();
    process->vm.local_wait = wait;
    uint64_t deadline = process->vm.deadline;
    spin_unlock(&process->vm.lock);
    task_wait_sleep_until(wait, deadline);
    spin_lock(&process->vm.lock);
    if (process->vm.local_wait == wait) {
      process->vm.local_wait = NULL;
    }
  }
  finish_mutation(process, expired);
  task_vm_kernel_leave();
}
