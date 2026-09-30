#include <arch/clock.h>
#include <kernel/net/ipv4.h>
#include <caelum_hooks.h>
#include "connection.h"

static size_t listener_count;

void tcp_listener_detach(struct tcp_connection *connection)
{
  struct tcp_connection *listener = connection->listener;
  if (!listener) {
    return;
  }
  struct tcp_connection **link = &listener->ready_head;
  struct tcp_connection *previous = NULL;
  while (*link && *link != connection) {
    previous = *link;
    link = &(*link)->ready_next;
  }
  if (*link) {
    *link = connection->ready_next;
    if (listener->ready_tail == connection) {
      listener->ready_tail = previous;
    }
  }
  KASSERT(listener->pending_count);
  --listener->pending_count;
  connection->listener = NULL;
  connection->ready_next = NULL;
}

err_t tcp_listener_passive_open(u8_t id, struct tcp_pcb_listen *pcb_listener,
    struct tcp_pcb *pcb)
{
  (void)id;
  net_worker_assert_context();
  struct tcp_connection *listener = tcp_connection_from_pcb((struct tcp_pcb *)pcb_listener);
  struct tcp_connection *connection = tcp_connection_from_pcb(pcb);
  /* Passive open overwrites the allocation hook's callback argument. Restore
   * the child's identity even on rejection: its error callback owns that record. */
  tcp_arg(pcb, connection);
  KASSERT(listener->listening && listener->owned);
  uint64_t now = arch_monotonic_ns();
  if (listener->pending_count == TCP_LISTENER_PENDING_LIMIT ||
      now > UINT64_MAX - TCP_LISTENER_HANDSHAKE_NS) {
    return ERR_MEM;
  }
  connection->listener = listener;
  ++listener->pending_count;
  connection->local = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&pcb->local_ip)));
  connection->remote = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&pcb->remote_ip)));
  connection->local_port = pcb->local_port;
  connection->remote_port = pcb->remote_port;
  connection->setup_deadline = now + TCP_LISTENER_HANDSHAKE_NS;
  return ERR_OK;
}

static err_t accept_connection(void *argument, struct tcp_pcb *pcb, err_t error)
{
  if (!pcb) {
    /* Allocation failure reports the listener's argument, not a child. */
    return ERR_MEM;
  }
  struct tcp_connection *connection = argument;
  KASSERT(error == ERR_OK && connection->pcb == pcb && connection->listener);
  if (arch_monotonic_ns() >= connection->setup_deadline) {
    tcp_connection_abort(connection, CALL_TIMED_OUT);
    return ERR_ABRT;
  }
  connection->connected = true;
  connection->setup_deadline = 0;
  /* lwIP released the SYN's backlog slot immediately before this callback.
   * Keep it occupied until native ACCEPT publishes the stream. */
  tcp_backlog_delayed(pcb);
  struct tcp_connection *listener = connection->listener;
  if (listener->ready_tail) {
    listener->ready_tail->ready_next = connection;
  } else {
    listener->ready_head = connection;
  }
  listener->ready_tail = connection;
  return ERR_OK;
}

enum net_result tcp_listener_prepare(uint32_t address, uint16_t port,
    struct tcp_connection **output)
{
  net_worker_assert_context();
  if (!address || !port || address != net_ipv4_address()) {
    return NET_INVALID;
  }
  if (listener_count == TCP_LISTENER_LIMIT) {
    return NET_QUEUE_FULL;
  }
  enum net_result admission = tcp_connection_allocation_status();
  if (admission != NET_OK) {
    return admission;
  }
  struct tcp_pcb *pcb = tcp_new();
  if (!pcb) {
    return NET_NO_MEMORY;
  }
  struct tcp_connection *connection = tcp_connection_from_pcb(pcb);
  ip_addr_t local = { .addr = lwip_htonl(address) };
  err_t result = tcp_bind(pcb, &local, port);
  if (result != ERR_OK) {
    tcp_connection_abort(connection, CALL_ENDPOINT_CLOSED);
    return result == ERR_USE ? NET_QUEUE_FULL : NET_INVALID;
  }
  struct tcp_pcb *listener = tcp_listen_with_backlog_and_err(pcb,
      TCP_LISTENER_PENDING_LIMIT, &result);
  if (!listener) {
    tcp_connection_abort(connection, CALL_NO_MEMORY);
    return NET_NO_MEMORY;
  }
  /* Listen conversion transfers extension ownership and frees the full PCB. */
  connection->pcb = listener;
  connection->local = address;
  connection->local_port = port;
  connection->listening = true;
  connection->owned = true;
  ++listener_count;
  tcp_accept(listener, accept_connection);
  *output = connection;
  return NET_OK;
}

void tcp_listener_close(struct tcp_connection *listener, enum call_status status)
{
  net_worker_assert_context();
  KASSERT(listener->listening);
  if (listener->terminal_status == CALL_OK) {
    listener->terminal_status = status;
  }
  tcp_connection_abort_pending(listener);
  if (listener->pcb) {
    KASSERT(tcp_close(listener->pcb) == ERR_OK);
    KASSERT(!listener->pcb);
  }
}

void tcp_listener_inspect(struct tcp_connection *listener, struct tcp_listener_info *info)
{
  *info = (struct tcp_listener_info){
    .local_address = listener->local, .local_port = listener->local_port,
    .pending = listener->pending_count, .capacity = TCP_LISTENER_PENDING_LIMIT,
    .terminal_status = listener->terminal_status,
  };
}

struct tcp_connection *tcp_listener_take(struct tcp_connection *listener)
{
  net_worker_assert_context();
  struct tcp_connection *connection = listener->ready_head;
  if (!connection) {
    return NULL;
  }
  KASSERT(connection->connected && connection->pcb && !connection->owned);
  tcp_backlog_accepted(connection->pcb);
  tcp_listener_detach(connection);
  connection->owned = true;
  return connection;
}

void tcp_listener_record_freed(void)
{
  KASSERT(listener_count);
  --listener_count;
}
