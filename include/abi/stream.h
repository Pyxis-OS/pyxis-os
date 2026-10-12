#ifndef ABI_STREAM_H
#define ABI_STREAM_H

#include <abi/endpoint.h>

#define STREAM_RIGHT_READ (UINT64_C(1) << 0)
#define STREAM_READ UINT64_C(1)

/* Exported forward-only byte stream. Copies share consumption. A nonzero READ
 * waits for data or a terminal result; successful zero means validated EOF.
 * Errors never mean EOF. A lost delivered reply may have consumed its bytes.
 * No offset, seek, SIZE, write or native readiness operation is implied. */
struct stream_read_request {
  uint64_t capacity;
};

struct stream_read_reply {
  uint64_t read;
};

#define STREAM_READ_MAX_BYTES (ENDPOINT_DATA_MAX - sizeof(struct stream_read_reply))

#endif
