#ifndef ABI_FILE_H
#define ABI_FILE_H

#include <abi/message.h>
#include <stddef.h>

/* The initial file protocol supports read-only initrd backing. READ permits
 * both operations; no shared seek position or mutation operation is exposed. */
#define FILE_RIGHT_READ (UINT64_C(1) << 0)

#define FILE_READ UINT64_C(1)
#define FILE_SIZE UINT64_C(2)

struct file_read_request {
  uint64_t offset;
  uint64_t address;
  uint64_t capacity;
};

/* Send the complete structure for every operation. FILE_SIZE ignores body;
 * initialize messages to zero so unused payload bytes contain no stack data. */
struct file_message {
  struct message_header header;
  union {
    struct file_read_request read;
  } body;
};

struct file_read_reply {
  uint64_t read;
};

/* FILE_SIZE has no payload fields. */
struct file_size_reply {
  uint64_t size;
};

_Static_assert(sizeof(struct file_read_request) == 24, "file read request layout");
_Static_assert(sizeof(struct file_read_reply) == 8, "file read reply layout");
_Static_assert(sizeof(struct file_size_reply) == 8, "file size reply layout");
_Static_assert(offsetof(struct file_message, body) == 16, "file payload offset");
_Static_assert(sizeof(struct file_message) == 40, "file message layout");

#endif
