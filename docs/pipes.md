# Native byte pipes

A pipe is a bounded, unidirectional byte stream. Its read and write endpoints are
separately owned capabilities using `PROTOCOL_PIPE`. The
[public ABI](../include/abi/pipe.h) defines creation, operation limits and replies.
Shell pipeline syntax and safe multi-child launch are later tasks in the
[shell-streams milestone](wip/shell-streams.md).

## Creation and authority

`PROTOCOL_PIPE_SERVICE` with `PIPE_SERVICE_RIGHT_CREATE` accepts `PIPE_CREATE`.
Success returns a read handle with only `PIPE_RIGHT_READ` and a write handle with
only `PIPE_RIGHT_WRITE`. Allocation or capability-table failure installs neither
endpoint. Each pipe allocates a fixed 64 KiB buffer at creation; there is no
resize, configurable capacity or independent pipe-memory quota.

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
nonblocking mode, deadlines, wait sets, signals or shared seek positions.

Multiple copied readers compete for bytes from the same stream. Multiple writers
contribute to that stream; large transfers may interleave between calls. No atomic
write size or strict fairness is guaranteed. Backend locking prevents corruption
and duplicate consumption, but wakeup does not reserve bytes or space for a task.

## Closure, waiting and storage lifetime

Final-writer closure wakes every waiting reader, including readers that will find
EOF after another reader drains the remaining bytes. Final-reader closure wakes
every waiting writer with broken pipe. Condition checks and queue registration
share a lock, so closure cannot be lost between checking and sleeping. Wait
records live in task metadata and are detached before wake; no pipe lock spans
user memory access or a context switch.

Read and write endpoints have independent ownership. Each owns the shared storage,
which does not retain either endpoint. A blocked operation keeps its own endpoint
alive through its process's existing handle, without retaining the peer. Any
storage bookkeeping reference must not count as an open reader or writer.
Allocation and final destruction follow the kernel's BSP rules. Final handle
release schedules deferred destruction; EOF/EPIPE wakeups do not promise to have
completed synchronously when close returns.

Normal exit and faults release remaining process grants through existing cleanup.
Closing only one of several copies does not close that direction. A launcher or
unrelated child retaining unused copies can therefore postpone EOF or EPIPE.
There is no process cancellation or forced pipe shutdown operation.

## Libc

[Stdio](stdio.md) dispatches pipe reads and writes through the FILE's declared
backend. `fread` and `fwrite` continue short transfers until their request completes,
EOF occurs or an error occurs. Earlier confirmed bytes remain consumed after a
later error; only complete elements contribute to their return count. Pipe seeks
report ESPIPE.

`fread_some(buffer, capacity, stream)` returns bytes after one backend transfer.
It blocks for initial data/EOF/error but never waits to fill the request after
positive progress. Cat uses it for all inputs, preserving bulk file copying while
forwarding terminal and pipe input promptly. A positive short read is not EOF;
a nonempty zero read sets EOF only when the backend actually reports it. Missing
input is EBADF and an unexpected zero terminal result is EIO. Existing EOF stays
sticky until cleared, and zero capacity changes no stream indicators.
