#ifndef ABI_ENDPOINT_H
#define ABI_ENDPOINT_H

#include <abi/message.h>
#include <stddef.h>

#define ENDPOINT_RIGHT_CALL (UINT64_C(1) << 0)
#define ENDPOINT_RIGHT_RECEIVE (UINT64_C(1) << 1)
#define ENDPOINT_RIGHT_REPLY (UINT64_C(1) << 2)

#define ENDPOINT_CALL UINT64_C(1)
#define ENDPOINT_RECEIVE UINT64_C(2)
#define ENDPOINT_REPLY UINT64_C(3)
#define ENDPOINT_DATA_MAX 64

/* Opaque application bytes. IDs identify one delivered request on this
 * endpoint, not a process or a transferable authority. Zero is never an ID. */
struct endpoint_packet {
  uint64_t id;
  uint64_t size;
  uint8_t data[ENDPOINT_DATA_MAX];
};

union endpoint_payload {
  struct {
    uint64_t size;
    uint8_t data[ENDPOINT_DATA_MAX];
  } call;
  struct endpoint_packet reply;
};

/* RECEIVE ignores body. Send the complete zero-initialized message for every
 * operation. CALL and RECEIVE return a packet; REPLY returns no reply bytes. */
struct endpoint_message {
  struct message_header header;
  union endpoint_payload body;
};

_Static_assert(sizeof(struct endpoint_packet) == 80, "endpoint packet layout");
_Static_assert(sizeof(union endpoint_payload) == 80, "endpoint payload layout");
_Static_assert(offsetof(struct endpoint_message, body) == 16, "endpoint payload offset");
_Static_assert(sizeof(struct endpoint_message) == 96, "endpoint message layout");

#endif
