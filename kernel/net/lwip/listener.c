#include <arch/clock.h>
#include <kernel/net/ipv4.h>
#include <lwip/netif.h>
#include <caelum_hooks.h>
#include "connection.h"

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
  if (connection->pending_previous) {
    connection->pending_previous->pending_next = connection->pending_next;
  } else {
    listener->pending_head = connection->pending_next;
  }
  if (connection->pending_next) {
    connection->pending_next->pending_previous = connection->pending_previous;
  }
  connection->pending_next = connection->pending_previous = NULL;
  KASSERT(listener->pending_count);
  --listener->pending_count;
  connection->listener = NULL;
  connection->ready_next = NULL;
  tcp_connection_readiness_notify(listener);
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
  if (ip_current_input_netif() != listener->interface->tcp_projection ||
      listener->address_epoch != listener->interface->ipv4.address_epoch) {
    return ERR_IF;
  }
  uint64_t now = arch_monotonic_ns();
  if (listener->pending_count == TCP_LISTENER_PENDING_LIMIT ||
      now > UINT64_MAX - TCP_LISTENER_HANDSHAKE_NS) {
    return ERR_MEM;
  }
  connection->interface = listener->interface;
  connection->address_epoch = listener->address_epoch;
  connection->listener = listener;
  connection->pending_next = listener->pending_head;
  if (listener->pending_head) {
    listener->pending_head->pending_previous = connection;
  }
  listener->pending_head = connection;
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
  tcp_connection_established(pcb);
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
  tcp_connection_readiness_notify(listener);
  return ERR_OK;
}

enum net_result tcp_listener_prepare(const struct ipv4_route *route,
    uint32_t address, uint16_t port,
    struct tcp_connection **output)
{
  net_worker_assert_context();
  if (!address || !port) {
    return NET_INVALID;
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
  connection->interface = route->interface;
  connection->address_epoch = route->address_epoch;
  tcp_bind_netif(pcb, route->interface->tcp_projection);
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
  tcp_connection_discard_receive(connection);
  tcp_connection_release_receive_reservation(connection);
  connection->owned = true;
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
  tcp_connection_readiness_notify(listener);
}

void tcp_listener_inspect(struct tcp_connection *listener, struct tcp_listener_info *info)
{
  *info = (struct tcp_listener_info){
    .local_address = listener->local, .local_port = listener->local_port,
    .interface_id = listener->interface->id, .address_epoch = listener->address_epoch,
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
