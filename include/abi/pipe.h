#ifndef ABI_PIPE_H
#define ABI_PIPE_H

#include <abi/handle.h>
#include <abi/message.h>

#define PIPE_SERVICE_RIGHT_CREATE (UINT64_C(1) << 0)
#define PIPE_CREATE UINT64_C(1)

#define PIPE_RIGHT_READ (UINT64_C(1) << 0)
#define PIPE_RIGHT_WRITE (UINT64_C(1) << 1)
#define PIPE_READ UINT64_C(1)
#define PIPE_WRITE UINT64_C(2)

#define PIPE_CAPACITY 65536
#define PIPE_READ_MAX_BYTES 4096
#define PIPE_WRITE_MAX_BYTES 4096

/* CREATE uses only its message header and returns both handles or neither. */
struct pipe_create_reply {
  handle_t reader;
  handle_t writer;
};

struct pipe_read_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t capacity;
};

struct pipe_read_reply {
  uint64_t length;
};

struct pipe_write_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t length;
};

struct pipe_write_reply {
  uint64_t length;
};

#endif
