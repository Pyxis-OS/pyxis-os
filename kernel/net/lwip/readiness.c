#include <arch/clock.h>
#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/task.h>
#include <kernel/user/wait.h>
#include "stream.h"

/* Incoming is shared by the two BSP workers under IF=0. Only the network
 * worker owns active and observes transport state, so registration and state
 * recheck cannot race a packet/callback. The notifier covers idle handoff. */
static struct bsp_request *incoming_head, *incoming_tail;
static struct bsp_request *active;

static void complete_wait(struct readiness_request *request, enum call_status status)
{
  uint64_t flags = cpu_save_interrupts();
  for (size_t i = 0; i < request->count; ++i) {
    object_release(request->interests[i].object);
    request->interests[i].object = NULL;
  }
  request->status = status;
  bsp_request_complete(&request->request);
  cpu_restore_interrupts(flags);
}

void net_readiness_submit(struct readiness_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED && !request->request.next);
  if (!net_worker_available()) {
    complete_wait(request, CALL_UNAVAILABLE);
    return;
  }
  if (incoming_tail) {
    incoming_tail->next = &request->request;
  } else {
    incoming_head = &request->request;
  }
  incoming_tail = &request->request;
  net_worker_notify();
}

static uint64_t ready_events(struct readiness_interest *interest)
{
  struct tcp_stream *stream = (struct tcp_stream *)interest->object;
  struct tcp_connection *connection = stream->connection;
  uint64_t events = interest->events;
  uint64_t ready = connection->terminal_status != CALL_OK ? WAIT_ERROR : 0;
  if (stream->object.type == OBJECT_TCP_LISTENER) {
    if (!connection->pcb) {
      ready |= WAIT_CLOSED;
    } else if ((events & WAIT_ACCEPTABLE) && connection->ready_head &&
        tcp_accept_available(stream)) {
      ready |= WAIT_ACCEPTABLE;
    }
    return ready;
  }

  if ((events & (WAIT_READABLE | WAIT_PEER_FIN)) && connection->peer_fin) {
    ready |= WAIT_PEER_FIN;
  }
  if ((events & (WAIT_WRITABLE | WAIT_WRITE_CLOSED)) && connection->write_shutdown) {
    ready |= WAIT_WRITE_CLOSED;
  }
  if (connection->terminal_status != CALL_OK) {
    return ready;
  }
  if ((events & WAIT_READABLE) && connection->receive_length && tcp_read_available(stream)) {
    ready |= WAIT_READABLE;
  }
  if ((events & WAIT_WRITABLE) && !connection->write_shutdown && connection->pcb &&
      tcp_sndbuf(connection->pcb) && tcp_sndqueuelen(connection->pcb) < TCP_SND_QUEUELEN &&
      tcp_write_available(stream)) {
    ready |= WAIT_WRITABLE;
  }
  return ready;
}

bool tcp_readiness_service(void)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  if (incoming_head) {
    incoming_tail->next = active;
    active = incoming_head;
    incoming_head = incoming_tail = NULL;
  }
  cpu_restore_interrupts(flags);

  bool worked = false;
  struct bsp_request **link = &active;
  while (*link) {
    struct readiness_request *request = (struct readiness_request *)*link;
    bool ready = false;
    for (size_t i = 0; i < request->count; ++i) {
      request->interests[i].ready = ready_events(&request->interests[i]);
      ready |= request->interests[i].ready != 0;
    }
    /* Current readiness wins over an expired deadline, including after worker
     * queueing delay. Polling is a successful empty observation, not timeout. */
    bool expired = request->deadline && arch_monotonic_ns() >= request->deadline;
    if (ready || !request->deadline || expired) {
      *link = request->request.next;
      request->request.next = NULL;
      complete_wait(request, ready || !request->deadline ? CALL_OK : CALL_TIMED_OUT);
      worked = true;
    } else {
      link = &request->request.next;
    }
  }
  return worked;
}

bool tcp_readiness_next_deadline(uint64_t *deadline)
{
  net_worker_assert_context();
  uint64_t flags = cpu_save_interrupts();
  bool incoming = incoming_head != NULL;
  cpu_restore_interrupts(flags);
  if (incoming) {
    *deadline = 0;
    return true;
  }
  uint64_t next = UINT64_MAX;
  bool found = false;
  for (struct bsp_request *entry = active; entry; entry = entry->next) {
    struct readiness_request *request = (struct readiness_request *)entry;
    if (!found || request->deadline < next) {
      next = request->deadline;
      found = true;
    }
  }
  *deadline = next;
  return found;
}
