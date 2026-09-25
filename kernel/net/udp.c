#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/mm/heap.h>
#include <kernel/net/ipv4.h>
#include <kernel/net/udp.h>
#include <kernel/panic.h>
#include <kernel/task.h>
#include <stdatomic.h>

#define UDP_ENDPOINT_LIMIT 16
#define UDP_CONTROL_LIMIT 8
#define UDP_EPHEMERAL_FIRST 49152
#define UDP_EPHEMERAL_LAST 65535

struct udp_endpoint {
  struct kernel_object object;
  struct udp_endpoint *next;
  struct udp_endpoint *retired_next;
  struct udp_endpoint_info local;
};

enum udp_control_operation { CONTROL_OPEN, CONTROL_INSPECT, CONTROL_SHUTDOWN };
enum udp_control_state { CONTROL_FREE, CONTROL_QUEUED, CONTROL_RUNNING, CONTROL_DONE };
struct udp_control {
  enum udp_control_state state;
  enum udp_control_operation operation;
  struct capability_table *table;
  struct udp_endpoint *endpoint;
  uint32_t address;
  uint16_t port;
  struct udp_open_reply reply;
  enum call_status status;
  struct task_wait *wait;
};

/* The worker owns the live list and all binding state. Control publication and
 * final-close handoff have their own lock, never held across allocation. */
static struct udp_endpoint *endpoints;
static size_t endpoint_count;
static uint16_t next_ephemeral = UDP_EPHEMERAL_FIRST;
static struct udp_control pending[UDP_CONTROL_LIMIT];
static struct udp_endpoint *retired;
static atomic_bool control_locked;

static void lock_control(void)
{
  while (atomic_exchange_explicit(&control_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_control(void)
{
  atomic_store_explicit(&control_locked, false, memory_order_release);
}

static void assert_worker_context(void)
{
  KASSERT(arch_cpu_index() == 0);
  uint64_t flags = cpu_save_interrupts();
  KASSERT(flags & RFLAGS_INTERRUPT_ENABLE);
  cpu_restore_interrupts(flags);
}

/* Object retirement owns the zero-reference allocation until this handoff.
 * No slot allocation or blocking is needed, even when all calls are occupied. */
static void retire_endpoint(struct kernel_object *object)
{
  struct udp_endpoint *endpoint = (struct udp_endpoint *)object;
  lock_control();
  endpoint->retired_next = retired;
  retired = endpoint;
  unlock_control();
  net_worker_notify();
}

static bool reap_endpoints(void)
{
  uint64_t flags = cpu_save_interrupts();
  lock_control();
  struct udp_endpoint *list = retired;
  retired = NULL;
  unlock_control();
  cpu_restore_interrupts(flags);

  bool worked = list != NULL;
  while (list) {
    struct udp_endpoint *endpoint = list;
    list = endpoint->retired_next;
    struct udp_endpoint **link = &endpoints;
    while (*link && *link != endpoint) {
      link = &(*link)->next;
    }
    KASSERT(*link == endpoint && endpoint_count);
    *link = endpoint->next;
    --endpoint_count;

    flags = cpu_save_interrupts();
    kfree(endpoint);
    cpu_restore_interrupts(flags);
  }
  return worked;
}

static bool port_bound(uint32_t address, uint16_t port)
{
  for (struct udp_endpoint *endpoint = endpoints; endpoint; endpoint = endpoint->next) {
    if (endpoint->local.state == UDP_STATE_BOUND &&
        endpoint->local.address == address && endpoint->local.port == port) {
      return true;
    }
  }
  return false;
}

static uint16_t ephemeral_port(uint32_t address)
{
  for (unsigned i = UDP_EPHEMERAL_FIRST; i <= UDP_EPHEMERAL_LAST; ++i) {
    uint16_t port = next_ephemeral;
    next_ephemeral = port == UDP_EPHEMERAL_LAST ? UDP_EPHEMERAL_FIRST : port + 1;
    if (!port_bound(address, port)) {
      return port;
    }
  }
  return 0;
}

static enum call_status open_endpoint(struct udp_control *call)
{
  if (!call->address || (!net_ipv4_is_loopback(call->address) &&
      call->address != net_ipv4_address())) {
    return CALL_UNAVAILABLE;
  }
  if (call->port && port_bound(call->address, call->port)) {
    return CALL_ALREADY_EXISTS;
  }
  if (endpoint_count == UDP_ENDPOINT_LIMIT) {
    return CALL_LIMIT;
  }
  uint16_t port = call->port ? call->port : ephemeral_port(call->address);
  if (!port) {
    return CALL_LIMIT;
  }

  uint64_t flags = cpu_save_interrupts();
  struct udp_endpoint *endpoint = kmalloc(sizeof(*endpoint));
  if (!endpoint) {
    cpu_restore_interrupts(flags);
    return CALL_NO_MEMORY;
  }
  *endpoint = (struct udp_endpoint){
    .local = {.address = call->address, .port = port, .state = UDP_STATE_BOUND},
  };
  object_init(&endpoint->object, OBJECT_UDP, retire_endpoint);
  enum capability_result result = capability_install(call->table, &endpoint->object,
      UDP_RIGHTS, &call->reply.handle);
  if (result != CAP_OK) {
    /* Unpublished: only the initial reference exists, with no binding to retire. */
    kfree(endpoint);
    cpu_restore_interrupts(flags);
    KASSERT(result == CAP_NO_MEMORY || result == CAP_LIMIT);
    return result == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
  }
  object_release(&endpoint->object);
  cpu_restore_interrupts(flags);

  endpoint->next = endpoints;
  endpoints = endpoint;
  ++endpoint_count;
  call->reply.local = endpoint->local;
  return CALL_OK;
}

static enum call_status apply_control(struct udp_control *call)
{
  switch (call->operation) {
  case CONTROL_OPEN:
    return open_endpoint(call);
  case CONTROL_INSPECT:
    call->reply.local = call->endpoint->local;
    return CALL_OK;
  case CONTROL_SHUTDOWN:
    /* Binding lookup ignores stopped objects, even while copied handles live. */
    call->endpoint->local.state = UDP_STATE_SHUTDOWN;
    return CALL_OK;
  }
  return CALL_BAD_OPERATION;
}

static enum call_status exchange_control(struct udp_control *request)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }
  lock_control();
  struct udp_control *call = NULL;
  for (size_t i = 0; i < UDP_CONTROL_LIMIT; ++i) {
    if (pending[i].state == CONTROL_FREE) {
      call = &pending[i];
      break;
    }
  }
  if (!call) {
    unlock_control();
    return CALL_QUEUE_FULL;
  }
  struct task_wait *wait = task_wait_prepare();
  *call = *request;
  call->state = CONTROL_QUEUED;
  call->wait = wait;
  unlock_control();
  net_worker_notify();
  task_wait_sleep(wait);

  lock_control();
  KASSERT(call->state == CONTROL_DONE && !call->wait);
  enum call_status status = call->status;
  if (status == CALL_OK) {
    request->reply = call->reply;
  }
  *call = (struct udp_control){0};
  unlock_control();
  return status;
}

enum call_status net_udp_open(struct capability_table *table, uint32_t address,
    uint16_t port, struct udp_open_reply *reply)
{
  struct udp_control request = {
    .operation = CONTROL_OPEN, .table = table, .address = address, .port = port,
  };
  enum call_status status = exchange_control(&request);
  if (status == CALL_OK) {
    *reply = request.reply;
  }
  return status;
}

enum call_status net_udp_inspect(struct kernel_object *object, struct udp_endpoint_info *reply)
{
  struct udp_control request = {
    .operation = CONTROL_INSPECT, .endpoint = (struct udp_endpoint *)object,
  };
  enum call_status status = exchange_control(&request);
  if (status == CALL_OK) {
    *reply = request.reply.local;
  }
  return status;
}

enum call_status net_udp_shutdown(struct kernel_object *object)
{
  struct udp_control request = {
    .operation = CONTROL_SHUTDOWN, .endpoint = (struct udp_endpoint *)object,
  };
  return exchange_control(&request);
}

bool net_udp_service(void)
{
  assert_worker_context();
  bool worked = reap_endpoints();
  for (size_t i = 0; i < UDP_CONTROL_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_control();
    struct udp_control *call = &pending[i];
    if (call->state != CONTROL_QUEUED) {
      unlock_control();
      cpu_restore_interrupts(flags);
      continue;
    }
    call->state = CONTROL_RUNNING;
    unlock_control();
    cpu_restore_interrupts(flags);

    /* The parked caller lends its table (OPEN) or keeps its endpoint grant.
     * RUNNING grants this worker exclusive access to the captured payload. */
    enum call_status status = apply_control(call);

    flags = cpu_save_interrupts();
    lock_control();
    call->status = status;
    call->state = CONTROL_DONE;
    struct task_wait *wait = call->wait;
    call->wait = NULL;
    task_wait_wake(wait);
    unlock_control();
    cpu_restore_interrupts(flags);
    worked = true;
  }
  return worked;
}

void net_udp_invalidate_address(uint32_t address)
{
  assert_worker_context();
  for (struct udp_endpoint *endpoint = endpoints; endpoint; endpoint = endpoint->next) {
    if (endpoint->local.state == UDP_STATE_BOUND && endpoint->local.address == address) {
      endpoint->local.state = UDP_STATE_UNAVAILABLE;
    }
  }
}
