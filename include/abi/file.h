#ifndef ABI_FILE_H
#define ABI_FILE_H

#include <abi/message.h>
#include <stddef.h>

/* Operations use explicit offsets. SIZE accepts either READ or WRITE; WRITE
 * permits WRITE, RESIZE and SYNC, subject to backend/host restrictions. */
#define FILE_RIGHT_READ (UINT64_C(1) << 0)
#define FILE_RIGHT_WRITE (UINT64_C(1) << 1)
#define FILE_RIGHTS (FILE_RIGHT_READ | FILE_RIGHT_WRITE)

#define FILE_READ UINT64_C(1)
#define FILE_SIZE UINT64_C(2)
#define FILE_WRITE UINT64_C(3)
#define FILE_RESIZE UINT64_C(4)
#define FILE_SYNC UINT64_C(5)

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

/* Send the complete structure for every operation. SIZE and SYNC ignore body;
 * initialize messages to zero so unused payload bytes contain no stack data. */
struct file_message {
  struct message_header header;
  union file_payload body;
};

struct file_read_reply {
  uint64_t read;
};

/* A successful nonempty WRITE reports 1..size bytes; advance offset/address by
 * written and submit only the remaining suffix. A zero-byte WRITE validates
 * authority and arguments, then returns zero without extending the file.
 * Errors have no reply/count. OUTCOME_UNKNOWN means a submitted mutation may
 * have taken effect; never automatically replay it. Earlier successful calls
 * retain their known progress. RAM writes still complete in full or fail
 * unchanged; this is not a guarantee for every backing store.
 * Beyond-EOF gaps and RESIZE growth read as zero; shrink discards the tail.
 * RESIZE returns no reply bytes and can also report OUTCOME_UNKNOWN. */
struct file_write_reply {
  uint64_t written;
};

/* SYNC requires WRITE, has no payload fields and returns no reply. It requests
 * full backing-store synchronization of this file, not its parent directory.
 * Success on RAM is a no-op, not persistence. Host durability depends on the
 * backing service/storage; OUTCOME_UNKNOWN means completion was not confirmed.
 * Closing a handle and flushing a userspace stream do not imply SYNC. */

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
