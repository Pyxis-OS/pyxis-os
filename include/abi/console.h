#ifndef ABI_CONSOLE_H
#define ABI_CONSOLE_H

#include <abi/message.h>
#include <stddef.h>

#define CONSOLE_RIGHT_WRITE (UINT64_C(1) << 0)
#define CONSOLE_RIGHT_READ (UINT64_C(1) << 1)
#define CONSOLE_RIGHTS (CONSOLE_RIGHT_WRITE | CONSOLE_RIGHT_READ)

#define CONSOLE_WRITE UINT64_C(1)
#define CONSOLE_READ UINT64_C(2)
#define CONSOLE_SIZE UINT64_C(3)

struct console_write_request {
  uint64_t address;
  uint64_t length;
};

/* READ blocks for available bytes, without echo, editing or EOF semantics.
 * Zero capacity succeeds immediately. INPUT_LOST acknowledges discarded input;
 * retry starts a fresh stream. Navigation sequences may span short reads. */
struct console_read_request {
  uint64_t address;
  uint64_t capacity;
};

union console_payload {
  struct console_write_request write;
  struct console_read_request read;
};

struct console_message {
  struct message_header header;
  union console_payload body;
};

struct console_write_reply {
  uint64_t written;
};

struct console_read_reply {
  uint64_t read;
};

/* Character cells, excluding session navigation. SIZE needs READ or WRITE;
 * its fixed-size payload is ignored. No resize notifications yet. */
struct console_size_reply {
  uint64_t columns;
  uint64_t rows;
};

_Static_assert(sizeof(struct console_read_request) == 16, "console read layout");
_Static_assert(sizeof(struct console_read_reply) == 8, "console read reply layout");
_Static_assert(sizeof(struct console_size_reply) == 16, "console size reply layout");
_Static_assert(sizeof(struct console_write_request) == 16, "console request layout");
_Static_assert(sizeof(struct console_write_reply) == 8, "console reply layout");
_Static_assert(offsetof(struct console_message, body) == 16, "console payload offset");
_Static_assert(sizeof(struct console_message) == 32, "console message layout");

#endif
