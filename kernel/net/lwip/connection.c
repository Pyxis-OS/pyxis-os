#include <arch/clock.h>
#include <kernel/net/ipv4.h>
#include <kernel/task.h>
#include <caelum_hooks.h>
#include "connection.h"
#include "identity.h"

#define TCP_PROGRESS_TIMEOUT_MS 120000
#define TCP_ORPHAN_TIMEOUT_MS 60000

static struct tcp_connection *connections;
static size_t connection_count;
static uint64_t next_generation;
static u8_t connection_arg;

static enum call_status connection_status(struct tcp_connection *connection, err_t error)
{
  switch (error) {
  case ERR_OK: return CALL_OK;
  case ERR_RST: return connection->connected ? CALL_CONNECTION_RESET : CALL_CONNECTION_REFUSED;
  case ERR_TIMEOUT:
  case ERR_ABRT: return CALL_TIMED_OUT; /* lwIP retry exhaustion, absent a local reason. */
  case ERR_RTE: return CALL_NO_ROUTE;
  case ERR_IF: return CALL_UNAVAILABLE;
  case ERR_MEM:
  case ERR_BUF: return CALL_NO_MEMORY;
  case ERR_CLSD: return CALL_ENDPOINT_CLOSED;
  default: return CALL_IO;
  }
}

static void connection_error(void *argument, err_t error)
{
  struct tcp_connection *connection = argument;
  /* lwIP reports normal LAST_ACK completion as ERR_CLSD after TX-only
   * shutdown. Preserve unread bytes and clean EOF after its PCB is gone. */
  if (error == ERR_CLSD && connection->write_shutdown && connection->peer_fin) {
    return;
  }
  tcp_connection_discard_receive(connection);
  /* Reset reports the error before destruction; abort reports it afterward.
   * Latch only the result here, without inspecting or releasing the PCB. */
  if (connection->error == ERR_OK) {
    connection->error = error;
    connection->terminal_status = connection_status(connection, error);
  }
}

static void destroy_pcb(u8_t id, void *data)
{
  net_worker_assert_context();
  KASSERT(id == connection_arg);
  struct tcp_connection *connection = data;
  net_ipv4_cancel_tcp(connection->generation);
  tcp_listener_detach(connection);
  connection->pcb = NULL;
  /* tcp_err may still be called with this record after this hook returns.
   * Reclamation happens only after returning to the worker, never here. */
}

static const struct tcp_ext_arg_callbacks connection_callbacks = {
  .destroy = destroy_pcb,
  .passive_open = tcp_listener_passive_open,
};

void tcp_connections_init(void)
{
  net_worker_assert_context();
  connection_arg = tcp_ext_arg_alloc_id();
  KASSERT(connection_arg != LWIP_TCP_PCB_NUM_EXT_ARG_ID_INVALID);
}

err_t caelum_lwip_pcb_allocated(struct tcp_pcb *pcb)
{
  net_worker_assert_context();
  if (!tcp_identity_ready() || connection_count == NET_TCP_CONNECTION_LIMIT ||
      next_generation == UINT64_MAX) {
    return ERR_MEM;
  }
  struct tcp_connection *connection = caelum_lwip_calloc(1, sizeof(*connection));
  if (!connection) {
    return ERR_MEM;
  }
  connection->pcb = pcb;
  connection->generation = ++next_generation;
  connection->next = connections;
  connections = connection;
  ++connection_count;
  tcp_ext_arg_set(pcb, connection_arg, connection);
  tcp_ext_arg_set_callbacks(pcb, connection_arg, &connection_callbacks);
  tcp_arg(pcb, connection);
  tcp_err(pcb, connection_error);
  tcp_recv(pcb, tcp_connection_receive);
  return ERR_OK;
}

enum net_result tcp_connection_allocation_status(void)
{
  net_worker_assert_context();
  if (!tcp_identity_ready()) {
    return NET_UNAVAILABLE;
  }
  if (connection_count == NET_TCP_CONNECTION_LIMIT) {
    return NET_QUEUE_FULL;
  }
  return NET_OK;
}

struct tcp_connection *tcp_connection_from_pcb(const struct tcp_pcb *pcb)
{
  struct tcp_connection *connection = tcp_ext_arg_get(pcb, connection_arg);
  KASSERT(connection && connection->pcb == pcb);
  return connection;
}

uint64_t tcp_connection_generation(const struct tcp_pcb *pcb)
{
  return tcp_connection_from_pcb(pcb)->generation;
}

static void abort_connection(struct tcp_connection *connection, err_t reason)
{
  tcp_connection_discard_receive(connection);
  if (connection->error == ERR_OK) {
    connection->error = reason;
    connection->terminal_status = connection_status(connection, reason);
  }
  /* Explicit abort discards application state, but must not permit early
   * tuple reuse after the transport has already entered TIME_WAIT. */
  if (connection->pcb && connection->pcb->state != TIME_WAIT) {
    tcp_abort(connection->pcb);
    KASSERT(!connection->pcb);
  }
}

void tcp_connection_abort(struct tcp_connection *connection, enum call_status status)
{
  net_worker_assert_context();
  if (connection->terminal_status == CALL_OK) {
    connection->error = ERR_ABRT;
    connection->terminal_status = status;
  }
  abort_connection(connection, ERR_ABRT);
}

/* lwIP fixes the send MSS from the peer's option and the interface MTU only
 * when the connection is established, just before the connect or accept
 * callback, so the clamp runs there rather than at PCB allocation. Nothing
 * has been sent yet. The initial window lwIP computed for TCP_MSS stays: its
 * 4380 bytes are within RFC 6928's initial window for TCP_ROUTED_MSS.
 *
 * Nagle is off: native writes already hand lwIP up to 4 KiB at once, and a
 * request/response frame of one full segment plus a tail would otherwise wait
 * for the peer's delayed ACK of that lone segment. */
void tcp_connection_established(struct tcp_pcb *pcb)
{
  net_worker_assert_context();
  uint32_t local = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&pcb->local_ip)));
  uint32_t remote = lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&pcb->remote_ip)));
  struct ipv4_route route;
  bool on_link = net_ipv4_route(local, remote, &route) == NET_OK &&
      (!route.next_hop || route.next_hop == remote);
  if (!on_link && pcb->mss > TCP_ROUTED_MSS) {
    pcb->mss = TCP_ROUTED_MSS;
  }
  tcp_nagle_disable(pcb);
}

static err_t connected(void *argument, struct tcp_pcb *pcb, err_t error)
{
  KASSERT(error == ERR_OK);
  struct tcp_connection *connection = argument;
  tcp_connection_established(pcb);
  connection->connected = true;
  /* Publication still checks the original deadline outside the callback. */
  return ERR_OK;
}

void tcp_connection_start(struct tcp_connection *connection)
{
  net_worker_assert_context();
  KASSERT(connection->pcb && connection->owned && !connection->connected);
  ip_addr_t remote = { .addr = lwip_htonl(connection->remote) };
  err_t result = tcp_connect(connection->pcb, &remote, connection->remote_port, connected);
  if (result != ERR_OK) {
    abort_connection(connection, result);
  }
}

enum call_status tcp_connection_shutdown_write(struct tcp_connection *connection)
{
  net_worker_assert_context();
  if (connection->write_shutdown) {
    return CALL_OK;
  }
  if (connection->terminal_status != CALL_OK) {
    return connection->terminal_status;
  }
  KASSERT(connection->pcb && connection->connected);
  err_t error = tcp_shutdown(connection->pcb, 0, 1);
  if (error != ERR_OK) {
    return error == ERR_CONN ? CALL_ENDPOINT_CLOSED : connection_status(connection, error);
  }
  /* ERR_OK also covers TF_CLOSEPEND: the timer will retry FIN allocation.
   * Do not use TF_FIN as the authority to accept new application writes. */
  connection->write_shutdown = true;
  return CALL_OK;
}

void tcp_connection_inspect(struct tcp_connection *connection, struct tcp_connection_info *info)
{
  net_worker_assert_context();
  *info = (struct tcp_connection_info){
    .local_address = connection->local, .remote_address = connection->remote,
    .local_port = connection->local_port, .remote_port = connection->remote_port,
    .state = !connection->pcb || connection->pcb->state == TIME_WAIT ? TCP_STATE_CLOSED :
        connection->peer_fin ? TCP_STATE_PEER_CLOSED : TCP_STATE_CONNECTED,
    .terminal_status = connection->terminal_status,
    .flags = (connection->write_shutdown ? TCP_INFO_WRITE_SHUTDOWN : 0) |
        (connection->peer_fin ? TCP_INFO_PEER_FIN : 0),
  };
}

enum net_result net_tcp_prepare(uint32_t destination, uint16_t port, uint64_t deadline,
    struct tcp_connection **output)
{
  net_worker_assert_context();
  KASSERT(output);
  uint64_t now = arch_monotonic_ns();
  if (deadline <= now) {
    return NET_TIMED_OUT;
  }
  if (!port || deadline - now > NET_TCP_MAX_WAIT_NS) {
    return NET_INVALID;
  }
  enum net_result admission = tcp_connection_allocation_status();
  if (admission != NET_OK) {
    return admission;
  }
  struct ipv4_route route;
  enum net_result result = net_ipv4_route(0, destination, &route);
  if (result != NET_OK) {
    return result;
  }
  uint16_t candidate, stride;
  if (!tcp_identity_ports(route.source, destination, port, &candidate, &stride)) {
    return NET_UNAVAILABLE;
  }
  struct tcp_pcb *pcb = tcp_new();
  if (!pcb) {
    return NET_NO_MEMORY;
  }
  struct tcp_connection *connection = tcp_ext_arg_get(pcb, connection_arg);
  connection->local = route.source;
  connection->remote = destination;
  connection->remote_port = port;
  connection->setup_deadline = deadline;
  ip_addr_t local = { .addr = lwip_htonl(route.source) };
  err_t bound = ERR_USE;
  /* No reuse option: lwIP's bind checks include TIME_WAIT. The odd stride
   * cannot repeat in the ephemeral range; at most 32 records own ports. */
  for (size_t attempt = 0; attempt <= NET_TCP_CONNECTION_LIMIT; ++attempt) {
    bound = tcp_bind(pcb, &local, TCP_EPHEMERAL_FIRST + candidate);
    if (bound != ERR_USE) {
      break;
    }
    candidate = (candidate + stride) & (TCP_EPHEMERAL_COUNT - 1);
  }
  if (bound != ERR_OK || task_deadline_expired(deadline)) {
    abort_connection(connection, bound == ERR_OK ? ERR_TIMEOUT : bound);
    return bound == ERR_OK ? NET_TIMED_OUT : NET_QUEUE_FULL;
  }
  connection->local_port = pcb->local_port;
  connection->owned = true;
  *output = connection;
  return NET_OK;
}

void net_tcp_release(struct tcp_connection *connection)
{
  net_worker_assert_context();
  KASSERT(connection && connection->owned);
  connection->owned = false;
  if (connection->listening) {
    tcp_listener_close(connection, CALL_ENDPOINT_CLOSED);
    return;
  }
  struct tcp_pcb *pcb = connection->pcb;
  bool unread = connection->receive_length || (pcb && (pcb->refused_data || pcb->ooseq));
  /* Read slots retain the object until collected and credited, so final release
   * cannot race a completed read whose bytes are still owed to its caller. */
  tcp_connection_discard_receive(connection);
  if (!pcb) {
    return;
  }
  if (!connection->write_shutdown || unread) {
    tcp_connection_abort(connection, CALL_ENDPOINT_CLOSED);
    return;
  }
  if (pcb->state == TIME_WAIT) {
    return;
  }
  connection->orphan_deadline = task_deadline_after_ms(TCP_ORPHAN_TIMEOUT_MS);
  /* Mark RX closed, so new unread data aborts instead of silently disappearing.
   * tcp_close also preserves/retries a previously deferred FIN. */
  if (tcp_close(pcb) != ERR_OK) {
    abort_connection(connection, ERR_ABRT);
  }
}

static void service_connection(struct tcp_connection *connection, uint64_t now)
{
  struct tcp_pcb *pcb = connection->pcb;
  if (!pcb) {
    return;
  }
  if (connection->listening) {
    /* Never let lwIP retarget an exact-address listener on address changes.
     * LISTEN is a smaller PCB and has no stream routing or progress fields. */
    if (connection->local != net_ipv4_address()) {
      tcp_listener_close(connection, CALL_UNAVAILABLE);
    }
    return;
  }
  if (pcb->state == TIME_WAIT) {
    /* TIME_WAIT keeps its tuple and admission slot even after an address/link
     * change. Only lwIP's 2*MSL expiry retires it; never the orphan deadline. */
    connection->setup_deadline = 0;
    connection->progress_deadline = 0;
    connection->orphan_deadline = 0;
    return;
  }
  if (!connection->owned && !connection->listener && !connection->orphan_deadline) {
    abort_connection(connection, ERR_ABRT);
    return;
  }
  if (!net_ipv4_is_loopback(connection->local) && connection->local != net_ipv4_address()) {
    abort_connection(connection, ERR_IF);
    return;
  }
  struct ipv4_route route;
  enum net_result routed = net_ipv4_route(connection->local, connection->remote, &route);
  if (routed != NET_OK) {
    abort_connection(connection, routed == NET_NO_ROUTE ? ERR_RTE : ERR_IF);
    return;
  }
  if (connection->setup_deadline && now >= connection->setup_deadline) {
    abort_connection(connection, ERR_TIMEOUT);
    return;
  }
  if (pcb->state != CLOSED && pcb->state != SYN_SENT && pcb->state != SYN_RCVD) {
    connection->setup_deadline = 0;
  }
  if (pcb->unacked || pcb->unsent || (pcb->flags & TF_CLOSEPEND)) {
    if (!connection->progress_deadline || pcb->lastack != connection->last_ack) {
      connection->last_ack = pcb->lastack;
      connection->progress_deadline = task_deadline_after_ms(TCP_PROGRESS_TIMEOUT_MS);
    }
  } else {
    connection->progress_deadline = 0;
  }
  if ((connection->progress_deadline && now >= connection->progress_deadline) ||
      (connection->orphan_deadline && now >= connection->orphan_deadline)) {
    abort_connection(connection, ERR_TIMEOUT);
  }
}

void tcp_connections_service(void)
{
  net_worker_assert_context();
  uint64_t now = arch_monotonic_ns();
  struct tcp_connection **link = &connections;
  while (*link) {
    struct tcp_connection *connection = *link;
    service_connection(connection, now);
    if (!connection->pcb && !connection->owned) {
      *link = connection->next;
      --connection_count;
      if (connection->listening) {
        tcp_listener_record_freed();
      }
      caelum_lwip_free(connection);
    } else {
      link = &connection->next;
    }
  }
}

void tcp_connection_abort_pending(struct tcp_connection *listener)
{
  net_worker_assert_context();
  for (struct tcp_connection *connection = connections; connection; connection = connection->next) {
    if (connection->listener == listener) {
      tcp_connection_abort(connection, CALL_ENDPOINT_CLOSED);
    }
  }
  KASSERT(!listener->pending_count && !listener->ready_head && !listener->ready_tail);
}

bool tcp_connections_next_deadline(uint64_t *deadline)
{
  net_worker_assert_context();
  uint64_t next = UINT64_MAX;
  bool found = false;
  for (struct tcp_connection *connection = connections; connection; connection = connection->next) {
    if (!connection->pcb) {
      if (!connection->owned) {
        *deadline = 0; /* Reap after the callback stack has unwound. */
        return true;
      }
      continue;
    }
    if (connection->pcb->state == TIME_WAIT) {
      continue;
    }
    uint64_t deadlines[] = { connection->setup_deadline,
      connection->progress_deadline, connection->orphan_deadline };
    for (size_t i = 0; i < sizeof(deadlines) / sizeof(deadlines[0]); ++i) {
      if (deadlines[i] && deadlines[i] < next) {
        next = deadlines[i];
        found = true;
      }
    }
  }
  *deadline = next;
  return found;
}
