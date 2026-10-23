#include <arch/cpu.h>
#include <arch/smp.h>
#include <kernel/user/readiness.h>
#include <kernel/user/readiness_set.h>
#include "stream.h"

/* Incoming is shared by the two BSP workers under IF=0. Only the network
 * worker owns active and observes transport state, so registration and state
 * recheck cannot race a packet/callback. The notifier covers idle handoff. */
static struct bsp_request *incoming_head, *incoming_tail;
static struct bsp_request *active;

struct readiness_source *tcp_readiness_source(struct kernel_object *object)
{
  if (object->type != OBJECT_TCP && object->type != OBJECT_TCP_LISTENER) {
    return NULL;
  }
  return &((struct tcp_stream *)object)->readiness;
}

enum call_status tcp_readiness_status(struct kernel_object *object)
{
  net_worker_assert_context();
  struct tcp_connection *connection = ((struct tcp_stream *)object)->connection;
  if (connection) {
    tcp_connection_refresh(connection);
  }
  return connection ? connection->terminal_status : CALL_ENDPOINT_CLOSED;
}

void tcp_connection_readiness_notify(struct tcp_connection *connection)
{
  net_worker_assert_context();
  tcp_calls_changed();
  tcp_connections_changed();
  if (connection->stream) {
    uint64_t flags = cpu_save_interrupts();
    readiness_source_notify(&connection->stream->readiness);
    cpu_restore_interrupts(flags);
  }
}

void net_readiness_submit(struct readiness_request *request)
{
  KASSERT(arch_cpu_index() == 0);
  KASSERT(!(cpu_save_interrupts() & RFLAGS_INTERRUPT_ENABLE));
  KASSERT(request->request.state == BSP_REQUEST_FORWARDED && !request->request.next);
  if (!net_worker_available()) {
    readiness_complete(request, CALL_UNAVAILABLE);
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

uint64_t tcp_readiness_events(struct readiness_interest *interest)
{
  net_worker_assert_context();
  struct tcp_stream *stream = (struct tcp_stream *)interest->object;
  struct tcp_connection *connection = stream->connection;
  tcp_connection_refresh(connection);
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

  bool worked = readiness_sets_service(true);
  return readiness_service(&active) || worked;
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
  uint64_t next;
  bool found = readiness_next_deadline(active, &next);
  uint64_t persistent;
  if (readiness_sets_deadline(true, &persistent) && (!found || persistent < next)) {
    next = persistent;
    found = true;
  }
  if (found) {
    *deadline = next;
  }
  return found;
}

void tcp_calls_changed(void)
{
  tcp_controls_changed();
  tcp_reads_changed();
  tcp_writes_changed();
}
