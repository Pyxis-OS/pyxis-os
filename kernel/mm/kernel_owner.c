#include <arch/clock.h>
#include <arch/cpu_local.h>
#include <arch/paging.h>
#include <arch/smp.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>
#include <stdatomic.h>

#define OWNER_ADMISSION_NS UINT64_C(1000000000)

/* Leaf guard: admission/notification only, never allocation or a context switch.
 * Queue storage belongs to the retained waiting actor; detach before waking. */
static struct spinlock owner_guard;
static struct vm_kernel_owner *current_owner, *first_waiter, *last_waiter;
static atomic_bool runtime;

bool vm_kernel_runtime(void)
{
  return atomic_load_explicit(&runtime, memory_order_acquire);
}

void vm_kernel_runtime_start(void)
{
  KASSERT(arch_cpu_index() == 0 && !cpu_interrupts_enabled());
  KASSERT(!current_owner && !first_waiter && !vm_kernel_runtime());
  atomic_store_explicit(&runtime, true, memory_order_release);
}

static bool same_actor(const struct vm_kernel_owner *owner)
{
  return owner->cpu == arch_cpu_index() && owner->actor == task_current_actor();
}

static bool try_locked(struct vm_kernel_owner *owner)
{
  if (current_owner) {
    if (!same_actor(current_owner)) {
      return false;
    }
    KASSERT(current_owner->depth != SIZE_MAX);
    ++current_owner->depth;
    owner->root = current_owner;
  } else {
    owner->actor = task_current_actor();
    owner->cpu = arch_cpu_index();
    owner->depth = 1;
    owner->root = owner;
    current_owner = owner;
  }
  return true;
}

bool vm_kernel_owner_held(void)
{
  uint64_t flags = cpu_save_interrupts();
  if (!vm_kernel_runtime()) {
    bool held = arch_cpu_index() == 0;
    cpu_restore_interrupts(flags);
    return held;
  }
  spin_lock(&owner_guard);
  bool held = current_owner && same_actor(current_owner);
  spin_unlock(&owner_guard);
  cpu_restore_interrupts(flags);
  return held;
}

bool vm_kernel_owner_try(struct vm_kernel_owner *owner)
{
  KASSERT(!cpu_interrupts_enabled() && owner && !owner->root && !owner->queued);
  if (!vm_kernel_runtime()) {
    KASSERT(arch_cpu_index() == 0);
    owner->bootstrap = true;
    owner->root = owner;
    return true;
  }
  KASSERT(cpu_current()->active_space == arch_kernel_space());
  spin_lock(&owner_guard);
  bool admitted = try_locked(owner);
  spin_unlock(&owner_guard);
  return admitted;
}

static void wake_one_locked(void)
{
  struct vm_kernel_owner *owner = first_waiter;
  if (!owner) {
    return;
  }
  first_waiter = owner->next;
  if (!first_waiter) {
    last_waiter = NULL;
  }
  struct task_wait *wait = owner->wait;
  owner->queued = false;
  owner->next = NULL;
  owner->wait = NULL;
  task_wait_wake(wait);
  /* The remote actor may already have returned: don't touch owner again. */
}

void vm_kernel_owner_release(struct vm_kernel_owner *owner)
{
  KASSERT(!cpu_interrupts_enabled() && owner && owner->root && !owner->queued);
  if (owner->bootstrap) {
    *owner = (struct vm_kernel_owner){0};
    return;
  }
  spin_lock(&owner_guard);
  KASSERT(current_owner == owner->root && same_actor(current_owner));
  if (owner == current_owner) {
    KASSERT(owner->depth == 1);
    current_owner = NULL;
    wake_one_locked();
  } else {
    KASSERT(current_owner->depth > 1);
    --current_owner->depth;
  }
  *owner = (struct vm_kernel_owner){0};
  spin_unlock(&owner_guard);
}

static void detach_locked(struct vm_kernel_owner *owner)
{
  if (!owner->queued) {
    return;
  }
  struct vm_kernel_owner **link = &first_waiter, *previous = NULL;
  while (*link != owner) {
    KASSERT(*link);
    previous = *link;
    link = &(*link)->next;
  }
  *link = owner->next;
  if (last_waiter == owner) {
    last_waiter = previous;
  }
  owner->next = NULL;
  owner->wait = NULL;
  owner->queued = false;
}

bool vm_kernel_owner_acquire(struct vm_kernel_owner *owner)
{
  KASSERT(cpu_interrupts_enabled() && owner && !owner->root && !owner->queued);
  KASSERT(vm_kernel_runtime() && task_current_actor());
  KASSERT(cpu_current()->active_space == arch_kernel_space());
  uint64_t now = arch_monotonic_ns();
  uint64_t deadline = UINT64_MAX - now < OWNER_ADMISSION_NS ? UINT64_MAX :
      now + OWNER_ADMISSION_NS;
  uint64_t flags = cpu_save_interrupts();
  spin_lock(&owner_guard);
  for (;;) {
    if (arch_monotonic_ns() >= deadline) {
      /* A previous notification can have left an available owner. Pass it on
       * if this actor's budget expired instead of stranding another waiter. */
      if (!current_owner) {
        wake_one_locked();
      }
      spin_unlock(&owner_guard);
      cpu_restore_interrupts(flags);
      return false;
    }
    if (try_locked(owner)) {
      spin_unlock(&owner_guard);
      cpu_restore_interrupts(flags);
      return true;
    }
    struct task_wait *wait = task_wait_prepare();
    owner->wait = wait;
    owner->queued = true;
    if (last_waiter) {
      last_waiter->next = owner;
    } else {
      first_waiter = owner;
    }
    last_waiter = owner;
    spin_unlock(&owner_guard);
    task_wait_sleep_until(wait, deadline);
    spin_lock(&owner_guard);
    detach_locked(owner);
  }
}

void vm_kernel_owner_cleanup(struct vm_kernel_owner *owner)
{
  /* Non-fallible retirement keeps the whole caller-owned cleanup item alive.
   * Each admission attempt still parks boundedly; no spin or false completion. */
  while (!vm_kernel_owner_acquire(owner)) {
  }
}
