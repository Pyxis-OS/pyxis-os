#ifndef ABI_COMPLETION_H
#define ABI_COMPLETION_H

#include <abi/message.h>
#include <abi/handle.h>
#include <abi/net_factory.h>
#include <stdint.h>

#define COMPLETION_SUBMIT UINT64_C(1)
#define COMPLETION_CANCEL UINT64_C(2)
#define COMPLETION_PAYLOAD_MAX 4096u
#define COMPLETION_FLAG_NOT_STARTED (UINT64_C(1) << 0)
#define COMPLETION_FLAG_CANCEL_REQUESTED (UINT64_C(1) << 1)
#define COMPLETION_FLAG_PROGRESS_KNOWN (UINT64_C(1) << 2)

/* Invoke SUBMIT on the native target. The bytes are copied before acceptance;
 * no output buffer is needed. All receivers share the process completion
 * metadata budget; exhaustion refuses with CALL_LIMIT before acceptance.
 * FILE_READ_BULK separately reserves its requested bulk-data capacity before
 * acceptance. Its nonzero FILE deadline can only shorten the submit deadline.
 * Its terminal payload is only a file_read_reply count; collect the bytes
 * through the received receipt's ENDPOINT_COLLECT_BULK operation.
 * Failed or short collection leaves the result available for another attempt;
 * receipt close/ACK discards it. */
struct completion_submit_message {
  struct message_header header;
  handle_t receiver;
  uint64_t correlation_id;
  uint64_t protocol;
  uint64_t operation;
  uint64_t deadline_ns;
  uint64_t buffer;
  uint64_t size;
};

/* Invoke CANCEL on the owned receiver with RECEIVE. Success is idempotent while
 * pending or terminal and never collects the result. Unknown/acknowledged IDs
 * return CALL_NOT_FOUND. CLOSE of the received receipt acknowledges completion. */
struct completion_cancel_message {
  struct message_header header;
  uint64_t correlation_id;
};

struct completion_connect_request {
  struct net_path path;
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
};

struct completion_udp_send_request {
  uint32_t address;
  uint16_t port;
  uint16_t reserved;
  uint64_t size; /* Followed by exactly size bytes. */
};

struct completion_udp_receive_request {
  uint64_t capacity;
};

struct completion_create_request {
  uint64_t name_length; /* Followed by one component, without NUL. */
  uint64_t kind;
  uint64_t rights;
};

struct completion_remove_request {
  uint64_t name_length;
  uint64_t kind;
};

_Static_assert(sizeof(struct completion_submit_message) == 72, "completion submit layout");
_Static_assert(sizeof(struct completion_cancel_message) == 24, "completion cancel layout");
_Static_assert(sizeof(struct completion_connect_request) == 24, "completion connect layout");
_Static_assert(sizeof(struct completion_udp_send_request) == 16, "completion send layout");

#endif
