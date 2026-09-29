#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/object/pipe.h>
#include <kernel/panic.h>
#include <kernel/service/request.h>
#include <kernel/task.h>
#include <stdatomic.h>

static struct bsp_request *request_head, *request_tail;
static atomic_bool requests_locked;
static struct task_wait *worker_wait; /* requests_locked; detached before notification. */
static bool initialized; /* Published to APs by scheduler startup. */

/* IF=0. Never enter the scheduler or a subsystem while holding this lock. */
static void lock_requests(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  while (atomic_exchange_explicit(&requests_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_requests(void)
{
  atomic_store_explicit(&requests_locked, false, memory_order_release);
}

struct bsp_request *bsp_request_prepare(enum bsp_service service)
{
  KASSERT(initialized);
  struct bsp_request *request = task_bsp_request_storage(service);
  KASSERT(request->state == BSP_REQUEST_FREE);
  *request = (struct bsp_request){
    .wait = task_wait_prepare(),
    .service = service,
    .state = BSP_REQUEST_PREPARED,
  };
  return request;
}

void bsp_request_submit_and_wait(struct bsp_request *request)
{
  KASSERT(initialized && request && request->state == BSP_REQUEST_PREPARED);
  KASSERT(request == task_bsp_request_storage(request->service));
  struct task_wait *wait = request->wait;
  KASSERT(wait && !request->next);

  lock_requests();
  request->state = BSP_REQUEST_QUEUED;
  if (request_tail) {
    request_tail->next = request;
  } else {
    request_head = request;
  }
  request_tail = request;
  struct task_wait *wake = worker_wait;
  worker_wait = NULL;
  unlock_requests();

  /* Only the first publisher takes the idle worker's waiter. A running or
   * already notified worker needs no additional notification. */
  if (wake) {
    task_wait_wake(wake);
  }

  /* Publication lends the record and subsystem resources. Only use the saved
   * wait until notification, even if service completes before we park. */
  task_wait_sleep(wait);
  KASSERT(request->state == BSP_REQUEST_COMPLETE && !request->wait && !request->next);
}

void bsp_request_release(struct bsp_request *request)
{
  KASSERT(request && request->state == BSP_REQUEST_COMPLETE);
  KASSERT(!request->next && !request->wait);
  *request = (struct bsp_request){0};
}

static void complete_request(struct bsp_request *request)
{
  KASSERT(request->state == BSP_REQUEST_SERVICING && !request->next);
  struct task_wait *wait = request->wait;
  request->wait = NULL;
  request->state = BSP_REQUEST_COMPLETE;
  /* Results and completion precede notification. The caller may consume,
   * reuse or retire storage as soon as wake publishes it; no accesses follow. */
  task_wait_wake(wait);
}

static void request_worker(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
    lock_requests();
    struct bsp_request *request = request_head;
    if (!request) {
      struct task_wait *wait = task_wait_prepare();
      KASSERT(!worker_wait);
      worker_wait = wait;
      unlock_requests();
      /* Always finish this wait: a publisher may already hold its pointer.
       * Queue inspection must not bypass notification and reuse the record. */
      task_wait_sleep(wait);
      cpu_restore_interrupts(flags);
      continue;
    }

    request_head = request->next;
    if (!request_head) {
      request_tail = NULL;
    }
    KASSERT(request->state == BSP_REQUEST_QUEUED);
    request->next = NULL;
    request->state = BSP_REQUEST_SERVICING;
    unlock_requests();

    switch (request->service) {
    case BSP_SERVICE_PIPE_CREATE:
      pipe_create_execute((struct pipe_create_request *)request);
      break;
    default:
      KASSERT(false);
    }
    complete_request(request);
    cpu_restore_interrupts(flags);
    kernel_task_yield_if_runnable();
  }
}

void bsp_requests_init(void)
{
  KASSERT(arch_cpu_index() == 0 && !initialized);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  enum mm_result result = kernel_task_create(request_worker, NULL);
  if (result != MM_OK) {
    panic("cannot create BSP request executor (error %u)", (unsigned)result);
  }
  initialized = true;
}
