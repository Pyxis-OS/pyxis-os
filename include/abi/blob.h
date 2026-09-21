#ifndef ABI_BLOB_H
#define ABI_BLOB_H

#include <abi/message.h>
#include <stddef.h>

#define BLOB_RIGHT_READ (UINT64_C(1) << 0)

#define BLOB_READ UINT64_C(1)
#define BLOB_SIZE UINT64_C(2)

struct blob_read_request {
  uint64_t offset;
  uint64_t address;
  uint64_t capacity;
};

/* Send the complete structure for every operation. BLOB_SIZE ignores body;
 * initialize messages to zero so unused payload bytes contain no stack data. */
struct blob_message {
  struct message_header header;
  union {
    struct blob_read_request read;
  } body;
};

struct blob_read_reply {
  uint64_t read;
};

/* BLOB_SIZE has no payload fields. */
struct blob_size_reply {
  uint64_t size;
};

_Static_assert(sizeof(struct blob_read_request) == 24, "blob read request layout");
_Static_assert(sizeof(struct blob_read_reply) == 8, "blob read reply layout");
_Static_assert(sizeof(struct blob_size_reply) == 8, "blob size reply layout");
_Static_assert(offsetof(struct blob_message, body) == 16, "blob payload offset");
_Static_assert(sizeof(struct blob_message) == 40, "blob message layout");

#endif
