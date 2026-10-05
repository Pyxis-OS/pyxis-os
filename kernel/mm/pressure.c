#include <kernel/mm/pressure.h>
#include <kernel/spinlock.h>
#include <kernel/task.h>

/* Protects pending and observer. A notifier wakes the observer while holding
 * it, so the worker's detach cannot complete while another CPU still uses the
 * borrowed wait. Order: this lock, then the queue lock. */
static struct spinlock pressure_lock;
static bool pending;
static struct task_wait *observer;

void mm_pressure_notify(void)
{
  spin_lock(&pressure_lock);
  pending = true;
  struct task_wait *wait = observer;
  observer = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
  spin_unlock(&pressure_lock);
}

bool mm_pressure_take(void)
{
  spin_lock(&pressure_lock);
  bool result = pending;
  pending = false;
  spin_unlock(&pressure_lock);
  return result;
}

void mm_pressure_wait(struct task_wait *wait)
{
  spin_lock(&pressure_lock);
  observer = wait;
  /* A notification after the worker's last take found no observer. Waking the
   * prepared wait now makes the coming sleep return at once. */
  if (wait && pending) {
    observer = NULL;
    task_wait_wake(wait);
  }
  spin_unlock(&pressure_lock);
}
