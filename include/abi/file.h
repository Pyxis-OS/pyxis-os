#ifndef ABI_FILE_H
#define ABI_FILE_H

#include <abi/message.h>
#include <abi/handle.h>
#include <abi/file_info.h>
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
#define FILE_INFO UINT64_C(6)
/* Native capture producers only; see output.h. Ordinary FILEs reject it. */
#define FILE_HANDOFF UINT64_C(7)
#define FILE_OUTPUT_STATUS UINT64_C(8)
#define FILE_TRANSFER UINT64_C(9)
#define FILE_TRANSFER_MAX_BYTES (1024u * 1024u)
#define FILE_READ_BULK UINT64_C(10)
#define FILE_READ_BULK_MAX_BYTES (1024u * 1024u)

/* Payloads contain no caller addresses. The native message header is separate;
 * exported delivery carries protocol/operation in its transport metadata.
 * Request/reply payloads, including their count/offset fields, fit in 4 KiB.
 * Send only the operation's fields and actual bytes, never unused capacity. */
/* The fixed payload bound applies to ordinary operations; bulk data is separate. */
#define FILE_PAYLOAD_MAX 4096

struct file_read_request {
  uint64_t offset;
  uint64_t capacity;
};

/* Positioned bulk READ borrows the same READ authority as READ, plus CALL
 * transport for exports. Capacity is at most FILE_READ_BULK_MAX_BYTES and
 * offset + capacity must not overflow. deadline_ns is absolute monotonic time,
 * zero for unlimited; one deadline covers the entire operation.
 * Native CALL returns raw bytes, with the actual count in syscall reply_size;
 * exported calls use ENDPOINT_CALL_BULK with this 24-byte payload. The output
 * buffer must hold capacity bytes and remain owned/reserved until return.
 * No caller address is sent to a provider and no attachments are accepted.
 * Short reads are valid; zero for nonzero capacity means EOF. Zero capacity
 * still validates authority/arguments, then succeeds with no bytes. Only the
 * actual initialized count is exposed, never unused capacity. Errors provide
 * no usable count; output contents are unspecified and no writes follow return.
 * Admission charges kernel bulk data to the initiating process: at most 16 MiB
 * of live data, separately from ordinary completion metadata. LIMIT refuses
 * budget exhaustion; NO_MEMORY refuses allocation failure before delivery/I/O. */
struct file_bulk_read_request {
  uint64_t offset;
  uint64_t capacity;
  uint64_t deadline_ns;
};

/* Followed immediately by size bytes, at most FILE_WRITE_MAX_BYTES. */
struct file_write_request {
  uint64_t offset;
  uint64_t size;
};

/* Destination WRITE is the call target; source READ is captured separately.
 * Native RAM/initrd -> FAT32 and FAT32 -> RAM only. Other backing pairs refuse
 * before effects. Finite ranges may stop at source EOF. No caller addresses.
 * BUSY never waits while holding another FILE reservation. A completed reply
 * names only confirmed bytes; errors/uncertainty must never be replayed. */
struct file_transfer_request {
  handle_t source;
  uint64_t source_offset, destination_offset, size;
};

struct file_transfer_reply {
  uint64_t transferred;
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
 * unchanged, except retained-value budget exhaustion: it permanently discards
 * the private prefix and yields only an immutable TOO_LARGE failed FILE (see
 * retention.h). No successful zero-length read/size is substituted for failure.
 * This is not a guarantee for every backing store.
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
 * success. Home checkpointing can continue afterward.
 * It reports and acknowledges retained recoverable writeback errors.
 * An ongoing failure still fails each attempt; uncertain disk I/O stays terminal
 * until reboot. Closing a handle promises no durability and discards no dirty data. */

/* FILE_SIZE has no payload fields. */
struct file_size_reply {
  uint64_t size;
};

_Static_assert(sizeof(struct file_transfer_request) == 32, "file transfer request layout");
_Static_assert(sizeof(struct file_transfer_reply) == 8, "file transfer reply layout");
_Static_assert(sizeof(struct file_write_request) == 16, "file write request layout");
_Static_assert(sizeof(struct file_write_reply) == 8, "file write reply layout");
_Static_assert(sizeof(struct file_resize_request) == 8, "file resize request layout");
_Static_assert(sizeof(struct file_read_request) == 16, "file read request layout");
_Static_assert(sizeof(struct file_bulk_read_request) == 24, "file bulk read request layout");
_Static_assert(sizeof(struct file_read_reply) == 8, "file read reply layout");
_Static_assert(sizeof(struct file_size_reply) == 8, "file size reply layout");

#endif
