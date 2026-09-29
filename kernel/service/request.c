#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/object/pipe.h>
#include <kernel/panic.h>
#include <kernel/service/request.h>
#include <kernel/task.h>
#include <stdatomic.h>

static struct bsp_request *request_head, *request_tail;
static atomic_bool requests_locked;

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
  KASSERT(request && request->state == BSP_REQUEST_PREPARED);
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
  unlock_requests();

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

void bsp_requests_service(void)
{
  KASSERT(arch_cpu_index() == 0);
  lock_requests();
  struct bsp_request *request = request_head;
  request_head = request_tail = NULL;
  unlock_requests();

  while (request) {
    struct bsp_request *next = request->next;
    KASSERT(request->state == BSP_REQUEST_QUEUED);
    request->next = NULL;
    request->state = BSP_REQUEST_SERVICING;
    switch (request->service) {
    case BSP_SERVICE_PIPE_CREATE:
      pipe_create_execute((struct pipe_create_request *)request);
      break;
    default:
      KASSERT(false);
    }
    complete_request(request);
    request = next;
  }
}

bool bsp_requests_pending(void)
{
  lock_requests();
  bool pending = request_head != NULL;
  unlock_requests();
  return pending;
}
