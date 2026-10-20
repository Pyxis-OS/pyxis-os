# Native byte pipes

A pipe is a bounded, unidirectional byte stream. Its read and write endpoints are
separately owned capabilities using `PROTOCOL_PIPE`. The
[public ABI](../../include/abi/pipe.h) defines creation, operation limits and replies.
The shell uses these endpoints with [batch launch](processes.md#batch-launch)
for [foreground pipelines](../userland/shell.md#foreground-pipelines). See the
[stream reference](../userland/shell-streams.md) for ownership across launch and libc.

[Owned capture output](output.md) uses declared session-account authority,
with FILE-compatible producers and process-local collectors. A pipe created with
`PIPE_CREATE_VALUES` also carries [value segments](#value-segments), the first
slice of [capability pipes](../wip/capability-model-implementation.md#task-9-first-slice-decisions-accepted-owner-2026-10-20);
other pipes stay byte-only. The shell creates value-capable pipes between user
stages. The [qualification and timing record](../development/experiments/capability-pipes.md)
covers npfs, RAM, boot and HTTP values, failures and a matched `cat | wc -c`.

## Creation and authority

`PROTOCOL_PIPE_SERVICE` with `PIPE_SERVICE_RIGHT_CREATE` accepts `PIPE_CREATE`,
with no payload or a flags word; `PIPE_CREATE_VALUES` is the only flag. Success returns a read handle with only `PIPE_RIGHT_READ` and a write handle with
only `PIPE_RIGHT_WRITE`. Allocation or capability-table failure installs neither
endpoint. Each pipe allocates a fixed 64 KiB buffer at creation; there is no
resize, configurable capacity or independent pipe-memory quota.

Creation uses an internal [BSP request](../../include/kernel/service/request.h).
The pipe subsystem claims both result slots before submission, transfers those
claims in a typed shared record, waits, then consumes its result before releasing
the record for reuse. Owned endpoint grants publish together under the table guard. Allocation, installation and rollback run on the BSP with
interrupts disabled. Completion drains unconsumed claims and detaches request inputs before
notifying the caller; an early notification does not enqueue a still-running task.
The record occupies the caller's reusable request allocation. A dedicated BSP worker
services requests in FIFO order, parks when idle, and receives a notification
when new work arrives. Between operations it enables interrupts and yields if
another BSP task is runnable. See the [BSP service requests](../kernel/bsp-service-requests.md)
for request lifetime, scheduling and storage contracts.

Boot delegates the named `pipe` service through init and session launch to the
shell. The shell forwards it on session handoff, but ordinary commands do not
receive creation authority by default. Native launchers may explicitly delegate
it. Holding an endpoint grants no authority to create pipes or acquire its peer.

Endpoints can be copied and delegated with the existing capability operations.
Startup stdin accepts the read endpoint; stdout and stderr accept write endpoints.
The existing exclusive standard-stream bindings still install one owned handle
per binding, adopted directly by libc. No ordinary startup copy is retained.
A caller choosing to copy a handle must close its own unused copies.

## Transfers

Native reads and writes clamp requests to 4 KiB per call and report the actual
byte count. A read returns available bytes, potentially short. An empty pipe
waits while writers remain; after the last writer closes, it drains buffered
bytes and then returns zero for EOF. A write returns available progress, possibly
short, or waits if the buffer is full. Once the last reader closes, nonempty
writes return `CALL_ENDPOINT_CLOSED`, translated to EPIPE by libc. Closing a reader
may discard bytes accepted earlier; successful writing does not prove consumption.

Zero-length operations succeed without waiting or observing peer closure after
handle and authority validation. A failed native call transfers no bytes and
leaves output storage untouched. A successful partial transfer is not an error;
the caller may submit its remaining suffix. There are no message boundaries,
per-endpoint mode flags, transfer deadlines, signals or shared seek positions.

`PIPE_TRY_READ` and `PIPE_TRY_WRITE` use the same layouts, rights and limits as
the blocking operations. They report available short progress, drained EOF or
peer closure immediately. An empty live reader or full live writer returns
`CALL_WOULD_BLOCK`, transfers nothing and publishes no pending operation.
Zero-length and error-output rules are unchanged. The native libpyxis helpers
are `pipe_try_read()` and `pipe_try_write()`; libc and its blocking streams do
not gain an `O_NONBLOCK` mode.

Multiple copied readers compete for bytes from the same stream. Multiple writers
contribute to that stream; large transfers may interleave between calls. No atomic
write size or strict fairness is guaranteed. Backend locking prevents corruption
and duplicate consumption, but wakeup does not reserve bytes or space for a task.

## Value segments

A value-capable pipe carries an ordered stream of segments: byte runs and
values. A value is one moved capability. `PIPE_HANDOFF` on a write endpoint
takes one handle from the caller's table, invalidating its generation, and
queues a READ-only grant on the same object with the same lineage, in order with
the bytes around it. Only a READ-capable FILE qualifies, native or exported,
including held failures; one-pass streams, pipe endpoints, directories, capture
producers and write-only grants are refused. Every rejection before the move
leaves the handle with the caller. A byte-only pipe returns `CALL_BAD_OPERATION`,
the signal for a producer to copy bytes instead. Concurrent writers interleave
whole segments.

Each pipe has four value slots, an implementation limit rather than a contract.
A handoff into full slots waits like a full write, `PIPE_TRY_HANDOFF` returns
WOULD_BLOCK, and last-reader closure returns `CALL_ENDPOINT_CLOSED`. A handoff
accepted just as the last reader closes is dropped, like accepted bytes.

READ and TRY_READ stop at the next value. With a value at the head they transfer
nothing and return `CALL_PIPE_VALUE`, for any raw reader, including one that
never asked for values. `PIPE_RECEIVE` reports the head as VALUE, with a newly
owned handle, BYTES (read them first) or END. It waits while the pipe is empty
and writers remain; `PIPE_TRY_RECEIVE` returns WOULD_BLOCK instead. The receiver
reserves its handle slot and validates the grant against its table while the
value stays at the head, so a refusal (DENIED, LIMIT) leaves the value queued
for a later attempt. A late bad reply buffer closes the returned handle. EOF
requires the last writer closed, no bytes and no values.

The reader holds the live object for as long as it keeps the value: it may
re-read it, and it sees later writes as any READ holder would. Cutting a
revocable value denies later reads whether it is queued or already received.
Last-reader closure releases queued values after dropping the pipe lock; a value
being received at that moment stays with its receiver. Writer closure leaves
queued values for the reader. Values carry no retention charge of their own.

## Readiness

[`wait_many`](../../include/abi/wait.h) accepts a pipe read endpoint with
`WAIT_READABLE` and/or `WAIT_PEER_FIN`, requiring `PIPE_RIGHT_READ`. A write
endpoint accepts `WAIT_WRITABLE` and/or `WAIT_WRITE_CLOSED`, requiring
`PIPE_RIGHT_WRITE`. Other interests are rejected before publishing the wait.
The existing 32-interest bound, absolute deadline and zero-deadline poll apply.
Pipe interests can share a wait with console, terminal, TCP and process interests.

Readiness is level-triggered and reserves nothing. READABLE means queued bytes
or a queued value;
WRITABLE means an open reader and room for at least one byte. Last-writer closure
returns PEER_FIN even when only READABLE was requested, including while bytes
remain buffered. With READABLE requested, READABLE plus PEER_FIN means buffered
bytes can still drain; PEER_FIN without READABLE then means the next nonempty
try-read returns zero. A PEER_FIN-only interest does not report whether bytes
remain. Last-reader closure returns WRITE_CLOSED even when only WRITABLE was
requested, suppresses WRITABLE, and makes a nonempty try-write fail with
`CALL_ENDPOINT_CLOSED`. Ordinary closure adds no `WAIT_ERROR`.

A copied endpoint can consume readiness before another caller transfers. Use a
try operation after a wait and handle WOULD_BLOCK, short progress and closure.
A wait retains only its watched endpoint, never its peer, until its worker
unlinks and completes the request. Thus watching a reader does not postpone EOF.

State observation uses the pair lock. Empty-to-nonempty writes, full-to-nonfull
reads and final endpoint closure notify the existing readiness workers after
releasing that lock. Admission registers a pipe interest before the initial
scan; a preceding transition is seen by the scan, and a following transition
notifies it. The existing remembered worker notification covers wake before
park. With no admitted pipe interests, transitions skip worker notification;
blocking pipeline traffic does not wake otherwise idle readiness workers.

## Closure, waiting and storage lifetime

Final-writer closure wakes every waiting reader, including readers that will find
EOF after another reader drains the remaining bytes. Final-reader closure wakes
every waiting writer with broken pipe. Condition checks and queue registration
share a lock, so closure cannot be lost between checking and sleeping. Wait
records live in task metadata and are detached before wake; no pipe lock spans
user memory access or a context switch.

Read and write endpoints have independent ownership. Each owns the shared storage,
which does not retain either endpoint. A blocked operation owns a storage
reference to its endpoint, without retaining the peer. Capability and transfer
grants count open directions separately, including attenuated zero-right grants;
CALL/readiness bookkeeping references do not postpone EOF or EPIPE. Final grant
release marks closure and detaches/notifies waiters synchronously under the pair
lock, with readiness notification after unlocking. Notification does not mean
those tasks have run before close returns.
Allocation and final destruction follow the kernel's BSP rules. Physical pair
storage remains until both endpoints are destroyed, independently of logical
closure; the last storage release schedules deferred destruction.

Normal exit and faults release remaining process grants through existing cleanup.
Closing only one of several copies does not close that direction. A launcher or
unrelated child retaining unused copies can therefore postpone EOF or EPIPE.
There is no process cancellation or forced pipe shutdown operation.

## Libc

[Stdio](../userland/stdio.md) dispatches pipe reads and writes through the FILE's declared
backend. `fread` and `fwrite` continue short transfers until their request completes,
EOF occurs or an error occurs. Earlier confirmed bytes remain consumed after a
later error; only complete elements contribute to their return count. Pipe seeks
report ESPIPE.

`fread_some(buffer, capacity, stream)` returns bytes already read ahead by
`fread`-based input, otherwise bytes after one backend transfer. Buffered input
may fetch up to BUFSIZ pipe bytes ahead; see [input read-ahead](../userland/stdio.md#input-read-ahead).
It blocks for initial data/EOF/error but never waits to fill the request after
positive progress. Cat uses it for all inputs, preserving bulk file copying while
forwarding terminal and pipe input promptly. A positive short read is not EOF;
a nonempty zero read sets EOF only when the backend actually reports it. Missing
input is EBADF and an unexpected zero terminal result is EIO. Existing EOF stays
sticky until cleared, and zero capacity changes no stream indicators.

On a value-capable pipe, the descriptor backend handles `CALL_PIPE_VALUE` in its
blocking and try paths: it receives the value, reads it to its end with FILE
reads, closes it and continues with the pipe. Byte readers therefore see one
non-seekable stream, including libuv streams through `pyxis_descriptor_try_read`.
The try path waits for nothing on the pipe but can wait for the value's own file
I/O. A failed value, such as an HTTP 404, fails each read with its outcome's
errno instead of ending the input. A value stays current until it reads to its
end, so a read error from it is returned as for a file; a provider that exited
or stalled reads as EIO. Close releases it. A child given this descriptor's pipe
continues after a partly read value, as it does after read-ahead.

`pyxis_input_take()` in `<pyxis/stdio.h>` moves the next value out of a stdio
input stream for a program that uses it as a value. It returns 0 when bytes,
pushback, read-ahead, a partly read value or the end come next, and on input
that is not a pipe. sbase `wc -c` uses it to count each value from `fstat`
without reading it. The libc handoff helper hands an eligible source to a pipe
stdout ([userland](output.md#userland)).

[Head](../userland/shell.md#bounded-input-with-head) closes its input at an exact line/byte
limit. An upstream producer can then receive EPIPE, including when already
blocked on a full pipe. No cancellation is needed to release that pipe wait;
other kinds of work or waits remain independent.
