#ifndef ABI_CONSOLE_H
#define ABI_CONSOLE_H

#include <abi/message.h>
#include <stddef.h>

#define CONSOLE_RIGHT_WRITE (UINT64_C(1) << 0)

#define CONSOLE_WRITE UINT64_C(1)

struct console_write_request {
  uint64_t address;
  uint64_t length;
};

struct console_message {
  struct message_header header;
  union {
    struct console_write_request write;
  } body;
};

struct console_write_reply {
  uint64_t written;
};

_Static_assert(sizeof(struct console_write_request) == 16, "console request layout");
_Static_assert(sizeof(struct console_write_reply) == 8, "console reply layout");
_Static_assert(offsetof(struct console_message, body) == 16, "console payload offset");
_Static_assert(sizeof(struct console_message) == 32, "console message layout");

#endif
