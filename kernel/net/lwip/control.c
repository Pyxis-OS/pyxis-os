#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/net/tcp.h>
#include <kernel/task.h>
#include <caelum_hooks.h>
#include <stdatomic.h>
#include "connection.h"

#define TCP_CONTROL_LIMIT 8

enum control_operation { CONTROL_CONNECT, CONTROL_INSPECT, CONTROL_ABORT };
enum control_state { CONTROL_FREE, CONTROL_QUEUED, CONTROL_ACTIVE, CONTROL_DONE };

struct tcp_stream {
  struct kernel_object object;
  struct tcp_stream *retired_next;
  struct tcp_connection *connection; /* Sole external transport owner. */
};

struct tcp_control {
  enum control_state state;
  enum control_operation operation;
  struct capability_table *table;
  struct tcp_stream *stream;
  uint32_t address;
  uint16_t port;
  uint64_t deadline;
  struct tcp_connect_reply reply;
  enum call_status status;
  struct task_wait *wait;
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
    if (stream->connection) {
      net_tcp_release(stream->connection);
    }
    caelum_lwip_free(stream);
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

static enum call_status prepare_connect(struct tcp_control *call)
{
  if (task_deadline_expired(call->deadline)) {
    return CALL_TIMED_OUT;
  }
  struct tcp_stream *stream = caelum_lwip_calloc(1, sizeof(*stream));
  if (!stream) {
    return CALL_NO_MEMORY;
  }
  object_init(&stream->object, OBJECT_TCP, retire_stream);

  /* The sole task is parked and lends its table exclusively. Reserve the real
   * entry before sending anything; its handle is private until CALL completes.
   * Failed preparation removes it, so there can be no half-open user handle. */
  uint64_t flags = cpu_save_interrupts();
  enum capability_result installed = capability_install(call->table, &stream->object,
      TCP_RIGHTS, &call->reply.handle);
  cpu_restore_interrupts(flags);
  if (installed != CAP_OK) {
    KASSERT(installed == CAP_NO_MEMORY || installed == CAP_LIMIT);
    caelum_lwip_free(stream); /* Unpublished initial reference only. */
    return installed == CAP_NO_MEMORY ? CALL_NO_MEMORY : CALL_LIMIT;
  }
  call->stream = stream;
  enum net_result result = net_tcp_prepare(call->address, call->port,
      call->deadline, &stream->connection);
  if (result != NET_OK) {
    return prepare_status(result);
  }
  tcp_connection_start(stream->connection);
  return CALL_OK;
}

static void finish_connect(struct tcp_control *call, enum call_status status)
{
  struct tcp_stream *stream = call->stream;
  if (!stream) {
    return;
  }
  if (status == CALL_OK) {
    tcp_connection_inspect(stream->connection, &call->reply.connection);
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
  if (call->operation == CONTROL_CONNECT) {
    finish_connect(call, status);
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
  *call = (struct tcp_control){0};
  unlock_control();
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
    if (start) {
      call->state = CONTROL_ACTIVE;
    }
    unlock_control();
    cpu_restore_interrupts(flags);
    if (!start && !active) {
      continue;
    }
    if (call->operation != CONTROL_CONNECT) {
      if (call->operation == CONTROL_INSPECT) {
        tcp_connection_inspect(call->stream->connection, &call->reply.connection);
      } else {
        tcp_connection_abort(call->stream->connection, CALL_ENDPOINT_CLOSED);
      }
      complete_control(call, CALL_OK);
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
  *deadline = next;
  return found;
}
