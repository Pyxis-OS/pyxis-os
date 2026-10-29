#include <arch/clock.h>
#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/private.h>
#include <kernel/mm/shared.h>
#include <kernel/mm/shared_view.h>
#include <kernel/mm/vm.h>
#include <kernel/object/forwarder.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/process_vm.h>
#include <kernel/task.h>

#define SHARED_WITHDRAWAL_NS UINT64_C(1000000000)
#define SHARED_VIEW_STEP_PAGES 256

struct shared_view {
  struct shared_view *next;
  struct shared_process *owner;
  struct shared_region *region, *quarantine;
  struct forwarder_mapping lineage;
  uintptr_t address;
  uint64_t identity;
  bool retiring, uncertain;
};

struct shared_process {
  struct process *process;
  struct shared_view *views;
  struct process_vm_external external;
  struct shared_process *ready_next, *waiting_next;
  size_t cpu;
  uint64_t deadline;
  bool queued, waiting, exiting, uncertain, terminated, finished;
  bool processing, vm_ready; /* Queue guard joins early readiness and actor return. */
  bool attached; /* Fixed actor only; token fields belong to the VM guard. */
};

struct shared_view_queue {
  struct spinlock guard;
  _Atomic(struct shared_process *) first;
  struct shared_process *last, *waiting;
  atomic_uint_fast64_t deadline;
};

static struct shared_view_queue *queues;
static size_t queue_count;

/* Queue guard, IF=0. Aggregate storage is process-owned; a cleanup pin lasts
 * from first MAP until this actor finishes the last-task exit notification. */
static void enqueue_locked(struct shared_process *state)
{
  struct shared_view_queue *queue = &queues[state->cpu];
  KASSERT(!state->finished);
  if (state->queued) {
    return;
  }
  state->queued = true;
  state->ready_next = NULL;
  if (queue->last) {
    queue->last->ready_next = state;
  } else {
    atomic_store_explicit(&queue->first, state, memory_order_release);
  }
  queue->last = state;
}

void shared_process_vm_ready(struct process_vm_external *external)
{
  struct shared_process *state = (void *)((char *)external -
      offsetof(struct shared_process, external));
  struct shared_view_queue *queue = &queues[state->cpu];
  spin_lock(&queue->guard);
  if (state->processing) {
    state->vm_ready = true;
  } else {
    enqueue_locked(state);
  }
  spin_unlock(&queue->guard);
  process_ram_cleanup_notify(state->cpu);
}

void shared_view_request_withdrawal(struct shared_view *view, bool uncertain)
{
  struct shared_process *state = view->owner;
  struct shared_view_queue *queue = &queues[state->cpu];
  spin_lock(&queue->guard);
  view->retiring = true;
  view->uncertain |= uncertain;
  state->uncertain |= uncertain;
  enqueue_locked(state);
  spin_unlock(&queue->guard);
  process_ram_cleanup_notify(state->cpu);
}

bool shared_view_targets(struct shared_view *view, struct process *process)
{
  return view->owner->process == process;
}

bool shared_view_withdrawal_uncertain(struct shared_view *view)
{
  struct shared_process *state = view->owner;
  struct shared_view_queue *queue = &queues[state->cpu];
  spin_lock(&queue->guard);
  bool uncertain = view->uncertain || (view->retiring && state->terminated);
  spin_unlock(&queue->guard);
  return uncertain;
}

/* Actor-owned deadline list; readers only observe its published minimum. */
static void refresh_deadline(struct shared_view_queue *queue)
{
  uint64_t deadline = UINT64_MAX;
  for (struct shared_process *state = queue->waiting; state;
       state = state->waiting_next) {
    if (state->deadline < deadline) {
      deadline = state->deadline;
    }
  }
  atomic_store_explicit(&queue->deadline, deadline, memory_order_release);
}

static void detach_waiting(struct shared_process *state)
{
  if (!state->waiting) {
    return;
  }
  struct shared_view_queue *queue = &queues[state->cpu];
  struct shared_process **link = &queue->waiting;
  while (*link != state) {
    KASSERT(*link);
    link = &(*link)->waiting_next;
  }
  *link = state->waiting_next;
  state->waiting_next = NULL;
  state->waiting = false;
  refresh_deadline(queue);
}

static void clear_queued(struct shared_process *state)
{
  struct shared_view_queue *queue = &queues[state->cpu];
  spin_lock(&queue->guard);
  struct shared_process *previous = NULL;
  struct shared_process *item = atomic_load_explicit(&queue->first, memory_order_relaxed);
  while (item && item != state) {
    previous = item;
    item = item->ready_next;
  }
  if (item) {
    if (previous) {
      previous->ready_next = item->ready_next;
    } else {
      atomic_store_explicit(&queue->first, item->ready_next, memory_order_release);
    }
    if (queue->last == item) {
      queue->last = previous;
    }
    state->ready_next = NULL;
    state->queued = false;
  }
  spin_unlock(&queue->guard);
}

static struct shared_process *prepare_process(struct process *process)
{
  if (process->shared_views) {
    return process->shared_views;
  }
  struct shared_process *state = kmalloc(sizeof(*state));
  if (!state) {
    return NULL;
  }
  *state = (struct shared_process){.process = process, .cpu = arch_cpu_index(),
    .deadline = UINT64_MAX};
  if (!process_cleanup_pin(process)) {
    kfree(state);
    return NULL;
  }
  process->shared_views = state;
  process->shared_view_metadata_bytes = sizeof(*state);
  return state;
}

static void unmap_view(struct shared_view *view, size_t pages, bool external)
{
  struct process *process = view->owner->process;
  for (size_t i = 0; i < pages; ++i) {
    phys_addr_t frame;
    KASSERT(vm_unmap(process->address_space, view->address + i * PAGE_SIZE,
        &frame) == MM_OK);
    KASSERT(frame == shared_region_frame(view->region, i));
    if ((i + 1) % SHARED_VIEW_STEP_PAGES == 0 && i + 1 < pages) {
      task_yield_masked();
      if (external) {
        process_vm_require_external(&view->owner->external);
      } else {
        process_vm_require_mutation(process);
      }
    }
  }
  KASSERT(vm_release(process->address_space, view->address,
      shared_region_size(view->region)) == MM_OK);
}

static void dispose_view(struct shared_view *view)
{
  struct process *process = view->owner->process;
  size_t bytes = shared_region_size(view->region);
  KASSERT(process->shared_view_count && process->shared_view_bytes >= bytes &&
      process->shared_view_metadata_bytes >= sizeof(*view));
  --process->shared_view_count;
  process->shared_view_bytes -= bytes;
  process->shared_view_metadata_bytes -= sizeof(*view);
  /* Removing registration joins callbacks before its embedded storage dies. */
  bool uncertain = forwarder_mapping_complete(&view->lineage, false);
  if (uncertain && view->quarantine) {
    shared_region_quarantine_owned(&view->quarantine);
  }
  if (view->quarantine) {
    object_release(shared_region_object(view->quarantine));
  }
  object_release(shared_region_object(view->region));
  kfree(view);
}

enum call_status shared_view_map(struct process *process,
    struct shared_region *region, bool writable, struct memory_reservation *result)
{
  process_vm_require_mutation(process);
  *result = (struct memory_reservation){0};
  size_t bytes = shared_region_size(region);
  uint64_t identity = private_memory_identity();
  if (!identity || process->shared_view_count == SIZE_MAX ||
      bytes > SIZE_MAX - process->shared_view_bytes ||
      sizeof(struct shared_view) > SIZE_MAX - process->shared_view_metadata_bytes) {
    return CALL_LIMIT;
  }
  struct shared_process *state = prepare_process(process);
  if (!state) {
    return CALL_NO_MEMORY;
  }
  struct shared_view *view = kmalloc(sizeof(*view));
  if (!view) {
    return CALL_NO_MEMORY;
  }
  *view = (struct shared_view){.owner = state, .region = region, .identity = identity};
  if (!object_retain(shared_region_object(region))) {
    kfree(view);
    return CALL_LIMIT;
  }
  if (!object_retain(shared_region_object(region))) {
    object_release(shared_region_object(region));
    kfree(view);
    return CALL_LIMIT;
  }
  view->quarantine = region;
  enum call_status status = forwarder_mapping_prepare(&view->lineage,
      task_forwarder_operation(), view);
  if (status != CALL_OK) {
    goto release;
  }
  enum mm_result mm = vm_reserve(process->address_space, bytes, PAGE_SIZE, &view->address);
  if (mm != MM_OK) {
    status = CALL_NO_MEMORY;
    goto prepared;
  }
  size_t pages = shared_region_page_count(region), mapped = 0;
  unsigned permissions = PAGE_USER | (writable ? PAGE_WRITE : 0);
  while (mapped < pages) {
    mm = vm_map(process->address_space, view->address + mapped * PAGE_SIZE,
        shared_region_frame(region, mapped), permissions);
    if (mm != MM_OK) {
      break;
    }
    ++mapped;
    if (mapped % SHARED_VIEW_STEP_PAGES == 0 && mapped < pages) {
      task_yield_masked();
      process_vm_require_mutation(process);
    }
  }
  status = mm == MM_OK ? forwarder_mapping_enroll(&view->lineage) : CALL_NO_MEMORY;
  if (status != CALL_OK) {
    unmap_view(view, mapped, false);
    goto prepared;
  }
  view->next = state->views;
  state->views = view;
  ++process->shared_view_count;
  process->shared_view_bytes += bytes;
  process->shared_view_metadata_bytes += sizeof(*view);
  *result = (struct memory_reservation){identity, view->address, bytes};
  return CALL_OK;

prepared:
  forwarder_mapping_complete(&view->lineage, false);
release:
  object_release(shared_region_object(view->quarantine));
  object_release(shared_region_object(region));
  kfree(view);
  return status;
}

bool shared_view_release(struct process *process, uint64_t identity)
{
  process_vm_require_mutation(process);
  struct shared_process *state = process->shared_views;
  if (!state) {
    return false;
  }
  struct shared_view **link = &state->views;
  while (*link && (*link)->identity != identity) {
    link = &(*link)->next;
  }
  struct shared_view *view = *link;
  if (!view) {
    return false;
  }
  unmap_view(view, shared_region_page_count(view->region), false);
  *link = view->next;
  dispose_view(view);
  return true;
}

void shared_process_exit(struct process *process)
{
  struct shared_process *state = process->shared_views;
  if (!state) {
    return;
  }
  struct shared_view_queue *queue = &queues[state->cpu];
  spin_lock(&queue->guard);
  state->exiting = true;
  enqueue_locked(state);
  spin_unlock(&queue->guard);
  process_ram_cleanup_notify(state->cpu);
}

void shared_process_discard(struct process *process)
{
  struct shared_process *state = process->shared_views;
  if (!state) {
    return;
  }
  KASSERT(state->finished && !state->views && !state->queued && !state->waiting &&
      state->external.state == PROCESS_VM_EXTERNAL_IDLE &&
      !process->shared_view_bytes && !process->shared_view_count &&
      process->shared_view_metadata_bytes == sizeof(*state));
  kfree(state);
  process->shared_views = NULL;
  process->shared_view_metadata_bytes = 0;
}

static void expire_withdrawal(struct shared_process *state)
{
  struct shared_view_queue *queue = &queues[state->cpu];
  spin_lock(&queue->guard);
  state->terminated = true;
  spin_unlock(&queue->guard);
  state->deadline = UINT64_MAX;
  /* The target may still own a private mutation. Do not inspect its view list
   * or consume refs before ownership transfers; registrations retain backing. */
  process_request_stop(state->process);
}

static void end_processing(struct shared_process *state, bool waiting)
{
  struct shared_view_queue *queue = &queues[state->cpu];
  spin_lock(&queue->guard);
  KASSERT(state->processing);
  state->processing = false;
  if (waiting && state->vm_ready) {
    enqueue_locked(state);
  }
  state->vm_ready = false;
  spin_unlock(&queue->guard);
}

static bool run_process(struct shared_process *state)
{
  struct shared_view_queue *queue = &queues[state->cpu];
  struct process *process = state->process;
  spin_lock(&queue->guard);
  bool escalate = state->uncertain && !state->terminated;
  spin_unlock(&queue->guard);
  if (escalate) {
    expire_withdrawal(state);
  }
  if (!state->attached) {
    uint64_t now = arch_monotonic_ns();
    state->deadline = state->terminated || now > UINT64_MAX - SHARED_WITHDRAWAL_NS ?
        UINT64_MAX : now + SHARED_WITHDRAWAL_NS;
    state->waiting_next = queue->waiting;
    queue->waiting = state;
    state->waiting = true;
    state->attached = true;
    process_vm_external_request(process, &state->external, state->cpu);
  }
  enum process_vm_external_progress progress = process_vm_external_acquire(&state->external);
  if (progress == PROCESS_VM_EXTERNAL_WAIT || progress == PROCESS_VM_EXTERNAL_EXHAUSTED) {
    if (!state->terminated && (progress == PROCESS_VM_EXTERNAL_EXHAUSTED ||
        arch_monotonic_ns() >= state->deadline)) {
      expire_withdrawal(state);
    }
    refresh_deadline(queue);
    end_processing(state, true);
    return false;
  }
  process_vm_require_external(&state->external);
  struct shared_view **link = &state->views;
  while (*link) {
    struct shared_view *view = *link;
    spin_lock(&queue->guard);
    bool retiring = state->exiting || view->retiring;
    bool uncertain = view->uncertain;
    spin_unlock(&queue->guard);
    if (!retiring) {
      link = &view->next;
      continue;
    }
    if (uncertain && view->quarantine) {
      shared_region_quarantine_owned(&view->quarantine);
    }
    unmap_view(view, shared_region_page_count(view->region), true);
    *link = view->next;
    dispose_view(view);
  }
  spin_lock(&queue->guard);
  bool terminated = state->terminated;
  spin_unlock(&queue->guard);
  bool final = process_vm_external_finish(&state->external, !terminated);
  state->attached = false;
  detach_waiting(state);
  end_processing(state, false);
  spin_lock(&queue->guard);
  final = final && state->exiting;
  spin_unlock(&queue->guard);
  if (final && state->views) {
    /* Final departure can happen while a bounded unmap yields, after an early
     * live view was skipped. Keep ownership pinned for another final pass. */
    spin_lock(&queue->guard);
    enqueue_locked(state);
    spin_unlock(&queue->guard);
    return false;
  }
  if (final) {
    clear_queued(state);
    spin_lock(&queue->guard);
    KASSERT(!state->views);
    state->finished = true;
    spin_unlock(&queue->guard);
    return true;
  }
  return false;
}

void shared_views_init(void)
{
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() == 0 && !queues);
  queue_count = arch_cpu_count();
  queues = kmalloc(queue_count * sizeof(*queues));
  if (!queues) {
    panic("cannot allocate shared-view work queues");
  }
  for (size_t i = 0; i < queue_count; ++i) {
    queues[i] = (struct shared_view_queue){0};
    atomic_init(&queues[i].guard.locked, false);
    atomic_init(&queues[i].first, NULL);
    atomic_init(&queues[i].deadline, UINT64_MAX);
  }
}

uint64_t shared_views_deadline(void)
{
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() < queue_count);
  return atomic_load_explicit(&queues[arch_cpu_index()].deadline, memory_order_acquire);
}

bool shared_views_pending(void)
{
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() < queue_count);
  struct shared_view_queue *queue = &queues[arch_cpu_index()];
  if (atomic_load_explicit(&queue->first, memory_order_acquire)) {
    return true;
  }
  uint64_t deadline = shared_views_deadline();
  return deadline != UINT64_MAX && arch_monotonic_ns() >= deadline;
}

void shared_views_reap(void)
{
  KASSERT(cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  uint64_t flags = cpu_save_interrupts();
  struct shared_view_queue *queue = &queues[arch_cpu_index()];
  uint64_t now = 0;
  if (shared_views_deadline() != UINT64_MAX) {
    now = arch_monotonic_ns();
  }
  for (struct shared_process *state = queue->waiting; state; state = state->waiting_next) {
    if (state->deadline != UINT64_MAX && now >= state->deadline) {
      spin_lock(&queue->guard);
      enqueue_locked(state);
      spin_unlock(&queue->guard);
    }
  }
  spin_lock(&queue->guard);
  struct shared_process *state = atomic_load_explicit(&queue->first, memory_order_relaxed);
  if (state) {
    atomic_store_explicit(&queue->first, state->ready_next, memory_order_release);
    if (!state->ready_next) {
      queue->last = NULL;
    }
    state->ready_next = NULL;
    state->queued = false;
    KASSERT(!state->processing);
    state->processing = true;
    state->vm_ready = false;
  }
  spin_unlock(&queue->guard);
  if (state) {
    struct process *process = state->process;
    struct execution_group *previous = object_cleanup_enter(process->execution_group);
    bool finished = run_process(state);
    object_cleanup_leave(previous);
    if (finished) {
      /* Unpin may transfer the process/group and destroy STATE on another CPU. */
      process_cleanup_unpin(process);
    }
  }
  cpu_restore_interrupts(flags);
}
