#include <arch/cpu.h>
#include <arch/cpu_local.h>
#include <arch/paging.h>
#include <kernel/display/iegd_owner.h>
#include <kernel/mm/kernel_owner.h>
#include <kernel/panic.h>
#include <kernel/task.h>

/* One fixed owner, including its top half: local interrupt exclusion protects
 * these fields. Native spinlocks cannot be taken from interrupt entry. */
static void (*irq_handler)(void *context);
static void *irq_context;
static bool irq_running;

static void require_bsp_masked(void)
{
  KASSERT(!cpu_interrupts_enabled() && cpu_current() == cpu_bsp());
}

static void require_join_context(void)
{
  KASSERT(cpu_interrupts_enabled() && cpu_current() == cpu_bsp());
  KASSERT(task_current_actor() && arch_space_active(arch_kernel_space()));
  KASSERT(!vm_kernel_owner_held());
}

static void wake_worker(struct iegd_worker *worker)
{
  struct task_wait *wait = worker->wait;
  worker->wait = NULL;
  if (wait) {
    task_wait_wake(wait);
  }
}

static void worker_entry(void *argument)
{
  struct iegd_worker *worker = argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    while (!worker->first && !worker->closing) {
      struct task_wait *wait = task_wait_prepare();
      KASSERT(!worker->wait);
      worker->wait = wait;
      task_wait_sleep(wait);
      KASSERT(!worker->wait);
    }
    struct iegd_work *work = worker->first;
    if (!work) {
      KASSERT(!worker->current && !worker->wait);
      worker->exited = true;
      struct task_wait *joined = worker->join_wait;
      worker->join_wait = NULL;
      if (joined) {
        task_wait_wake(joined);
      }
      /* Joining may release the record immediately; no further access. */
      cpu_restore_interrupts(flags);
      return;
    }
    worker->first = work->next;
    if (!worker->first) {
      worker->last = NULL;
    }
    work->next = NULL;
    work->pending = false;
    work->running = true;
    worker->current = work;
    cpu_restore_interrupts(flags);

    work->run(work);
    KASSERT(cpu_interrupts_enabled() && !vm_kernel_owner_held());

    flags = cpu_save_interrupts();
    KASSERT(worker->current == work && work->running);
    worker->current = NULL;
    work->running = false;
    struct task_wait *cancelled = work->cancel_wait;
    work->cancel_wait = NULL;
    if (cancelled) {
      task_wait_wake(cancelled);
    }
    /* The canceller may release WORK after wake. */
    cpu_restore_interrupts(flags);
  }
}

enum mm_result iegd_worker_start(struct iegd_worker *worker)
{
  require_bsp_masked();
  if (!worker || worker->started || worker->closing || worker->exited) {
    return MM_INVALID;
  }
  KASSERT(!worker->first && !worker->last && !worker->current &&
      !worker->wait && !worker->join_wait);
  enum mm_result result = kernel_task_create(worker_entry, worker);
  if (result == MM_OK) {
    worker->started = true;
  }
  return result;
}

void iegd_work_init(struct iegd_work *work, struct iegd_worker *worker,
    void (*run)(struct iegd_work *work))
{
  require_bsp_masked();
  KASSERT(work && worker && run && !work->worker && !work->pending && !work->running);
  *work = (struct iegd_work){.worker = worker, .run = run};
}

bool iegd_work_queue(struct iegd_work *work)
{
  require_bsp_masked();
  KASSERT(work && work->worker && work->run);
  struct iegd_worker *worker = work->worker;
  if (!worker->started || worker->closing || work->pending || work->cancelling) {
    return false;
  }
  work->pending = true;
  if (worker->last) {
    worker->last->next = work;
  } else {
    worker->first = work;
  }
  worker->last = work;
  wake_worker(worker);
  return true;
}

static bool remove_pending(struct iegd_work *work)
{
  if (!work->pending) {
    return false;
  }
  struct iegd_worker *worker = work->worker;
  struct iegd_work **link = &worker->first, *previous = NULL;
  while (*link != work) {
    KASSERT(*link);
    previous = *link;
    link = &(*link)->next;
  }
  *link = work->next;
  if (worker->last == work) {
    worker->last = previous;
  }
  work->next = NULL;
  work->pending = false;
  return true;
}

bool iegd_work_cancel_sync(struct iegd_work *work)
{
  require_join_context();
  uint64_t flags = cpu_save_interrupts();
  KASSERT(work && work->worker && !work->cancelling && !work->cancel_wait);
  work->cancelling = true;
  bool pending = remove_pending(work);
  if (work->running) {
    KASSERT(!kernel_task_is_current(worker_entry, work->worker));
    struct task_wait *wait = task_wait_prepare();
    work->cancel_wait = wait;
    task_wait_sleep(wait);
    KASSERT(!work->running && !work->cancel_wait);
  }
  work->cancelling = false;
  cpu_restore_interrupts(flags);
  return pending;
}

void iegd_worker_close(struct iegd_worker *worker)
{
  require_bsp_masked();
  KASSERT(worker);
  worker->closing = true;
  wake_worker(worker);
}

void iegd_worker_join(struct iegd_worker *worker)
{
  require_join_context();
  uint64_t flags = cpu_save_interrupts();
  KASSERT(worker && worker->closing && !worker->join_wait);
  KASSERT(!kernel_task_is_current(worker_entry, worker));
  if (!worker->started) {
    worker->exited = true;
  }
  if (!worker->exited) {
    struct task_wait *wait = task_wait_prepare();
    worker->join_wait = wait;
    task_wait_sleep(wait);
    /* worker_entry detached this wait before its final record access. */
    KASSERT(worker->exited && !worker->join_wait);
  }
  cpu_restore_interrupts(flags);
}

enum mm_result iegd_irq_attach(void (*handler)(void *context), void *context)
{
  require_bsp_masked();
  KASSERT(!irq_running);
  if (!handler || !context) {
    return MM_INVALID;
  }
  if (irq_handler) {
    return MM_BUSY;
  }
  irq_context = context;
  irq_handler = handler;
  return MM_OK;
}

enum mm_result iegd_irq_detach(void *context)
{
  require_bsp_masked();
  KASSERT(!irq_running);
  if (!irq_handler || irq_context != context) {
    return MM_INVALID;
  }
  irq_handler = NULL;
  irq_context = NULL;
  return MM_OK;
}

void iegd_irq_interrupt(void)
{
  require_bsp_masked();
  if (irq_handler) {
    KASSERT(!irq_running);
    irq_running = true;
    irq_handler(irq_context);
    KASSERT(!cpu_interrupts_enabled());
    irq_running = false;
  }
}
