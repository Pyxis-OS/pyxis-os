#ifndef ABI_FILE_H
#define ABI_FILE_H

#include <abi/message.h>
#include <stddef.h>

/* Operations use explicit offsets. SIZE accepts either READ or WRITE; WRITE
 * permits both mutation operations, but never changes read-only backing. */
#define FILE_RIGHT_READ (UINT64_C(1) << 0)
#define FILE_RIGHT_WRITE (UINT64_C(1) << 1)
#define FILE_RIGHTS (FILE_RIGHT_READ | FILE_RIGHT_WRITE)

#define FILE_READ UINT64_C(1)
#define FILE_SIZE UINT64_C(2)
#define FILE_WRITE UINT64_C(3)
#define FILE_RESIZE UINT64_C(4)

struct file_read_request {
  uint64_t offset;
  uint64_t address;
  uint64_t capacity;
};

struct file_write_request {
  uint64_t offset;
  uint64_t address;
  uint64_t size;
};

struct file_resize_request {
  uint64_t size;
};

union file_payload {
  struct file_read_request read;
  struct file_write_request write;
  struct file_resize_request resize;
};

/* Send the complete structure for every operation. FILE_SIZE ignores body;
 * initialize messages to zero so unused payload bytes contain no stack data. */
struct file_message {
  struct message_header header;
  union file_payload body;
};

struct file_read_reply {
  uint64_t read;
};

/* WRITE is all-or-nothing, including writes beyond EOF. Gaps and ranges made
 * visible by RESIZE read as zero; shrinking permanently discards the tail.
 * A zero-byte write does not extend the file. RESIZE returns no reply bytes. */
struct file_write_reply {
  uint64_t written;
};

/* FILE_SIZE has no payload fields. */
struct file_size_reply {
  uint64_t size;
};

_Static_assert(sizeof(struct file_write_request) == 24, "file write request layout");
_Static_assert(sizeof(struct file_write_reply) == 8, "file write reply layout");
_Static_assert(sizeof(struct file_resize_request) == 8, "file resize request layout");
_Static_assert(sizeof(struct file_read_request) == 24, "file read request layout");
_Static_assert(sizeof(struct file_read_reply) == 8, "file read reply layout");
_Static_assert(sizeof(struct file_size_reply) == 8, "file size reply layout");
_Static_assert(offsetof(struct file_message, body) == 16, "file payload offset");
_Static_assert(sizeof(struct file_message) == 40, "file message layout");

#endif
