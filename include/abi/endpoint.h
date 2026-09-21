#ifndef ABI_ENDPOINT_H
#define ABI_ENDPOINT_H

#include <abi/message.h>
#include <abi/handle.h>
#include <stddef.h>

#define ENDPOINT_RIGHT_CALL (UINT64_C(1) << 0)
#define ENDPOINT_RIGHT_RECEIVE (UINT64_C(1) << 1)
#define ENDPOINT_RIGHT_REPLY (UINT64_C(1) << 2)

#define ENDPOINT_CALL UINT64_C(1)
#define ENDPOINT_RECEIVE UINT64_C(2)
#define ENDPOINT_REPLY UINT64_C(3)
#define ENDPOINT_DATA_MAX 64

/* CALL copies this source grant with equal or reduced rights. A zero handle
 * with zero rights means no grant. RECEIVE reports a new recipient-local
 * handle; the sender keeps its original. REPLY cannot attach capabilities. */
struct endpoint_grant {
  handle_t handle;
  uint64_t rights;
};

/* IDs correlate delivered requests, never authority. Zero is not an ID.
 * Only RECEIVE returns a grant; CALL's reply always has an absent grant. */
struct endpoint_packet {
  uint64_t id;
  uint64_t size;
  struct endpoint_grant grant;
  uint8_t data[ENDPOINT_DATA_MAX];
};

union endpoint_payload {
  struct {
    uint64_t size;
    struct endpoint_grant grant;
    uint8_t data[ENDPOINT_DATA_MAX];
  } call;
  struct {
    uint64_t id;
    uint64_t size;
    uint8_t data[ENDPOINT_DATA_MAX];
  } reply;
};

/* RECEIVE ignores body. Send the complete zero-initialized message for every
 * operation. CALL and RECEIVE return a packet; REPLY returns no reply bytes. */
struct endpoint_message {
  struct message_header header;
  union endpoint_payload body;
};

_Static_assert(sizeof(struct endpoint_packet) == 96, "endpoint packet layout");
_Static_assert(sizeof(union endpoint_payload) == 88, "endpoint payload layout");
_Static_assert(offsetof(struct endpoint_message, body) == 16, "endpoint payload offset");
_Static_assert(sizeof(struct endpoint_message) == 104, "endpoint message layout");

#endif
