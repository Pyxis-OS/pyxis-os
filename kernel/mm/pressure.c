#include <kernel/mm/pressure.h>
#include <kernel/task.h>

static bool pending;
static struct task_wait *observer;

void mm_pressure_notify(void)
{
  pending = true;
  struct task_wait *wait = observer;
  observer = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

bool mm_pressure_take(void)
{
  bool result = pending;
  pending = false;
  return result;
}

void mm_pressure_wait(struct task_wait *wait)
{
  observer = wait;
}
