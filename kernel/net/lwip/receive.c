#include <arch/clock.h>
#include <arch/cpu.h>
#include <kernel/memory.h>
#include <kernel/task.h>
#include <caelum_hooks.h>
#include <stdatomic.h>
#include "stream.h"

#define TCP_READ_LIMIT 16

enum read_state { READ_FREE, READ_QUEUED, READ_ACTIVE, READ_DONE, READ_CONSUMED, READ_RECLAIMING };

struct tcp_read {
  enum read_state state;
  struct tcp_stream *stream; /* Owned until the worker returns receive credit. */
  struct task_wait *wait;
  uint64_t deadline;
  bool nonblocking;
  size_t capacity, length;
  enum call_status status;
  uint8_t data[TCP_READ_MAX_BYTES];
};

static struct tcp_read reads[TCP_READ_LIMIT];
static atomic_bool reads_locked;

static void lock_reads(void)
{
  while (atomic_exchange_explicit(&reads_locked, true, memory_order_acquire)) {
    __asm__ volatile("pause");
  }
}

static void unlock_reads(void)
{
  atomic_store_explicit(&reads_locked, false, memory_order_release);
}

void tcp_connection_discard_receive(struct tcp_connection *connection)
{
  caelum_lwip_free(connection->receive_data);
  connection->receive_data = NULL;
  connection->receive_head = 0;
  connection->receive_length = 0;
}

err_t tcp_connection_receive(void *argument, struct tcp_pcb *pcb,
    struct pbuf *buffer, err_t error)
{
  struct tcp_connection *connection = argument;
  KASSERT(connection->pcb == pcb && error == ERR_OK);
  if (!buffer) {
    connection->peer_fin = true;
    if (!connection->receive_length) {
      tcp_connection_discard_receive(connection);
    }
    return ERR_OK;
  }
  if (!connection->receive_data) {
    connection->receive_data = caelum_lwip_malloc(NET_TCP_RECEIVE_BYTES);
    if (!connection->receive_data) {
      pbuf_free(buffer);
      tcp_connection_abort(connection, CALL_NO_MEMORY);
      return ERR_ABRT;
    }
  }

  /* lwIP trims duplicates/overlaps and delivers only contiguous data. No credit
   * is returned here: its window covers this ring, uncollected read replies and
   * out-of-order sequence space together. The ring avoids per-packet metadata
   * growth when a peer sends many tiny, ordered segments. */
  size_t length = buffer->tot_len;
  KASSERT(length <= NET_TCP_RECEIVE_BYTES - connection->receive_length);
  size_t tail = (connection->receive_head + connection->receive_length) % NET_TCP_RECEIVE_BYTES;
  size_t first = NET_TCP_RECEIVE_BYTES - tail;
  if (first > length) {
    first = length;
  }
  KASSERT(pbuf_copy_partial(buffer, connection->receive_data + tail, first, 0) == first);
  KASSERT(pbuf_copy_partial(buffer, connection->receive_data, length - first, first) == length - first);
  connection->receive_length += length;
  pbuf_free(buffer);
  return ERR_OK;
}

static size_t read_ordered(struct tcp_connection *connection, void *data, size_t capacity)
{
  size_t length = connection->receive_length < capacity ? connection->receive_length : capacity;
  size_t first = NET_TCP_RECEIVE_BYTES - connection->receive_head;
  if (first > length) {
    first = length;
  }
  if (length) {
    memcpy(data, connection->receive_data + connection->receive_head, first);
    memcpy((uint8_t *)data + first, connection->receive_data, length - first);
    connection->receive_head = (connection->receive_head + length) % NET_TCP_RECEIVE_BYTES;
    connection->receive_length -= length;
    if (!connection->receive_length && connection->peer_fin) {
      tcp_connection_discard_receive(connection);
    }
  }
  return length;
}

static void return_receive_credit(struct tcp_connection *connection, size_t length)
{
  /* TIME_WAIT no longer receives stream data; tcp_output on that PCB is not
   * part of the active send path. The last credit may arrive after its FIN. */
  if (length && connection->pcb && connection->pcb->state != TIME_WAIT &&
      connection->terminal_status == CALL_OK) {
    tcp_recved(connection->pcb, length);
  }
}

enum call_status net_tcp_read(struct kernel_object *object, size_t capacity,
    uint64_t deadline, bool nonblocking, void *data, struct tcp_read_reply *reply)
{
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(capacity <= TCP_READ_MAX_BYTES);
  uint64_t now = arch_monotonic_ns();
  if (!nonblocking && deadline <= now) {
    return CALL_TIMED_OUT;
  }
  if (!nonblocking && deadline - now > TCP_READ_MAX_WAIT_NS) {
    return CALL_BAD_REQUEST;
  }
  if (!capacity) {
    *reply = (struct tcp_read_reply){0};
    return CALL_OK;
  }
  if (!net_worker_available()) {
    return CALL_UNAVAILABLE;
  }

  struct tcp_stream *stream = (struct tcp_stream *)object;
  lock_reads();
  struct tcp_read *call = NULL;
  for (size_t i = 0; i < TCP_READ_LIMIT; ++i) {
    struct tcp_read *slot = &reads[i];
    if (slot->state == READ_FREE) {
      call = slot;
    } else if (slot->stream == stream && slot->state != READ_CONSUMED &&
               slot->state != READ_RECLAIMING) {
      unlock_reads();
      return CALL_BUSY;
    }
  }
  if (!call || !object_retain(object)) {
    unlock_reads();
    return call ? CALL_LIMIT : CALL_QUEUE_FULL;
  }
  struct task_wait *wait = task_wait_prepare();
  call->stream = stream;
  call->capacity = capacity;
  call->deadline = deadline;
  call->nonblocking = nonblocking;
  call->wait = wait;
  call->state = READ_QUEUED;
  unlock_reads();
  net_worker_notify();
  task_wait_sleep(wait);

  /* DONE is immutable until this caller collects it. Copy outside the lock;
   * successful bytes have already left the stream even if an abort follows. */
  lock_reads();
  KASSERT(call->state == READ_DONE && !call->wait);
  enum call_status status = call->status;
  unlock_reads();
  if (status == CALL_OK) {
    memcpy(data, call->data, call->length);
    reply->length = call->length;
  }
  lock_reads();
  if (status == CALL_OK) {
    call->state = READ_CONSUMED;
  } else {
    /* No bytes/credit were transferred. A failed try leaves no pending read
     * or retained operation reference for a later worker pass to reclaim. */
    *call = (struct tcp_read){0};
  }
  unlock_reads();
  if (status != CALL_OK) {
    object_release(object);
  }
  net_worker_notify();
  return status;
}

static void complete_read(struct tcp_read *call, enum call_status status)
{
  uint64_t flags = cpu_save_interrupts();
  lock_reads();
  call->status = status;
  call->state = READ_DONE;
  struct task_wait *wait = call->wait;
  call->wait = NULL;
  task_wait_wake(wait);
  unlock_reads();
  cpu_restore_interrupts(flags);
}

bool tcp_reads_service(void)
{
  net_worker_assert_context();
  bool worked = false;
  for (size_t i = 0; i < TCP_READ_LIMIT; ++i) {
    uint64_t flags = cpu_save_interrupts();
    lock_reads();
    struct tcp_read *call = &reads[i];
    enum read_state state = call->state;
    if (state == READ_CONSUMED) {
      call->state = READ_RECLAIMING;
    } else if (state == READ_QUEUED) {
      call->state = READ_ACTIVE;
    }
    unlock_reads();
    cpu_restore_interrupts(flags);

    if (state == READ_CONSUMED) {
      return_receive_credit(call->stream->connection, call->length);
      flags = cpu_save_interrupts();
      object_release(&call->stream->object);
      lock_reads();
      *call = (struct tcp_read){0};
      unlock_reads();
      cpu_restore_interrupts(flags);
      worked = true;
      continue;
    }
    if (state != READ_QUEUED && state != READ_ACTIVE) {
      continue;
    }
    struct tcp_connection *connection = call->stream->connection;
    enum call_status status = connection->terminal_status;
    if (status == CALL_OK && !call->nonblocking && task_deadline_expired(call->deadline)) {
      status = CALL_TIMED_OUT;
    }
    if (status == CALL_OK) {
      if (!connection->receive_length && !connection->peer_fin) {
        if (!call->nonblocking) {
          continue;
        }
        status = CALL_WOULD_BLOCK;
      } else {
        call->length = read_ordered(connection, call->data, call->capacity);
      }
    }
    complete_read(call, status);
    worked = true;
  }
  return worked;
}

bool tcp_reads_next_deadline(uint64_t *deadline)
{
  uint64_t flags = cpu_save_interrupts();
  lock_reads();
  uint64_t next = UINT64_MAX;
  bool found = false;
  for (size_t i = 0; i < TCP_READ_LIMIT; ++i) {
    struct tcp_read *call = &reads[i];
    if (call->state == READ_QUEUED || call->state == READ_CONSUMED) {
      next = 0;
      found = true;
      break;
    }
    if (call->state == READ_ACTIVE && call->deadline < next) {
      next = call->deadline;
      found = true;
    }
  }
  unlock_reads();
  cpu_restore_interrupts(flags);
  *deadline = next;
  return found;
}

bool tcp_read_available(struct tcp_stream *stream)
{
  uint64_t flags = cpu_save_interrupts();
  lock_reads();
  bool available = true;
  for (size_t i = 0; i < TCP_READ_LIMIT; ++i) {
    struct tcp_read *call = &reads[i];
    if (call->stream == stream && call->state != READ_FREE &&
        call->state != READ_CONSUMED && call->state != READ_RECLAIMING) {
      available = false;
      break;
    }
  }
  unlock_reads();
  cpu_restore_interrupts(flags);
  return available;
}
