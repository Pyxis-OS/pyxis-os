#ifndef CAELUM_TCP_STREAM_H
#define CAELUM_TCP_STREAM_H

#include "connection.h"
#include <kernel/user/readiness_source.h>

struct tcp_stream {
  struct kernel_object object;
  struct readiness_source readiness;
  bool read_busy, write_busy, accept_busy; /* Protected by each direction lock. */
  size_t grants; /* Logical native grants, protected by control lock. */
  bool grants_closed;
  struct tcp_stream *closing_next;
  struct execution_group *close_group;
  struct tcp_stream *retired_next;
  struct execution_group *cleanup_group; /* Worker retirement's pending token. */
  struct tcp_connection *connection; /* Sole external transport owner. */
};

void tcp_reads_changed(void);
void tcp_writes_changed(void);
void tcp_controls_changed(void);
void tcp_calls_changed(void);

bool tcp_reads_service(void);
bool tcp_reads_next_deadline(uint64_t *deadline);

bool tcp_writes_service(void);
bool tcp_writes_next_deadline(uint64_t *deadline);

/* Worker-only readiness checks; direction ownership is sampled under its lock. */
bool tcp_read_available(struct tcp_stream *stream);
bool tcp_write_available(struct tcp_stream *stream);
bool tcp_accept_available(struct tcp_stream *stream);
bool tcp_readiness_service(void);
bool tcp_readiness_next_deadline(uint64_t *deadline);

#endif
