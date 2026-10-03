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

/* Payloads contain no caller addresses. The native message header is separate;
 * exported delivery carries protocol/operation in its transport metadata.
 * Request/reply payloads, including their count/offset fields, fit in 4 KiB.
 * Send only the operation's fields and actual bytes, never unused capacity. */
#define FILE_PAYLOAD_MAX 4096

struct file_read_request {
  uint64_t offset;
  uint64_t capacity;
};

/* Followed immediately by size bytes, at most FILE_WRITE_MAX_BYTES. */
struct file_write_request {
  uint64_t offset;
  uint64_t size;
};

struct file_resize_request {
  uint64_t size;
};

/* Followed immediately by read bytes. A successful reply has this exact extent.
 * READ capacity must fit FILE_READ_MAX_BYTES; a smaller reply buffer is invalid.
 * Short reads are allowed; zero for nonzero capacity means EOF. */
struct file_read_reply {
  uint64_t read;
};

#define FILE_READ_MAX_BYTES (FILE_PAYLOAD_MAX - sizeof(struct file_read_reply))
#define FILE_WRITE_MAX_BYTES (FILE_PAYLOAD_MAX - sizeof(struct file_write_request))

/* A successful nonempty WRITE reports 1..size bytes; advance offset/source by
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
/* Native SYNC provides the stronger whole-current-pool guarantee: all ordered
 * data dependencies and covering COMMITTED journal records are durable before
 * success. Home checkpointing can continue afterward. */

/* FILE_SIZE has no payload fields. */
struct file_size_reply {
  uint64_t size;
};

_Static_assert(sizeof(struct file_write_request) == 16, "file write request layout");
_Static_assert(sizeof(struct file_write_reply) == 8, "file write reply layout");
_Static_assert(sizeof(struct file_resize_request) == 8, "file resize request layout");
_Static_assert(sizeof(struct file_read_request) == 16, "file read request layout");
_Static_assert(sizeof(struct file_read_reply) == 8, "file read reply layout");
_Static_assert(sizeof(struct file_size_reply) == 8, "file size reply layout");

#endif
