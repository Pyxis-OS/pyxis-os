#ifndef CAELUM_TCP_CONNECTION_H
#define CAELUM_TCP_CONNECTION_H

#include <kernel/net/tcp.h>
#include <lwip/tcp.h>

/* Worker-owned. The error callback may run before or after PCB destruction.
 * Keep the record until callbacks unwind AND its owner releases it. */
struct tcp_connection {
  struct tcp_connection *next;
  struct tcp_pcb *pcb;
  uint64_t generation;
  uint32_t local, remote;
  uint16_t local_port, remote_port;
  uint64_t setup_deadline, progress_deadline, orphan_deadline;
  uint32_t last_ack;
  err_t error;
  bool owned, peer_fin, connected;
  bool write_shutdown; /* Committed even while lwIP defers FIN allocation. */
  enum call_status terminal_status;
  uint8_t *receive_data;
  size_t receive_head, receive_length;
  bool listening;
  struct tcp_connection *listener; /* Internal owner until ACCEPT or failure. */
  struct tcp_connection *ready_next;
  struct tcp_connection *ready_head, *ready_tail;
  size_t pending_count;
};

void tcp_connections_init(void);
void tcp_connections_service(void);
bool tcp_connections_next_deadline(uint64_t *deadline);
uint64_t tcp_connection_generation(const struct tcp_pcb *pcb);
struct tcp_connection *tcp_connection_from_pcb(const struct tcp_pcb *pcb);
enum net_result tcp_connection_allocation_status(void);
void tcp_connection_abort_pending(struct tcp_connection *listener);

enum net_result tcp_listener_prepare(uint32_t address, uint16_t port,
    struct tcp_connection **output);
void tcp_listener_close(struct tcp_connection *listener, enum call_status status);
void tcp_listener_inspect(struct tcp_connection *listener, struct tcp_listener_info *info);
struct tcp_connection *tcp_listener_take(struct tcp_connection *listener);
void tcp_listener_detach(struct tcp_connection *connection);
void tcp_listener_record_freed(void);
err_t tcp_listener_passive_open(u8_t id, struct tcp_pcb_listen *listener,
    struct tcp_pcb *pcb);

/* Worker-only. Receive may abort on allocation failure; its callback must then
 * return ERR_ABRT. Starting a connection does not publish a handle. */
void tcp_connection_start(struct tcp_connection *connection);
void tcp_connection_abort(struct tcp_connection *connection, enum call_status status);
enum call_status tcp_connection_shutdown_write(struct tcp_connection *connection);
void tcp_connection_inspect(struct tcp_connection *connection, struct tcp_connection_info *info);

/* Receive callback and payload cleanup share the worker-owned record. */
void tcp_connection_discard_receive(struct tcp_connection *connection);
err_t tcp_connection_receive(void *argument, struct tcp_pcb *pcb, struct pbuf *buffer, err_t error);

#endif
