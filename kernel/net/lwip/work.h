#ifndef CAELUM_TCP_WORK_H
#define CAELUM_TCP_WORK_H

#include <arch/clock.h>
#include <kernel/memory.h>
#include <stdatomic.h>

#define TCP_WORK_BATCH 16

struct tcp_work {
  struct tcp_work *previous, *next;
};

/* The caller holds its subsystem lock. Only the worker rotates/removes active
 * records; callers may enqueue new records and notify without walking a list. */
struct tcp_work_queue {
  struct tcp_work *head, *tail;
  size_t count, remaining;
  uint64_t deadline;
  atomic_bool dirty;
};

static inline void tcp_work_changed(struct tcp_work_queue *queue)
{
  atomic_store_explicit(&queue->dirty, true, memory_order_release);
}

static inline void tcp_work_append(struct tcp_work_queue *queue, struct tcp_work *work)
{
  work->previous = queue->tail;
  work->next = NULL;
  if (queue->tail) {
    queue->tail->next = work;
  } else {
    queue->head = work;
  }
  queue->tail = work;
  ++queue->count;
  tcp_work_changed(queue);
}

static inline void tcp_work_remove(struct tcp_work_queue *queue, struct tcp_work *work)
{
  if (work->previous) {
    work->previous->next = work->next;
  } else {
    queue->head = work->next;
  }
  if (work->next) {
    work->next->previous = work->previous;
  } else {
    queue->tail = work->previous;
  }
  work->previous = work->next = NULL;
  KASSERT(queue->count);
  --queue->count;
}

static inline void tcp_work_begin(struct tcp_work_queue *queue)
{
  if (!queue->count) {
    return;
  }
  if (atomic_exchange_explicit(&queue->dirty, false, memory_order_acq_rel) ||
      (!queue->remaining && queue->deadline != UINT64_MAX &&
       queue->deadline <= arch_monotonic_ns())) {
    queue->remaining = queue->count;
    queue->deadline = UINT64_MAX;
  }
}

static inline struct tcp_work *tcp_work_next(struct tcp_work_queue *queue)
{
  if (!queue->remaining) {
    return NULL;
  }
  KASSERT(queue->head && queue->remaining <= queue->count);
  --queue->remaining;
  struct tcp_work *work = queue->head;
  if (work != queue->tail) {
    queue->head = work->next;
    queue->head->previous = NULL;
    work->previous = queue->tail;
    work->next = NULL;
    queue->tail->next = work;
    queue->tail = work;
  }
  return work;
}

static inline void tcp_work_deadline(struct tcp_work_queue *queue, uint64_t deadline)
{
  if (deadline < queue->deadline) {
    queue->deadline = deadline;
  }
}

static inline bool tcp_work_next_deadline(struct tcp_work_queue *queue, uint64_t *deadline)
{
  if (!queue->count) {
    return false;
  }
  if (queue->remaining || atomic_load_explicit(&queue->dirty, memory_order_acquire)) {
    *deadline = 0;
    return true;
  }
  *deadline = queue->deadline;
  return queue->deadline != UINT64_MAX;
}

#endif
