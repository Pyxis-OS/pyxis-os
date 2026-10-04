#include <arch/cpu.h>
#include <kernel/net/config.h>
#include <kernel/net/driver.h>
#include <kernel/net/ipv4.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <stdatomic.h>

#define CONFIG_PENDING_LIMIT 8

enum config_state { CONFIG_FREE, CONFIG_QUEUED, CONFIG_RUNNING, CONFIG_DONE };
struct config_call {
  enum config_state state;
  uint64_t operation;
  struct net_config_request request;
  struct net_selector selector;
  struct net_config_reply reply;
  uint32_t after_id;
  struct net_controller_reply controller_reply;
  enum call_status status;
  struct task_wait *wait;
  bool cancelled;
};

static struct config_call pending[CONFIG_PENDING_LIMIT];
static atomic_bool pending_locked;
/* Only the network worker reads or writes the userspace-selected value. */
static uint32_t chosen_dns;

static void lock_pending(void)
{
  while (atomic_exchange_explicit(&pending_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_pending(void)
{
  atomic_store_explicit(&pending_locked, false, memory_order_release);
}

enum call_status net_config_exchange(uint64_t operation,
    const struct net_config_request *request, const struct net_selector *selector,
    struct net_config_reply *reply, uint32_t after_id,
    struct net_controller_reply *controller_reply)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }
  lock_pending();
  struct config_call *call = NULL;
  for (size_t i = 0; i < CONFIG_PENDING_LIMIT; ++i) {
    if (pending[i].state == CONFIG_FREE) {
      call = &pending[i];
      break;
    }
  }
  if (!call) {
    unlock_pending();
    return CALL_QUEUE_FULL;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = (struct config_call){
    .state = CONFIG_QUEUED, .operation = operation, .request = *request,
    .selector = *selector, .after_id = after_id, .wait = wait,
  };
  unlock_pending();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_pending();
  while (call->state != CONFIG_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_pending();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_pending();
  }
  KASSERT(!call->wait);
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  if (status == CALL_OK && (operation == NET_CONFIG_QUERY ||
      operation == NET_CONFIG_BIND || operation == NET_CONFIG_LOOKUP)) {
    *reply = call->reply;
  }
  if (status == CALL_OK && operation == NET_CONFIG_NEXT_CONTROLLER) {
    *controller_reply = call->controller_reply;
  }
  call->state = CONFIG_FREE;
  unlock_pending();
  return status;
}

static enum call_status configure(struct config_call *call)
{
  if ((call->operation == NET_CONFIG_REPLACE || call->operation == NET_CONFIG_SET_DNS) &&
      (!(call->request.dns_server >> 24) || call->request.dns_server >= UINT32_C(0xe0000000))) {
    return CALL_BAD_REQUEST;
  }
  switch (call->operation) {
  case NET_CONFIG_NEXT_CONTROLLER:
    net_driver_next_controller(call->after_id, &call->controller_reply);
    return CALL_OK;
  case NET_CONFIG_QUERY:
    net_ipv4_snapshot(&call->reply);
    call->reply.dns_server = chosen_dns;
    return CALL_OK;
  case NET_CONFIG_BIND: {
    enum call_status status = net_driver_bind(&call->selector);
    if (status == CALL_OK) {
      net_ipv4_snapshot(&call->reply);
      call->reply.dns_server = chosen_dns;
    }
    return status;
  }
  case NET_CONFIG_LOOKUP: {
    enum call_status status = net_driver_lookup(&call->selector, &call->reply);
    if (status == CALL_OK && (call->reply.flags & NET_CONFIG_BOUND)) {
      net_ipv4_snapshot(&call->reply);
    }
    if (status == CALL_OK) {
      call->reply.dns_server = chosen_dns;
    }
    return status;
  }
  case NET_CONFIG_CLEAR:
    net_ipv4_clear();
    return CALL_OK;
  case NET_CONFIG_SET_DNS:
    chosen_dns = call->request.dns_server;
    return CALL_OK;
  case NET_CONFIG_REPLACE: {
    enum net_result result = net_ipv4_configure(call->request.address,
        call->request.prefix, call->request.gateway);
    switch (result) {
    case NET_OK:
      chosen_dns = call->request.dns_server;
      return CALL_OK;
    case NET_INVALID: return CALL_BAD_REQUEST;
    default: return CALL_UNAVAILABLE;
    }
  }
  default:
    return CALL_BAD_OPERATION;
  }
}

bool net_config_service(void)
{
  bool worked = false;
  for (size_t i = 0; i < CONFIG_PENDING_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_pending();
    struct config_call *call = &pending[i];
    if (call->state != CONFIG_QUEUED) {
      unlock_pending();
      cpu_restore_interrupts(flags);
      continue;
    }
    bool cancelled = call->cancelled;
    call->state = CONFIG_RUNNING;
    unlock_pending();
    cpu_restore_interrupts(flags);

    /* RUNNING keeps the caller parked. Only this worker touches its payload;
     * no request lock spans ARP cleanup or another protocol's completion. */
    enum call_status status = cancelled ? CALL_ENDPOINT_CLOSED : configure(call);

    flags = cpu_save_interrupts();
    lock_pending();
    call->status = status;
    call->state = CONFIG_DONE;
    struct task_wait *wait = call->wait;
    call->wait = NULL;
    task_wait_wake(wait);
    unlock_pending();
    cpu_restore_interrupts(flags);
    worked = true;
  }
  return worked;
}
