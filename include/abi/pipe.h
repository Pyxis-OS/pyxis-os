#ifndef ABI_PIPE_H
#define ABI_PIPE_H

#include <abi/handle.h>
#include <abi/message.h>
#include <stddef.h>

#define PIPE_SERVICE_RIGHT_CREATE (UINT64_C(1) << 0)
#define PIPE_CREATE UINT64_C(1)
/* The pipe also carries value segments (PIPE_HANDOFF/PIPE_RECEIVE). */
#define PIPE_CREATE_VALUES (UINT64_C(1) << 0)

#define PIPE_RIGHT_READ (UINT64_C(1) << 0)
#define PIPE_RIGHT_WRITE (UINT64_C(1) << 1)
#define PIPE_READ UINT64_C(1)
#define PIPE_WRITE UINT64_C(2)
#define PIPE_TRY_READ UINT64_C(3)
#define PIPE_TRY_WRITE UINT64_C(4)
#define PIPE_HANDOFF UINT64_C(5)
#define PIPE_TRY_HANDOFF UINT64_C(6)
#define PIPE_RECEIVE UINT64_C(7)
#define PIPE_TRY_RECEIVE UINT64_C(8)

#define PIPE_HEAD_VALUE UINT64_C(1)
#define PIPE_HEAD_BYTES UINT64_C(2)
#define PIPE_HEAD_END UINT64_C(3)

#define PIPE_CAPACITY 65536
#define PIPE_READ_MAX_BYTES 4096
#define PIPE_WRITE_MAX_BYTES 4096

/* CREATE takes no payload, or this one; no flags is a byte-only pipe. It
 * returns both handles or neither; on failure the reply buffer is untouched.
 * The handles grant separate ends, and copied grants keep their respective
 * ends open until all copies close. */
struct pipe_create_options {
  struct message_header header;
  uint64_t flags;
};

struct pipe_create_reply {
  handle_t reader;
  handle_t writer;
};

struct pipe_read_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t capacity;
};

struct pipe_read_reply {
  uint64_t length;
};

struct pipe_write_request {
  struct message_header header;
  uint64_t buffer;
  uint64_t length;
};

struct pipe_write_reply {
  uint64_t length;
};

struct pipe_handoff_request {
  struct message_header header;
  handle_t value;
};

struct pipe_receive_reply {
  uint64_t head;
  handle_t value;
};

/* A call transfers at most its 4096-byte limit. READ returns available bytes,
 * waits if empty while writers exist, and reports EOF only after the last writer
 * closes and all queued bytes drain. WRITE returns available partial progress or
 * waits if full. Last-reader closure wakes all blocked writers with
 * CALL_ENDPOINT_CLOSED. Zero-length operations succeed after authority and
 * buffer validation, regardless of peer closure. Errors transfer no bytes and
 * leave both the data and reply outputs unchanged. There are no message or
 * atomic-write boundaries and no fairness guarantee among copied endpoints. */

/* TRY_READ/TRY_WRITE use the same request/reply layouts, rights and limits.
 * They return available progress, EOF or peer closure without waiting for
 * data or space. Empty/full live pipes return WOULD_BLOCK with no transfer or
 * pending operation. Zero-length behavior is unchanged. Readiness reserves
 * nothing; another endpoint copy may consume it before a try operation. */

/* A value-capable pipe carries ordered segments: byte runs and values. A value
 * is one moved capability: HANDOFF takes the caller's handle (its generation is
 * invalidated) and queues a READ-only grant on the same object, preserving its
 * lineage. Only a READ-capable FILE qualifies, never a one-pass stream. A pipe
 * without PIPE_CREATE_VALUES returns BAD_OPERATION, which is a safe byte-copy
 * fallback; so is any rejection before the move, which leaves the handle with
 * the caller. Full value slots wait like a full write; TRY_HANDOFF returns
 * WOULD_BLOCK. Last-reader closure returns ENDPOINT_CLOSED.
 *
 * READ stops at the next value: with a value at the head it transfers nothing
 * and returns CALL_PIPE_VALUE, including for raw readers that never asked for
 * values. RECEIVE reports the head: VALUE with an owned handle, BYTES (read
 * them first) or END; it waits while the pipe is empty and writers remain, and
 * TRY_RECEIVE returns WOULD_BLOCK instead. A value the caller's table may not
 * hold returns DENIED and stays queued. A late bad reply buffer closes the
 * returned handle. READABLE readiness covers either kind of head segment, and
 * EOF still requires the last writer closed, no bytes and no values. */

_Static_assert(sizeof(struct pipe_create_options) == 24, "pipe create options layout");
_Static_assert(sizeof(struct pipe_create_reply) == 16, "pipe create reply layout");
_Static_assert(offsetof(struct pipe_read_request, buffer) == 16, "pipe read request layout");
_Static_assert(sizeof(struct pipe_read_request) == 32, "pipe read request size");
_Static_assert(sizeof(struct pipe_read_reply) == 8, "pipe read reply layout");
_Static_assert(offsetof(struct pipe_write_request, buffer) == 16, "pipe write request layout");
_Static_assert(sizeof(struct pipe_write_request) == 32, "pipe write request size");
_Static_assert(sizeof(struct pipe_write_reply) == 8, "pipe write reply layout");
_Static_assert(sizeof(struct pipe_handoff_request) == 24, "pipe handoff request layout");
_Static_assert(sizeof(struct pipe_receive_reply) == 16, "pipe receive reply layout");

#endif
