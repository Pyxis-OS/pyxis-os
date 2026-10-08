#ifndef ABI_BLUETOOTH_HCI_H
#define ABI_BLUETOOTH_HCI_H

#include <abi/message.h>

#define BLUETOOTH_HCI_RIGHT_CONTROL (UINT64_C(1) << 0)
#define BLUETOOTH_HCI_STATUS UINT64_C(1)
#define BLUETOOTH_HCI_ACQUIRE UINT64_C(2)
#define BLUETOOTH_HCI_RELEASE UINT64_C(3)
#define BLUETOOTH_HCI_SUBMIT_COMMAND UINT64_C(4)
#define BLUETOOTH_HCI_SUBMIT_ACL UINT64_C(5)
#define BLUETOOTH_HCI_RECEIVE UINT64_C(6)

#define BLUETOOTH_HCI_COMMAND_MAX 258
#define BLUETOOTH_HCI_ACL_MAX 4096
#define BLUETOOTH_HCI_ACQUIRE_ALLOW_DEVELOPMENT (UINT64_C(1) << 0)

#define BLUETOOTH_HCI_READY_PRODUCTION (UINT32_C(1) << 0)
#define BLUETOOTH_HCI_READY_DEVELOPMENT (UINT32_C(1) << 1)
#define BLUETOOTH_HCI_OWNED (UINT32_C(1) << 2)
#define BLUETOOTH_HCI_TERMINAL (UINT32_C(1) << 3)

#define BLUETOOTH_HCI_RECORD_EVENT UINT32_C(1)
#define BLUETOOTH_HCI_RECORD_ACL UINT32_C(2)

/* STATUS is header-only. Scalar capabilities expose no private packet,
 * controller identity, peer address or keys. Capacities are current settings.
 * An unavailable controller can still have a valid status reply. */
struct bluetooth_hci_status {
  uint64_t features, le_features;
  uint32_t flags;
  uint32_t command_max, acl_max;
  uint32_t command_queue_capacity, acl_queue_capacity, receive_queue_capacity;
  uint32_t command_credits, acl_credits, acl_packet_length;
  uint32_t reserved;
};

/* CONTROL permits exclusive process acquisition. Ownership survives handle
 * copies and closure; RELEASE or process exit starts confirmed cleanup.
 * A development-only running build requires explicit opt-in and is not
 * production compatibility evidence. Repeat acquisition returns BUSY.
 * Deadlines use CLOCK_NOW's monotonic nanoseconds, must be finite and within
 * the controller's bounded operation window. No user pointers are retained. */
struct bluetooth_hci_acquire_request {
  struct message_header header;
  uint64_t flags, deadline_ns;
};

struct bluetooth_hci_acquire_reply {
  uint64_t epoch;
};

/* RELEASE and RECEIVE carry this prefix. Epoch binds the operation to its
 * process-owned session. RECEIVE's deadline bounds idle collection only;
 * it does not cancel a posted controller transfer or relinquish DMA. */
struct bluetooth_hci_session_request {
  struct message_header header;
  uint64_t epoch, deadline_ns;
};

/* SUBMIT_COMMAND/SUBMIT_ACL append one whole wire packet after this prefix.
 * Include its HCI command/ACL header, without an H4 packet-type byte.
 * COMMAND is at most 258 bytes; ACL at most 4096 bytes including its header.
 * CALL_OK means copied queue admission, not controller/procedure success.
 * Queue exhaustion returns QUEUE_FULL without admission. */
struct bluetooth_hci_submit_request {
  struct message_header header;
  uint64_t epoch, deadline_ns;
  uint8_t wire[];
};

struct bluetooth_hci_submit_reply {
  uint64_t submission_id;
};

/* RECEIVE returns this metadata followed immediately by length wire bytes.
 * One entire EVENT or ACL record is copied; an undersized reply preserves the
 * queue head and returns BUFFER_TOO_SMALL. Sequence is session-consecutive.
 * Generation disambiguates reused connection handles; zero means no link.
 * submission_id matches a command response when applicable, otherwise zero.
 * Lost stream continuity terminates availability rather than hiding loss. */
struct bluetooth_hci_record {
  uint32_t kind, length;
  uint64_t epoch, sequence, connection_generation, submission_id;
};

_Static_assert(sizeof(struct bluetooth_hci_status) == 56, "HCI status layout");
_Static_assert(sizeof(struct bluetooth_hci_acquire_request) == 32, "HCI acquire layout");
_Static_assert(sizeof(struct bluetooth_hci_acquire_reply) == 8, "HCI epoch layout");
_Static_assert(sizeof(struct bluetooth_hci_session_request) == 32, "HCI session layout");
_Static_assert(sizeof(struct bluetooth_hci_submit_request) == 32, "HCI submit layout");
_Static_assert(sizeof(struct bluetooth_hci_submit_reply) == 8, "HCI submission layout");
_Static_assert(sizeof(struct bluetooth_hci_record) == 40, "HCI record layout");

#endif
