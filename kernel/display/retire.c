#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/display_retire.h>
#include <kernel/log.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/mm/vm.h>
#include <kernel/object/display.h>
#include <kernel/object/execution_group.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <kernel/task.h>
#include <stdint.h>

#define DISPLAY_RECLAIM_PAGES 64

struct display_ram_queue {
  struct spinlock guard;
  struct display_frame *first, *last;
  struct display_frame *quarantined;
};

static struct display_ram_queue *queues;
static size_t queue_count;

void display_retirement_init(struct display_retirement *join)
{
  KASSERT(!cpu_interrupts_enabled() && join);
  *join = (struct display_retirement){0};
  atomic_init(&join->guard.locked, false);
}

void display_retirement_wait(struct display_retirement *join)
{
  KASSERT(!cpu_interrupts_enabled() && join && task_current_actor());
  KASSERT(arch_space_active(arch_kernel_space()));
  KASSERT(!vm_kernel_owner_held());
  spin_lock(&join->guard);
  KASSERT(!join->wait && !join->process);
  while (join->pending) {
    struct task_wait *wait = task_wait_prepare();
    join->wait = wait;
    spin_unlock(&join->guard);
    task_wait_sleep(wait);
    spin_lock(&join->guard);
    KASSERT(!join->wait);
  }
  spin_unlock(&join->guard);
}

bool display_retirement_join_process(struct display_retirement *join, struct process *process)
{
  KASSERT(!cpu_interrupts_enabled() && join && process);
  spin_lock(&join->guard);
  KASSERT(!join->wait && !join->process);
  if (!join->pending) {
    spin_unlock(&join->guard);
    return true;
  }
  join->process = process;
  spin_unlock(&join->guard);
  return false;
}

static void complete_retirement(struct display_retirement *join)
{
  if (!join) {
    return;
  }
  spin_lock(&join->guard);
  KASSERT(join->pending);
  --join->pending;
  struct task_wait *wait = NULL;
  struct process *process = NULL;
  if (!join->pending) {
    wait = join->wait;
    process = join->process;
    KASSERT(!wait || !process);
    join->wait = NULL;
    join->process = NULL;
  }
  spin_unlock(&join->guard);
  if (wait) {
    /* The resumed caller can immediately release the join's containing storage. */
    task_wait_wake(wait);
  } else if (process) {
    /* Notification transfers the process and its embedded join to its RAM actor. */
    process_display_cleanup_ready(process);
  }
}

void display_ram_init(void)
{
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() == 0 && !queues);
  size_t count = arch_cpu_count();
  KASSERT(count && count <= SIZE_MAX / sizeof(*queues));
  queues = kmalloc(count * sizeof(*queues));
  if (!queues) {
    panic("cannot allocate display RAM retirement queues");
  }
  for (size_t cpu = 0; cpu < count; ++cpu) {
    queues[cpu] = (struct display_ram_queue){0};
    atomic_init(&queues[cpu].guard.locked, false);
  }
  queue_count = count;
}

void display_frame_retire(struct display_frame *frame, struct display_retirement *join)
{
  KASSERT(!cpu_interrupts_enabled() && arch_cpu_index() == 0 && frame);
  KASSERT(queues && frame->preparation_cpu < queue_count && !frame->references);
  KASSERT(frame->fb.address && frame->fb.size && !frame->retire_next && !frame->retirement);
  if (!frame->cleanup_group) {
    frame->cleanup_group = object_cleanup_defer();
  }
  if (join) {
    spin_lock(&join->guard);
    KASSERT(!join->wait && !join->process && join->pending != SIZE_MAX);
    ++join->pending;
    spin_unlock(&join->guard);
  }
  frame->retirement = join;
  size_t cpu = frame->preparation_cpu;
  struct display_ram_queue *queue = &queues[cpu];
  spin_lock(&queue->guard);
  if (queue->last) {
    queue->last->retire_next = frame;
  } else {
    queue->first = frame;
  }
  queue->last = frame;
  spin_unlock(&queue->guard);
  process_ram_cleanup_notify(cpu);
}

bool display_ram_pending(void)
{
  KASSERT(!cpu_interrupts_enabled());
  size_t cpu = arch_cpu_index();
  KASSERT(queues && cpu < queue_count);
  struct display_ram_queue *queue = &queues[cpu];
  spin_lock(&queue->guard);
  bool pending = queue->first != NULL;
  spin_unlock(&queue->guard);
  return pending;
}

static void validate_batch(struct display_frame *first, size_t cpu)
{
  for (struct display_frame *frame = first; frame; frame = frame->retire_next) {
    uint64_t flags = cpu_save_interrupts();
    KASSERT(!frame->references && frame->preparation_cpu == cpu);
    struct vm_reclaim reclaim;
    KASSERT(vm_free_begin(vm_kernel_space(), frame->fb.address, frame->fb.size,
        &reclaim) == MM_OK);
    cpu_restore_interrupts(flags);
    if (frame->retire_next) {
      kernel_task_yield_if_runnable();
    }
  }
}

static void quarantine_batch(struct display_ram_queue *queue, struct display_frame *first)
{
  struct display_frame *last = NULL;
  for (struct display_frame *frame = first; frame; frame = frame->retire_next) {
    vm_kernel_quarantine(frame->fb.address, frame->fb.size);
    last = frame;
  }
  KASSERT(last);
  spin_lock(&queue->guard);
  last->retire_next = queue->quarantined;
  queue->quarantined = first;
  spin_unlock(&queue->guard);
}

void display_ram_reap(void)
{
  KASSERT(cpu_interrupts_enabled() && arch_space_active(arch_kernel_space()));
  size_t cpu = arch_cpu_index();
  KASSERT(queues && cpu < queue_count);
  struct display_ram_queue *queue = &queues[cpu];
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&queue->guard);
  struct display_frame *frame = queue->first;
  queue->first = queue->last = NULL;
  spin_unlock(&queue->guard);
  cpu_restore_interrupts(flags);
  if (!frame) {
    return;
  }

  struct vm_kernel_owner owner = {0};
  vm_kernel_owner_cleanup(&owner);
  validate_batch(frame, cpu);
  if (!vm_kernel_flush_remote()) {
    flags = cpu_save_interrupts();
    quarantine_batch(queue, frame);
    vm_kernel_owner_release(&owner);
    cpu_restore_interrupts(flags);
    kerror("display: CPU %zu backing retirement unconfirmed; batch retained until reboot\n", cpu);
    return;
  }

  while (frame) {
    flags = cpu_save_interrupts();
    struct display_frame *next = frame->retire_next;
    struct display_retirement *join = frame->retirement;
    struct execution_group *group = frame->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(group);
    struct vm_reclaim reclaim;
    KASSERT(vm_free_begin(vm_kernel_space(), frame->fb.address, frame->fb.size,
        &reclaim) == MM_OK);
    while (!vm_free_step(&reclaim, DISPLAY_RECLAIM_PAGES)) {
      cpu_restore_interrupts(flags);
      kernel_task_yield_if_runnable();
      flags = cpu_save_interrupts();
    }
    kfree(frame);
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    complete_retirement(join);
    cpu_restore_interrupts(flags);
    frame = next;
    if (frame) {
      kernel_task_yield_if_runnable();
    }
  }
  flags = cpu_save_interrupts();
  vm_kernel_owner_release(&owner);
  cpu_restore_interrupts(flags);
}
