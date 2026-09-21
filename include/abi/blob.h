#ifndef ABI_BLOB_H
#define ABI_BLOB_H

#include <stdint.h>

#define BLOB_READ UINT64_C(2)
#define BLOB_SIZE UINT64_C(3)

struct blob_read_request {
  uint64_t offset;
  uint64_t address;
  uint64_t capacity;
};

struct blob_read_reply {
  uint64_t read;
};

/* BLOB_SIZE has an empty request. */
struct blob_size_reply {
  uint64_t size;
};

_Static_assert(sizeof(struct blob_read_request) == 24, "blob read request layout");
_Static_assert(sizeof(struct blob_read_reply) == 8, "blob read reply layout");
_Static_assert(sizeof(struct blob_size_reply) == 8, "blob size reply layout");

#endif
