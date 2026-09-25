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

#endif
