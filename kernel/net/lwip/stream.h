#ifndef CAELUM_TCP_STREAM_H
#define CAELUM_TCP_STREAM_H

#include "connection.h"

struct tcp_stream {
  struct kernel_object object;
  struct tcp_stream *retired_next;
  struct tcp_connection *connection; /* Sole external transport owner. */
};

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
