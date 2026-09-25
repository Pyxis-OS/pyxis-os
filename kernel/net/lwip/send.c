#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include <caelum_hooks.h>
#include <stdatomic.h>
#include "stream.h"

#define TCP_WRITE_LIMIT 16

enum write_state { WRITE_FREE, WRITE_STAGING, WRITE_QUEUED, WRITE_ACTIVE, WRITE_DONE };

struct tcp_write_call {
  enum write_state state;
  struct tcp_stream *stream; /* The parked caller retains its grant. */
  struct task_wait *wait;
  uint64_t deadline;
  size_t length, accepted;
  enum call_status status;
  uint8_t data[TCP_WRITE_MAX_BYTES];
};

static struct tcp_write_call writes[TCP_WRITE_LIMIT];
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
    size_t length, uint64_t deadline, struct tcp_write_reply *reply)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(length <= TCP_WRITE_MAX_BYTES);
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return CALL_TIMED_OUT;
  }
  if (deadline - now > TCP_WRITE_MAX_WAIT_NS) {
    return CALL_BAD_REQUEST;
  }
  if (!length) {
    *reply = (struct tcp_write_reply){0};
    return CALL_OK;
  }
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }

  struct tcp_stream *stream = (struct tcp_stream *)object;
  lock_writes();
  struct tcp_write_call *call = NULL;
  for (size_t i = 0; i < TCP_WRITE_LIMIT; ++i) {
    if (writes[i].state == WRITE_FREE) {
      call = &writes[i];
    } else if (writes[i].stream == stream) {
      unlock_writes();
      return CALL_BUSY;
    }
  }
  if (!call) {
    unlock_writes();
    return CALL_QUEUE_FULL;
  }
  call->stream = stream;
  call->state = WRITE_STAGING;
  unlock_writes();

  /* Reserve the slot/direction before copying, without holding the shared lock
   * across the copy. Neither the worker nor another writer can use this slot. */
  memcpy(call->data, data, length);
  call->length = length;
  call->deadline = deadline;
  struct task_wait *wait = task_wait_prepare();
  lock_writes();
  call->wait = wait;
  call->state = WRITE_QUEUED;
  unlock_writes();
  net_worker_notify();
  task_wait_sleep(wait);

  lock_writes();
  KASSERT(call->state == WRITE_DONE && !call->wait);
  enum call_status status = call->status;
  if (status == CALL_OK) {
    reply->length = call->accepted;
  }
  *call = (struct tcp_write_call){0};
  unlock_writes();
  return status;
}

static void complete_write(struct tcp_write_call *call, enum call_status status)
{
  uint64_t flags = cpu_save_interrupts();
  lock_writes();
  call->status = status;
  call->state = WRITE_DONE;
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  task_wait_wake(wait);
  unlock_writes();
  cpu_restore_interrupts(flags);
  /* The caller can immediately reclaim this slot. */
}

/* false means backpressure: no bytes accepted, retry after worker wake/timer. */
static bool queue_write(struct tcp_write_call *call, enum call_status *status)
{
  struct tcp_connection *connection = call->stream->connection;
  *status = connection->terminal_status;
  if (*status != CALL_OK) {
    return true;
  }
  if (task_deadline_expired(call->deadline)) {
    *status = CALL_TIMED_OUT;
    return true;
  }
  struct tcp_pcb *pcb = connection->pcb;
  KASSERT(pcb);
  if (pcb->flags & TF_FIN) {
    *status = CALL_ENDPOINT_CLOSED;
    return true;
  }
  size_t available = tcp_sndbuf(pcb);
  if (!available || tcp_sndqueuelen(pcb) >= TCP_SND_QUEUELEN) {
    return false;
  }

  size_t length = call->length < available ? call->length : available;
  while (length) {
    err_t error = tcp_write(pcb, call->data, length, TCP_WRITE_FLAG_COPY);
    if (error == ERR_OK) {
      call->accepted = length;
      /* Acceptance is already committed. Output failure cannot undo it or make
       * the caller resend these bytes; lwIP retains them for its normal retry. */
      (void)tcp_output(pcb);
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
  for (size_t i = 0; i < TCP_WRITE_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_writes();
    struct tcp_write_call *call = &writes[i];
    enum write_state state = call->state;
    if (state == WRITE_QUEUED) {
      call->state = WRITE_ACTIVE;
    }
    unlock_writes();
    cpu_restore_interrupts(flags);

    if (state != WRITE_QUEUED && state != WRITE_ACTIVE) {
      continue;
    }
    enum call_status status;
    if (queue_write(call, &status)) {
      complete_write(call, status);
      worked = true;
    }
  }
  return worked;
}

bool tcp_writes_next_deadline(uint64_t *deadline)
{
  uint64_t flags = cpu_save_interrupts();
  lock_writes();
  uint64_t next = UINT64_MAX;
  bool found = false;
  for (size_t i = 0; i < TCP_WRITE_LIMIT; ++i) {
    struct tcp_write_call *call = &writes[i];
    if (call->state == WRITE_QUEUED) {
      next = 0;
      found = true;
      break;
    }
    if (call->state == WRITE_ACTIVE && call->deadline < next) {
      next = call->deadline;
      found = true;
    }
  }
  unlock_writes();
  cpu_restore_interrupts(flags);
  *deadline = next;
  return found;
}
