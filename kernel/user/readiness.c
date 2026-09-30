#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/net/interface.h>
#include <kernel/object/terminal.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <kernel/user/readiness.h>
#include <stdatomic.h>

/* Queue ownership is BSP/IF=0; the dedicated worker owns active at IF=1.
 * Notifications may originate on any CPU and do not borrow request storage. */
static struct bsp_request *incoming_head, *incoming_tail;
static struct bsp_request *active;
static atomic_bool worker_locked;
static struct task_wait *worker_wait;
static bool worker_notified;

static void lock_worker(void)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  while (atomic_exchange_explicit(&worker_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_worker(void)
{
  atomic_store_explicit(&worker_locked, false, memory_order_release);
}

void readiness_notify(void)
{
  lock_worker();
  worker_notified = true;
  struct task_wait *wake = worker_wait;
  worker_wait = NULL;
  if (wake) {
    task_wait_wake(wake);
  }
  unlock_worker();
  net_worker_notify();
}

void readiness_complete(struct readiness_request *request, enum call_status status)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  for (size_t i = 0; i < request->count; ++i) {
    object_release(request->interests[i].object);
    request->interests[i].object = NULL;
  }
  request->status = status;
  bsp_request_complete(&request->request);
}

bool readiness_service(struct bsp_request **active_list)
{
  bool worked = false;
  struct bsp_request **link = active_list;
  while (*link) {
    struct readiness_request *request = (struct readiness_request *)*link;
    bool ready = false;
    for (size_t i = 0; i < request->count; ++i) {
      struct readiness_interest *interest = &request->interests[i];
      switch (interest->object->type) {
      case OBJECT_TCP:
      case OBJECT_TCP_LISTENER:
        interest->ready = tcp_readiness_events(interest);
        break;
      case OBJECT_TERMINAL_ATTACHMENT:
        interest->ready = terminal_attachment_ready(interest->object, interest->events);
        break;
      default:
        KASSERT(false);
      }
      ready |= interest->ready != 0;
    }
    /* Current readiness wins over an expired deadline, including after worker
     * queueing delay. Polling is a successful empty observation, not timeout. */
    bool expired = request->deadline && arch_monotonic_ns() >= request->deadline;
    if (ready || !request->deadline || expired) {
      *link = request->request.next;
      request->request.next = NULL;
      uint64_t flags = cpu_save_interrupts();
      readiness_complete(request, ready || !request->deadline ? CALL_OK : CALL_TIMED_OUT);
      cpu_restore_interrupts(flags);
      worked = true;
    } else {
      link = &request->request.next;
    }
  }
  return worked;
}

bool readiness_next_deadline(struct bsp_request *active_list, uint64_t *deadline)
{
  uint64_t next = UINT64_MAX;
  bool found = false;
  for (struct bsp_request *entry = active_list; entry; entry = entry->next) {
    struct readiness_request *request = (struct readiness_request *)entry;
    if (!found || request->deadline < next) {
      next = request->deadline;
      found = true;
    }
  }
  *deadline = next;
  return found;
}

void readiness_submit(struct readiness_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED && !request->request.next);
  for (size_t i = 0; i < request->count; ++i) {
    enum object_type type = request->interests[i].object->type;
    if (type == OBJECT_TCP || type == OBJECT_TCP_LISTENER) {
      net_readiness_submit(request);
      return;
    }
  }
  if (incoming_tail) {
    incoming_tail->next = &request->request;
  } else {
    incoming_head = &request->request;
  }
  incoming_tail = &request->request;
  readiness_notify();
}

static void wait_for_work(void)
{
  uint64_t deadline;
  bool timed = readiness_next_deadline(active, &deadline);
  uint64_t flags = cpu_save_interrupts();
  lock_worker();
  if (worker_notified || incoming_head || (timed && task_deadline_expired(deadline))) {
    worker_notified = false;
    unlock_worker();
    cpu_restore_interrupts(flags);
    return;
  }
  struct task_wait *wait = task_wait_prepare();
  KASSERT(!worker_wait);
  worker_wait = wait;
  unlock_worker();
  if (timed) {
    task_wait_sleep_until(wait, deadline);
  } else {
    task_wait_sleep(wait);
  }
  lock_worker();
  worker_wait = NULL;
  unlock_worker();
  cpu_restore_interrupts(flags);
}

static void readiness_worker(void *argument)
{
  (void)argument;
  for (;;) {
    uint64_t flags = cpu_save_interrupts();
    if (incoming_head) {
      incoming_tail->next = active;
      active = incoming_head;
      incoming_head = incoming_tail = NULL;
    }
    cpu_restore_interrupts(flags);
    if (readiness_service(&active)) {
      kernel_task_yield_if_runnable();
    } else {
      wait_for_work();
    }
  }
}

void readiness_init(void)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  enum mm_result result = kernel_task_create(readiness_worker, NULL);
  if (result != MM_OK) {
    panic("cannot create readiness worker (error %u)", (unsigned)result);
  }
}
