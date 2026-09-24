#ifndef ABI_MESSAGE_H
#define ABI_MESSAGE_H

#include <stdint.h>

#define PROTOCOL_CONSOLE UINT64_C(1)
#define PROTOCOL_FILE UINT64_C(2)
#define PROTOCOL_ENDPOINT UINT64_C(3)
#define PROTOCOL_DIRECTORY UINT64_C(4)
#define PROTOCOL_MEMORY UINT64_C(5)
#define PROTOCOL_PROCESS UINT64_C(6)
#define PROTOCOL_LAUNCHER UINT64_C(7)
#define PROTOCOL_DISPLAY UINT64_C(8)
#define PROTOCOL_CLOCK UINT64_C(9)
#define PROTOCOL_KEYBOARD UINT64_C(10)
#define PROTOCOL_MOUNT UINT64_C(11)
#define PROTOCOL_ECHO UINT64_C(12)

/* The handle selects the object; this tag identifies the request protocol,
 * never the caller's authority. Operation numbers are local to that protocol. */
struct message_header {
  uint64_t protocol;
  uint64_t operation;
};

_Static_assert(sizeof(struct message_header) == 16, "message header layout");

#endif
