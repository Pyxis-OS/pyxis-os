#include <kernel/object/forwarder.h>
#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/net/storage.h>
#include <kernel/task.h>
#include <caelum_hooks.h>
#include <stdatomic.h>
#include "stream.h"

enum write_state { WRITE_QUEUED, WRITE_ACTIVE, WRITE_DONE };

struct tcp_write_call {
  struct tcp_work work;
  enum write_state state;
  struct tcp_stream *stream; /* The parked caller retains its grant. */
  struct task_wait *wait;
  struct forwarder_operation *context;
  bool cancelled;
  uint64_t deadline;
  bool nonblocking;
  size_t length, accepted;
  enum call_status status;
  uint8_t data[];
};

static struct tcp_work_queue writes;
static atomic_bool writes_locked;

static void lock_writes(void)
{
  while (atomic_exchange_explicit(&writes_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_writes(void)
{
  atomic_store_explicit(&writes_locked, false, memory_order_release);
}

enum call_status net_tcp_write(struct kernel_object *object, const void *data,
    size_t length, uint64_t deadline, bool nonblocking, struct tcp_write_reply *reply)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(length <= TCP_WRITE_MAX_BYTES);
  if (!nonblocking) {
    uint64_t now = arch_monotonic_ns();
    if (deadline <= now) {
      return CALL_TIMED_OUT;
    }
    if (deadline - now > TCP_WRITE_MAX_WAIT_NS) {
      return CALL_BAD_REQUEST;
    }
  }
  if (!length) {
    *reply = (struct tcp_write_reply){0};
    return CALL_OK;
  }
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }

  struct tcp_stream *stream = (struct tcp_stream *)object;
  if (!tcp_write_available(stream)) {
    return CALL_BUSY;
  }
  struct tcp_write_call *call = net_storage_alloc(sizeof(*call) + length);
  if (!call) {
    return CALL_NO_MEMORY;
  }
  memset(call, 0, sizeof(*call));
  lock_writes();
  if (stream->write_busy) {
    unlock_writes();
    net_storage_free(call);
    return CALL_BUSY;
  }
  stream->write_busy = true;
  call->stream = stream;
  unlock_writes();
  readiness_source_notify(&stream->readiness);

  /* Reserve the direction before copying, outside the shared lock. The worker
   * cannot see the record until the captured input is fully queued. */
  memcpy(call->data, data, length);
  call->length = length;
  call->deadline = deadline;
  call->nonblocking = nonblocking;
  struct task_wait *wait = task_wait_prepare();
  lock_writes();
  call->wait = wait;
  call->context = task_forwarder_operation();
  call->state = WRITE_QUEUED;
  tcp_work_append(&writes, &call->work);
  unlock_writes();
  net_worker_notify();
  task_wait_sleep_interruptible(wait);

  lock_writes();
  while (call->state != WRITE_DONE) {
    KASSERT(call->wait == wait);
    call->wait = NULL;
    call->cancelled = true;
    tcp_work_changed(&writes);
    wait = task_wait_prepare();
    call->wait = wait;
    unlock_writes();
    net_worker_notify();
    task_wait_sleep(wait);
    lock_writes();
  }
  KASSERT(!call->wait);
  enum call_status status = task_stop_requested() ? CALL_ENDPOINT_CLOSED : call->status;
  if (status == CALL_OK) {
    reply->length = call->accepted;
  }
  stream->write_busy = false;
  unlock_writes();
  net_storage_free(call);
  readiness_source_notify(&stream->readiness);
  net_worker_notify();
  return status;
}

static void complete_write(struct tcp_write_call *call, enum call_status status)
{
  uint64_t flags = cpu_save_interrupts();
  lock_writes();
  tcp_work_remove(&writes, &call->work);
  call->status = status;
  call->state = WRITE_DONE;
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  task_wait_wake(wait);
  unlock_writes();
  cpu_restore_interrupts(flags);
  /* The caller can immediately free this record. */
}

/* false means backpressure: no bytes accepted, retry after worker wake/timer. */
static bool queue_write(struct tcp_write_call *call, enum call_status *status)
{
  struct tcp_connection *connection = call->stream->connection;
  tcp_connection_refresh(connection);
  *status = connection->terminal_status;
  if (*status != CALL_OK) {
    return true;
  }
  if (!call->nonblocking && task_deadline_expired(call->deadline)) {
    *status = CALL_TIMED_OUT;
    return true;
  }
  if (connection->write_shutdown) {
    *status = CALL_ENDPOINT_CLOSED;
    return true;
  }
  struct tcp_pcb *pcb = connection->pcb;
  KASSERT(pcb);
  size_t available = tcp_sndbuf(pcb);
  if (!available || tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN) {
    return false;
  }

  size_t length = call->length < available ? call->length : available;
  while (length) {
    uint64_t flags = cpu_save_interrupts();
    bool started = forwarder_operation_start(call->context);
    cpu_restore_interrupts(flags);
    if (!started) {
      *status = CALL_DENIED;
      return true;
    }
    err_t error = tcp_write(pcb, call->data, length, TCP_WRITE_FLAG_COPY);
    if (error == ERR_OK) {
      call->accepted = length;
      /* Acceptance is already committed. Output failure cannot undo it or make
       * the caller resend these bytes; lwIP retains them for its normal retry. */
      (void)tcp_output(pcb);
      tcp_connection_readiness_notify(connection);
      return true;
    }
    if (error != ERR_MEM) {
      *status = error == ERR_CONN ? CALL_ENDPOINT_CLOSED : CALL_IO;
      return true;
    }
    /* A byte budget does not imply enough pbuf slots for this whole write.
     * lwIP rolls back failed writes. Bounded smaller attempts allow a short
     * result near that limit, including with an unusually small peer MSS. */
    length /= 2;
  }
  /* With byte and pbuf room, even a one-byte copy failed to allocate. No bytes
   * from this call were committed; previously accepted data remains owned. */
  *status = CALL_NO_MEMORY;
  return true;
}

bool tcp_writes_service(void)
{
  net_worker_assert_context();
  bool worked = false;
  uint64_t begin_flags = cpu_save_interrupts();
  lock_writes();
  tcp_work_begin(&writes);
  unlock_writes();
  cpu_restore_interrupts(begin_flags);
  for (size_t i = 0; i < TCP_WORK_BATCH; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_writes();
    struct tcp_write_call *call = (struct tcp_write_call *)tcp_work_next(&writes);
    if (!call) {
      unlock_writes();
      cpu_restore_interrupts(flags);
      break;
    }
    if (!call->nonblocking) {
      tcp_work_deadline(&writes, call->deadline);
    }
    enum write_state state = call->state;
    bool cancelled = call->cancelled;
    if (state == WRITE_QUEUED) {
      call->state = WRITE_ACTIVE;
    }
    unlock_writes();
    cpu_restore_interrupts(flags);

    if (state != WRITE_QUEUED && state != WRITE_ACTIVE) {
      continue;
    }
    enum call_status status = CALL_ENDPOINT_CLOSED;
    flags = cpu_save_interrupts();
    bool denied = forwarder_operation_denied(call->context);
    cpu_restore_interrupts(flags);
    if (denied) {
      status = CALL_DENIED;
    }
    bool complete = denied || cancelled || queue_write(call, &status);
    if (!complete && call->nonblocking) {
      status = CALL_WOULD_BLOCK;
      complete = true;
    }
    if (complete) {
      complete_write(call, status);
      worked = true;
    }
  }
  return worked;
}

void tcp_writes_changed(void)
{
  tcp_work_changed(&writes);
}

bool tcp_writes_next_deadline(uint64_t *deadline)
{
  uint64_t flags = cpu_save_interrupts();
  lock_writes();
  bool found = tcp_work_next_deadline(&writes, deadline);
  unlock_writes();
  cpu_restore_interrupts(flags);
  return found;
}

bool tcp_write_available(struct tcp_stream *stream)
{
  uint64_t flags = cpu_save_interrupts();
  lock_writes();
  bool available = !stream->write_busy;
  unlock_writes();
  cpu_restore_interrupts(flags);
  return available;
}
