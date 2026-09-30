#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/net/tcp.h>
#include <kernel/object/execution_group.h>
#include <kernel/task.h>
#include <caelum_hooks.h>
#include <stdatomic.h>
#include "stream.h"

#define TCP_CONTROL_LIMIT 8

enum control_operation {
  CONTROL_CONNECT, CONTROL_INSPECT, CONTROL_ABORT, CONTROL_SHUTDOWN_WRITE,
  CONTROL_LISTEN, CONTROL_ACCEPT, CONTROL_LISTENER_INSPECT,
};
enum control_state { CONTROL_FREE, CONTROL_QUEUED, CONTROL_ACTIVE, CONTROL_DONE };

struct tcp_control {
  enum control_state state;
  enum control_operation operation;
  struct capability_table *table;
  struct tcp_stream *stream;
  struct tcp_stream *listener;
  uint32_t address;
  uint16_t port;
  uint64_t deadline;
  bool nonblocking;
  struct tcp_connect_reply reply;
  struct tcp_listen_reply listen_reply;
  enum call_status status;
  struct task_wait *wait;
  bool cancelled;
  struct execution_group *cleanup_group; /* Attributes failed provisional objects. */
};

static struct tcp_control pending[TCP_CONTROL_LIMIT];
static struct tcp_stream *retired;
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

/* BSP object retirement, IF=0. Intrusive handoff cannot need a free call slot. */
static void retire_stream(struct kernel_object *object)
{
  struct tcp_stream *stream = (struct tcp_stream *)object;
  stream->cleanup_group = object_cleanup_defer();
  lock_control();
  stream->retired_next = retired;
  retired = stream;
  unlock_control();
  net_worker_notify();
}

static bool reap_streams(void)
{
  uint64_t flags = cpu_save_interrupts();
  lock_control();
  struct tcp_stream *list = retired;
  retired = NULL;
  unlock_control();
  cpu_restore_interrupts(flags);

  bool worked = list != NULL;
  while (list) {
    struct tcp_stream *stream = list;
    list = stream->retired_next;
    flags = cpu_save_interrupts();
    struct execution_group *group = stream->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(group);
    cpu_restore_interrupts(flags);
    if (stream->connection) {
      net_tcp_release(stream->connection);
    }
    caelum_lwip_free(stream);
    flags = cpu_save_interrupts();
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    cpu_restore_interrupts(flags);
  }
  return worked;
}

static enum call_status prepare_status(enum net_result result)
{
  switch (result) {
  case NET_OK: return CALL_OK;
  case NET_INVALID: return CALL_BAD_REQUEST;
  case NET_UNAVAILABLE: return CALL_UNAVAILABLE;
  case NET_QUEUE_FULL: return CALL_QUEUE_FULL;
  case NET_NO_ROUTE: return CALL_NO_ROUTE;
  case NET_NO_MEMORY: return CALL_NO_MEMORY;
  case NET_TIMED_OUT: return CALL_TIMED_OUT;
  }
  return CALL_IO;
}

static enum call_status reserve_handle(struct tcp_control *call, bool listening)
{
  struct tcp_stream *stream = caelum_lwip_calloc(1, sizeof(*stream));
  if (!stream) {
    return CALL_NO_MEMORY;
  }
  object_init(&stream->object, listening ? OBJECT_TCP_LISTENER : OBJECT_TCP, retire_stream);

  /* The sole task is parked and lends its table exclusively. Reserve the real
   * entry before sending anything; its handle is private until CALL completes.
   * Failed preparation removes it, so there can be no half-open user handle. */
  uint64_t flags = cpu_save_interrupts();
  enum capability_result installed = capability_install(call->table, &stream->object,
      listening ? TCP_LISTENER_RIGHTS : TCP_RIGHTS, 0, &call->reply.handle);
  cpu_restore_interrupts(flags);
  if (installed != CAP_OK) {
    KASSERT(installed == CAP_NO_MEMORY || installed == CAP_LIMIT);
    caelum_lwip_free(stream); /* Unpublished initial reference only. */
    return installed == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
  }
  call->stream = stream;
  return CALL_OK;
}

static enum call_status prepare_connect(struct tcp_control *call)
{
  if (task_deadline_expired(call->deadline)) {
    return CALL_TIMED_OUT;
  }
  enum call_status status = reserve_handle(call, false);
  if (status != CALL_OK) {
    return status;
  }
  struct tcp_stream *stream = call->stream;
  enum net_result result = net_tcp_prepare(call->address, call->port,
      call->deadline, &stream->connection);
  if (result != NET_OK) {
    return prepare_status(result);
  }
  tcp_connection_start(stream->connection);
  return CALL_OK;
}

static void finish_created(struct tcp_control *call, enum call_status status)
{
  struct tcp_stream *stream = call->stream;
  if (!stream) {
    return;
  }
  if (status == CALL_OK) {
    if (call->operation == CONTROL_LISTEN) {
      call->listen_reply.handle = call->reply.handle;
      tcp_listener_inspect(stream->connection, &call->listen_reply.listener);
    } else {
      tcp_connection_inspect(stream->connection, &call->reply.connection);
    }
  } else if (stream->connection) {
    /* Synchronous abort precedes the failure reply, even if object retirement
     * is delayed. Neither a PCB nor an ARP/local copy may outlive a failed open. */
    net_tcp_release(stream->connection);
    stream->connection = NULL;
  }

  uint64_t flags = cpu_save_interrupts();
  if (status != CALL_OK) {
    KASSERT(capability_close(call->table, call->reply.handle) == CAP_OK);
  }
  object_release(&stream->object); /* Table owns successful streams from here. */
  cpu_restore_interrupts(flags);
  call->stream = NULL;
  call->table = NULL;
}

/* ACTIVE grants the worker exclusive access until completion publication. */
static void complete_control(struct tcp_control *call, enum call_status status)
{
  if (call->operation == CONTROL_CONNECT || call->operation == CONTROL_LISTEN ||
      call->operation == CONTROL_ACCEPT) {
    uint64_t flags = cpu_save_interrupts();
    struct execution_group *group = call->cleanup_group;
    struct execution_group *previous = object_cleanup_enter(group);
    cpu_restore_interrupts(flags);
    finish_created(call, status);
    flags = cpu_save_interrupts();
    object_cleanup_leave(previous);
    if (group) {
      execution_group_cleanup_end(group);
    }
    cpu_restore_interrupts(flags);
  }
  uint64_t flags = cpu_save_interrupts();
  lock_control();
  call->status = status;
  call->state = CONTROL_DONE;
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  task_wait_wake(wait);
  unlock_control();
  cpu_restore_interrupts(flags);
  /* The caller may already have consumed and reused the slot. */
}

static enum call_status exchange_control(struct tcp_control *request)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }
  lock_control();
  struct tcp_control *call = NULL;
  for (size_t i = 0; i < TCP_CONTROL_LIMIT; ++i) {
    if (pending[i].state == CONTROL_FREE) {
      call = &pending[i];
    } else if (request->operation == CONTROL_ACCEPT &&
        pending[i].operation == CONTROL_ACCEPT && pending[i].listener == request->listener) {
      unlock_control();
      return CALL_BUSY;
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
  if (call->operation == CONTROL_CONNECT || call->operation == CONTROL_LISTEN ||
      call->operation == CONTROL_ACCEPT) {
    call->cleanup_group = object_cleanup_defer();
  }
  unlock_control();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_control();
  /* A stop wake does not return the loan. Detach before renewing the wait and
   * let the worker cancel or finish before collecting the slot. */
  while (call->state != CONTROL_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_control();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_control();
  }
  KASSERT(!call->wait);
  bool discard_created = task_stop_requested() && call->status == CALL_OK &&
      (request->operation == CONTROL_CONNECT || request->operation == CONTROL_LISTEN ||
       request->operation == CONTROL_ACCEPT);
  uint64_t created_handle = call->reply.handle;
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  if (status == CALL_OK) {
    request->reply = call->reply;
    request->listen_reply = call->listen_reply;
  }
  *call = (struct tcp_control){0};
  unlock_control();
  if (discard_created) {
    KASSERT(capability_close(request->table, created_handle) == CAP_OK);
  }
  net_worker_notify(); /* Direction ownership became available to readiness waits. */
  return status;
}

enum call_status net_tcp_connect(struct capability_table *table, uint32_t address,
    uint16_t port, uint64_t deadline, struct tcp_connect_reply *reply)
{
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return CALL_TIMED_OUT;
  }
  if (!port || deadline - now > TCP_CONNECT_MAX_WAIT_NS) {
    return CALL_BAD_REQUEST;
  }
  struct tcp_control request = {
    .operation = CONTROL_CONNECT, .table = table, .address = address,
    .port = port, .deadline = deadline,
  };
  enum call_status status = exchange_control(&request);
  if (status == CALL_OK) {
    *reply = request.reply;
  }
  return status;
}

enum call_status net_tcp_listen(struct capability_table *table, uint32_t address,
    uint16_t port, struct tcp_listen_reply *reply)
{
  struct tcp_control request = {
    .operation = CONTROL_LISTEN, .table = table, .address = address, .port = port,
  };
  enum call_status status = exchange_control(&request);
  if (status == CALL_OK) {
    *reply = request.listen_reply;
  }
  return status;
}

enum call_status net_tcp_accept(struct kernel_object *object, struct capability_table *table,
    uint64_t deadline, bool nonblocking, struct tcp_accept_reply *reply)
{
  uint64_t now = arch_monotonic_ns();
  if (!nonblocking && deadline <= now) {
    return CALL_TIMED_OUT;
  }
  if (!nonblocking && deadline - now > TCP_ACCEPT_MAX_WAIT_NS) {
    return CALL_BAD_REQUEST;
  }
  struct tcp_control request = {
    .operation = CONTROL_ACCEPT, .table = table,
    .listener = (struct tcp_stream *)object, .deadline = deadline, .nonblocking = nonblocking,
  };
  enum call_status status = exchange_control(&request);
  if (status == CALL_OK) {
    *reply = (struct tcp_accept_reply){request.reply.handle, request.reply.connection};
  }
  return status;
}

enum call_status net_tcp_listener_inspect(struct kernel_object *object,
    struct tcp_listener_info *reply)
{
  struct tcp_control request = {
    .operation = CONTROL_LISTENER_INSPECT, .listener = (struct tcp_stream *)object,
  };
  enum call_status status = exchange_control(&request);
  if (status == CALL_OK) {
    *reply = request.listen_reply.listener;
  }
  return status;
}

enum call_status net_tcp_inspect(struct kernel_object *object, struct tcp_connection_info *reply)
{
  struct tcp_control request = {
    .operation = CONTROL_INSPECT, .stream = (struct tcp_stream *)object,
  };
  enum call_status status = exchange_control(&request);
  if (status == CALL_OK) {
    *reply = request.reply.connection;
  }
  return status;
}

enum call_status net_tcp_abort(struct kernel_object *object)
{
  struct tcp_control request = {
    .operation = CONTROL_ABORT, .stream = (struct tcp_stream *)object,
  };
  return exchange_control(&request);
}

enum call_status net_tcp_shutdown_write(struct kernel_object *object)
{
  struct tcp_control request = {
    .operation = CONTROL_SHUTDOWN_WRITE, .stream = (struct tcp_stream *)object,
  };
  return exchange_control(&request);
}

bool net_tcp_service(void)
{
  net_worker_assert_context();
  bool worked = reap_streams();
  for (size_t i = 0; i < TCP_CONTROL_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_control();
    struct tcp_control *call = &pending[i];
    bool start = call->state == CONTROL_QUEUED;
    bool active = call->state == CONTROL_ACTIVE;
    bool cancelled = call->cancelled;
    if (start) {
      call->state = CONTROL_ACTIVE;
    }
    unlock_control();
    cpu_restore_interrupts(flags);
    if (!start && !active) {
      continue;
    }
    if (cancelled) {
      complete_control(call, CALL_ENDPOINT_CLOSED);
      worked = true;
      continue;
    }
    if (call->operation == CONTROL_LISTEN) {
      enum call_status status = reserve_handle(call, true);
      if (status == CALL_OK) {
        status = prepare_status(tcp_listener_prepare(call->address, call->port,
            &call->stream->connection));
      }
      complete_control(call, status);
      worked = true;
      continue;
    }
    if (call->operation == CONTROL_ACCEPT) {
      struct tcp_connection *listener = call->listener->connection;
      enum call_status status = listener->terminal_status;
      if (!call->nonblocking && task_deadline_expired(call->deadline)) {
        status = CALL_TIMED_OUT;
      }
      if (status == CALL_OK && call->nonblocking && !listener->ready_head) {
        /* No provisional stream or handle exists when a try cannot accept. */
        status = CALL_WOULD_BLOCK;
      }
      if (status == CALL_OK && start) {
        status = reserve_handle(call, false);
        worked = true;
      }
      if (status == CALL_OK && !call->nonblocking && task_deadline_expired(call->deadline)) {
        status = CALL_TIMED_OUT;
      }
      if (status == CALL_OK) {
        call->stream->connection = tcp_listener_take(listener);
        if (!call->stream->connection) {
          KASSERT(!call->nonblocking);
          continue;
        }
      }
      complete_control(call, status);
      worked = true;
      continue;
    }
    if (call->operation == CONTROL_LISTENER_INSPECT) {
      tcp_listener_inspect(call->listener->connection, &call->listen_reply.listener);
      complete_control(call, CALL_OK);
      worked = true;
      continue;
    }
    if (call->operation != CONTROL_CONNECT) {
      enum call_status status = CALL_OK;
      if (call->operation == CONTROL_INSPECT) {
        tcp_connection_inspect(call->stream->connection, &call->reply.connection);
      } else if (call->operation == CONTROL_SHUTDOWN_WRITE) {
        status = tcp_connection_shutdown_write(call->stream->connection);
      } else {
        tcp_connection_abort(call->stream->connection, CALL_ENDPOINT_CLOSED);
      }
      complete_control(call, status);
      worked = true;
      continue;
    }
    if (start) {
      enum call_status status = prepare_connect(call);
      worked = true;
      if (status != CALL_OK) {
        complete_control(call, status);
        continue;
      }
    }

    struct tcp_connection *connection = call->stream->connection;
    enum call_status status = connection->terminal_status;
    if (task_deadline_expired(call->deadline)) {
      status = CALL_TIMED_OUT;
    } else if (status == CALL_OK && !connection->connected) {
      continue;
    }
    complete_control(call, status);
    worked = true;
  }
  if (tcp_reads_service()) {
    worked = true;
  }
  if (tcp_writes_service()) {
    worked = true;
  }
  if (tcp_readiness_service()) {
    worked = true;
  }
  return worked;
}

bool net_tcp_next_deadline(uint64_t *deadline)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  lock_control();
  uint64_t next = UINT64_MAX;
  bool found = false;
  for (size_t i = 0; i < TCP_CONTROL_LIMIT; ++i) {
    struct tcp_control *call = &pending[i];
    if (call->state == CONTROL_QUEUED) {
      next = 0;
      found = true;
      break;
    }
    if (call->state == CONTROL_ACTIVE && call->deadline < next) {
      next = call->deadline;
      found = true;
    }
  }
  unlock_control();
  cpu_restore_interrupts(flags);
  uint64_t read_deadline;
  if (tcp_reads_next_deadline(&read_deadline)) {
    if (read_deadline < next) {
      next = read_deadline;
    }
    found = true;
  }
  uint64_t write_deadline;
  if (tcp_writes_next_deadline(&write_deadline)) {
    if (write_deadline < next) {
      next = write_deadline;
    }
    found = true;
  }
  uint64_t readiness_deadline;
  if (tcp_readiness_next_deadline(&readiness_deadline)) {
    if (readiness_deadline < next) {
      next = readiness_deadline;
    }
    found = true;
  }
  *deadline = next;
  return found;
}

bool tcp_accept_available(struct tcp_stream *stream)
{
  uint64_t flags = cpu_save_interrupts();
  lock_control();
  bool available = true;
  for (size_t i = 0; i < TCP_CONTROL_LIMIT; ++i) {
    if (pending[i].state != CONTROL_FREE && pending[i].operation == CONTROL_ACCEPT &&
        pending[i].listener == stream) {
      available = false;
      break;
    }
  }
  unlock_control();
  cpu_restore_interrupts(flags);
  return available;
}
