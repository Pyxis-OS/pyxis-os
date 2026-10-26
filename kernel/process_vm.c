#include <kernel/object/forwarder.h>
#include <arch/clock.h>
#include <arch/smp.h>
#include <kernel/object/display.h>
#include <kernel/object/memory.h>
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

static struct process *pending_processes; /* BSP, IF=0. */
static struct spinlock worker_lock;
static struct task_wait *worker_wait; /* worker_lock; queues nest inside it. */

static void wake_worker(void)
{
  spin_lock(&worker_lock);
  if (worker_wait) {
    struct task_wait *wait = worker_wait;
    worker_wait = NULL;
    /* Keep timeout detachment from racing a notifier holding this pointer. */
    task_wait_wake(wait);
  }
  spin_unlock(&worker_lock);
}

static bool access_allowed(struct process *process, struct task *task)
{
  return process->vm.phase == PROCESS_VM_OPEN ||
    (process->vm.phase == PROCESS_VM_DRAINING && task_vm_continuation(task)) ||
    (process->vm.phase == PROCESS_VM_REPLY && process->vm.mutator == task);
}

bool process_vm_dispatch(struct process *process, struct task *task, uint64_t *generation)
{
  spin_lock(&process->vm.lock);
  bool admitted = access_allowed(process, task);
  if (admitted) {
    KASSERT(process->vm.roots != SIZE_MAX);
    size_t cpu = arch_cpu_index();
    KASSERT(process->vm.incarnation && process->vm.generation && !resident(process, cpu));
    process->vm.resident_cpus[cpu / 64] |= UINT64_C(1) << (cpu % 64);
    ++process->vm.roots;
    *generation = process->vm.generation;
  } else {
    struct process_vm_held *held = task_vm_hold(task);
    held->next = process->vm.held;
    process->vm.held = held;
  }
  spin_unlock(&process->vm.lock);
  return admitted;
}

void process_vm_root_depart(struct process *process, uint64_t generation)
{
  spin_lock(&process->vm.lock);
  size_t cpu = arch_cpu_index();
  KASSERT(process->vm.roots && resident(process, cpu) &&
      process->vm.generation == generation);
  process->vm.resident_cpus[cpu / 64] &= ~(UINT64_C(1) << (cpu % 64));
  --process->vm.roots;
  bool pending = process->vm.pending &&
    process->vm.pending->service != BSP_SERVICE_MEMORY;
  if (process->vm.local_wait) {
    struct task_wait *wait = process->vm.local_wait;
    process->vm.local_wait = NULL;
    /* VM guard joins this access with timeout detachment before record reuse. */
    task_wait_wake(wait);
  }
  spin_unlock(&process->vm.lock);
  if (pending) {
    wake_worker();
  }
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

static void finish_mutation(struct process *process, bool expired, bool local);

static void refuse_mutation(struct bsp_request *request, enum call_status status)
{
  if (request->service == BSP_SERVICE_MEMORY) {
    struct memory_request *memory = (void *)request;
    memory->status = status;
    memory->process = NULL;
  } else if (request->service == BSP_SERVICE_THREAD) {
    struct thread_request *thread = (void *)request;
    thread->status = status;
    thread->process = NULL;
    thread->caller = NULL;
  } else {
    KASSERT(request->service == BSP_SERVICE_DISPLAY);
    struct display_request *display = (void *)request;
    display->result = status;
    display->loan = NULL;
    display->display = NULL;
  }
  request->vm_process = NULL;
  request->vm_mutator = NULL;
}

void process_vm_mutation_forward(struct bsp_request *request)
{
  KASSERT(arch_cpu_index() == 0 && request->state == BSP_REQUEST_SERVICING);
  KASSERT(request->service == BSP_SERVICE_DISPLAY || request->service == BSP_SERVICE_THREAD);
  struct process *process = request->vm_process;
  struct task *mutator = request->vm_mutator;
  KASSERT(process && mutator);
  spin_lock(&process->vm.lock);
  KASSERT(!task_vm_borrow_count(mutator));
  if (process->vm.phase != PROCESS_VM_OPEN) {
    spin_unlock(&process->vm.lock);
    refuse_mutation(request, CALL_BUSY);
    bsp_request_complete(request);
    return;
  }
  KASSERT(!process->vm.pending && !process->vm.mutator && !process->vm.next_pending);
  /* The scheduler has already dropped this caller's root. Surrender its
   * continuation so the transaction cannot wait for its own final reply. */
  task_vm_set_continuation(mutator, false);
  bool drained = !process->vm.roots && !process->vm.borrowers;
  process->vm.phase = drained ? PROCESS_VM_MUTATING : PROCESS_VM_DRAINING;
  process->vm.mutator = mutator;
  process->vm.mutation_cpu = 0;
  process->vm.pending = request;
  if (!drained) {
    uint64_t now = arch_monotonic_ns();
    process->vm.deadline = now > UINT64_MAX - VM_QUIESCENCE_NS ?
      UINT64_MAX : now + VM_QUIESCENCE_NS;
  }
  spin_unlock(&process->vm.lock);
  request->state = BSP_REQUEST_FORWARDED;
  if (drained) {
    /* BSP DISPLAY/thread clients keep the same gate. Only drainage needs
     * the VM worker; private MEMORY never enters this pending list. */
    finish_mutation(process, false, false);
    return;
  }
  struct process **tail = &pending_processes;
  while (*tail) {
    tail = &(*tail)->vm.next_pending;
  }
  *tail = process;
  /* Current one-task processes already have no other root. Future siblings
   * observe closure at their syscall entry/return or timer preemption. */
  wake_worker();
}

void process_vm_require_mutation(struct process *process)
{
  spin_lock(&process->vm.lock);
  KASSERT(arch_cpu_index() == process->vm.mutation_cpu);
  struct task *actor = task_current_user();
  KASSERT(actor ? task_vm_kernel_active() && actor == process->vm.mutator :
      arch_cpu_index() == 0);
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
  process->vm.phase = PROCESS_VM_OPEN;
  task_vm_set_continuation(task, true);
  struct process_vm_held *held = process->vm.held;
  process->vm.held = NULL;
  spin_unlock(&process->vm.lock);
  resume_held(held);
}

void process_vm_retire(struct process *process)
{
  KASSERT(arch_cpu_index() == 0);
  spin_lock(&process->vm.lock);
  KASSERT((process->vm.phase == PROCESS_VM_OPEN ||
      process->vm.phase == PROCESS_VM_DRAINING) && !process->vm.roots &&
      !process->vm.borrowers && !process->vm.pending && !process->vm.mutator &&
      !process->vm.held && residency_empty(process));
  process->vm.phase = PROCESS_VM_DRAINING;
  spin_unlock(&process->vm.lock);
}

static struct process *select_pending(uint64_t now, uint64_t *deadline, bool *expired)
{
  struct process **link = &pending_processes;
  while (*link) {
    struct process *process = *link;
    spin_lock(&process->vm.lock);
    /* BSP clients may arrive already drained and MUTATING. */
    bool mutating = process->vm.phase == PROCESS_VM_MUTATING;
    bool drained = mutating || (!process->vm.roots && !process->vm.borrowers);
    *expired = !drained && now >= process->vm.deadline;
    if (drained || *expired) {
      KASSERT(mutating || process->vm.phase == PROCESS_VM_DRAINING);
      if (drained) {
        process->vm.phase = PROCESS_VM_MUTATING;
      }
      spin_unlock(&process->vm.lock);
      *link = process->vm.next_pending;
      process->vm.next_pending = NULL;
      return process;
    }
    if (process->vm.deadline < *deadline) {
      *deadline = process->vm.deadline;
    }
    spin_unlock(&process->vm.lock);
    link = &process->vm.next_pending;
  }
  return NULL;
}

static void finish_mutation(struct process *process, bool expired, bool local)
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
    } else if (request->service == BSP_SERVICE_THREAD) {
      thread_request_execute((struct thread_request *)request);
    } else {
      KASSERT(request->service == BSP_SERVICE_DISPLAY);
      display_request_execute((struct display_request *)request);
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
    process->vm.phase = PROCESS_VM_OPEN;
    task_vm_set_continuation(process->vm.mutator, true);
    process->vm.mutator = NULL;
    held = process->vm.held;
    process->vm.held = NULL;
  } else {
    KASSERT(!process->vm.roots && !process->vm.borrowers);
    process->vm.phase = PROCESS_VM_REPLY;
  }
  spin_unlock(&process->vm.lock);
  resume_held(held);
  if (local) {
    /* No endpoint publication or waiter owns this record: its caller executed
     * it synchronously under kernel CR3 and now consumes the staged result. */
    request->wait = NULL;
    request->cleanup_group = NULL;
    request->state = BSP_REQUEST_COMPLETE;
  } else {
    bsp_request_complete(request);
  }
}

void process_vm_memory_call(struct memory_request *memory)
{
  struct bsp_request *request = &memory->request;
  struct process *process = request->vm_process;
  struct task *mutator = request->vm_mutator;
  KASSERT(request->service == BSP_SERVICE_MEMORY &&
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
  finish_mutation(process, expired, true);
  task_vm_kernel_leave();
}

static void vm_worker(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    struct task_wait *wait = task_wait_prepare();
    spin_lock(&worker_lock);
    KASSERT(!worker_wait);
    worker_wait = wait;
    spin_unlock(&worker_lock);

    uint64_t now = pending_processes ? arch_monotonic_ns() : 0;
    uint64_t deadline = UINT64_MAX;
    bool expired = false;
    struct process *process = select_pending(now, &deadline, &expired);
    if (process) {
      /* Finish the published wait even when work is immediately available. */
      deadline = now;
    }
    if (deadline == UINT64_MAX) {
      task_wait_sleep(wait);
    } else {
      task_wait_sleep_until(wait, deadline);
    }
    spin_lock(&worker_lock);
    if (worker_wait == wait) {
      worker_wait = NULL;
    }
    spin_unlock(&worker_lock);
    if (process) {
      finish_mutation(process, expired, false);
    }
    cpu_restore_interrupts(flags);
    kernel_task_yield_if_runnable();
  }
}

void process_vm_init(void)
{
  KASSERT(arch_cpu_index() == 0);
  atomic_init(&worker_lock.locked, false);
  KASSERT(kernel_task_create(vm_worker, NULL) == MM_OK);
}
