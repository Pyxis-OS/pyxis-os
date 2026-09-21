#ifndef ABI_CONSOLE_H
#define ABI_CONSOLE_H

#include <stdint.h>

#define CONSOLE_RIGHT_WRITE (UINT64_C(1) << 0)

#define CONSOLE_WRITE UINT64_C(1)

struct console_write_request {
  uint64_t address;
  uint64_t length;
};

struct console_write_reply {
  uint64_t written;
};

_Static_assert(sizeof(struct console_write_request) == 16, "console request layout");
_Static_assert(sizeof(struct console_write_reply) == 8, "console reply layout");

#endif
