# Independent terminal sessions

A terminal session owns bounded input/output queues and independent dimensions without
borrowing a framebuffer, keyboard or space TTY. Application input/output handles
implement the existing CONSOLE protocol; libterm, libc standard streams and
launcher forwarding use the same interface as local framebuffer consoles.

Native layouts live in [the terminal ABI](../../include/abi/terminal.h).

## Creation and authority

Trusted init receives a `terminal` creation service. Its CREATE right returns
application input, application output, one attachment and a separate event
handle, or no handles on failure. The normal session launcher does not delegate
the service. An opted-in local multiplexer receives CREATE separately through trusted
startup. Creation accepts 1–512 columns and 1–256 rows. Allocation and
failure unwinding remain BSP-owned.

The application input handle permits CONSOLE READ and SIZE, plus
[interrupt arming](terminal.md#interrupt-arming-and-passthrough); the output handle
permits WRITE, SIZE, FRESH_LINE and SET_TAB_WIDTH. Application grants cannot
inject input, intercept output or hang up the terminal. Attachment rights are
separate: INJECT permits input injection and END_INPUT; DRAIN permits output
collection; HANGUP controls disconnection; RESIZE changes character dimensions. Copies refer to the same attachment;
there is no detach/reattach operation.

The event handle implements TERMINAL_EVENTS with only EMIT authority. It is not
an application stream or a readiness target and does not imply attachment rights.
The remote server delegates it only to the root shell; the shell does not pass
it to children, script interpreters or session successors.

Capability entries and in-flight capability transfers retain logical authority.
An internal operation or readiness reference retains storage only. Closing the
last HANGUP-authorized attachment grant hangs up even if restricted observers
still hold the attachment. Queued IPC grants retain authority until delivered
or discarded, so sender closure cannot invalidate a pending transfer.

## Queues and operations

Each session has 4 KiB of input and 64 KiB of output storage, including record
headers. Transfers accept at most 4 KiB of bytes per call. These are per-session
bounds, not a system-wide quota. The remote server separately limits admission
to four concurrent sessions.

Application reads retain the console timeout convention and return available
short input. Empty live input blocks, subject to that timeout. Application
output blocks until the whole call, at most 4 KiB, fits as one record, so a
continuous writer cannot fill the queue with fragments each sized to the space
just drained. Concurrent calls serialize accepted chunks and complete controls;
whole multi-call messages are not atomic. No terminal queue silently drops data under backpressure.

Libpyxis exposes creation, `terminal_try_inject`, `terminal_try_drain`,
`terminal_end_input`, `terminal_hangup` and `terminal_command_complete` in
`<terminal.h>`. Injection can accept a short prefix. Drain returns one complete output record, including its header.
A buffer too small for that record returns BUFFER_TOO_SMALL without consuming
it. Empty live output or full live input returns WOULD_BLOCK with no pending
operation; helper outputs remain unchanged on failure.

| Output record | Payload |
| --- | --- |
| DATA | 1–4096 bytes of native terminal output |
| FRESH_LINE | None |
| TAB_WIDTH | One uint64_t, 1–32 columns |
| COMMAND_COMPLETE | uint64_t command number, uint64_t kind and int64_t status |

`terminal_command_complete(events, kind, status)` accepts these kinds:

| Kind | Status |
| --- | --- |
| EXITED (1) | Exact signed 32-bit exit code of the last waited stage |
| FAULTED (2), TERMINATED (3) | Zero |
| LAUNCH_FAILED (4) | Zero; external command preparation or launch failed |
| BUILTIN (5) | 0 for success or 1 for failure |
| REJECTED (6) | Zero; syntax, command-form or submitted line-limit rejection |
| LAUNCHED (7) | Zero; successful background launch, not later process exit |

The kernel and libpyxis reject other kinds and out-of-range status with
BAD_REQUEST. Drained records must satisfy the same rules. The call queues a whole record after previously accepted output, waiting
interruptibly for space in the same bounded output queue. Hangup or execution
group stopping fails ENDPOINT_CLOSED. The kernel assigns numbers starting at 1
under the queue lock only when insertion succeeds; failed emits consume no
number. After UINT64_MAX, further emits fail LIMIT rather than wrapping. There
is no reply payload. An uncertain result must not be retried, since insertion
may already have succeeded.

Controls and completion events are indivisible and ordered with bytes. The
attachment forwards that order to its presentation. Kernel queues do not translate to host escape
sequences or implement a remote wire protocol. The shared behavior is the
[existing TTY contract](terminal.md#tty-output-controls): LF resets the column,
tabs preserve cells and clamp, wrapping is delayed, and FRESH_LINE cancels an
incomplete escape and advances only when required. Parser state survives DATA
record boundaries. Both framebuffer and host presentations must honor
these rules; no shared parser framework is introduced here.

## Resize

`terminal_resize(attachment, columns, rows)` requires RESIZE, separately from
INJECT, DRAIN and HANGUP. It accepts the same bounds as creation and updates
dimensions and generation as one locked snapshot. Identical dimensions succeed
without advancing generation. A changed size at UINT64_MAX returns LIMIT;
hangup returns ENDPOINT_CLOSED. Neither failure changes geometry. No queues,
handles or processes are replaced, and no output record is inserted.

Applications observe SIZE and the existing RESIZED interest on input/output.
Changes coalesce; callers re-query the generation before waiting again. The
attachment owns presentation and cropping: the kernel retains only geometry.
Queued output is interpreted at the presentation's current geometry. Remote
wire negotiation and host-window resize remain separate work.

## EOF and hangup

END_INPUT is idempotent. Previously injected bytes drain before application
reads return zero-byte EOF; later nonempty injection fails ENDPOINT_CLOSED.
Output remains usable. Zero-capacity application reads and zero-length application
writes/injections remain validated no-ops, including after hangup; they do not
probe closure. SIZE still reports the current dimensions. Closing the last
READ-authorized application input grant
also closes injection, discarding input no reader can consume. Closing the last
WRITE-authorized application output grant and the last EMIT-authorized event
grant closes output while preserving queued records; draining returns zero only
after the final record is consumed. Either kind of producer keeps output open,
including captured IPC grants. Final event-grant closure does not hang up or
terminate the session.

HANGUP, including final controlling-grant closure, is permanent and idempotent.
It discards both queues and wakes blocked application operations with
ENDPOINT_CLOSED. Accepted output is not a delivery acknowledgment. Neither
hangup nor EOF terminates processes; execution groups provide separate supervision.

`term_read_key` reports actual input EOF as TERM_KEY_EOF, even during an incomplete
escape sequence. The line editor returns TERM_LINE_EOF and discards an unfinished
line; it never executes text lacking Enter. Ctrl+D remains byte 0x04: on an empty
line it asks the editor to finish with TERM_LINE_EOF, and otherwise is ignored.
It does not close terminal input. Kilo exits on actual EOF, reporting failure if
unsaved edits would be lost. Framebuffer console input still has no EOF operation.

## Readiness and ownership

`wait_many` accepts attachment interests together with TCP interests. DRAIN
authorizes READABLE and PEER_FIN, meaning queued output and closure of both
output producers respectively; ordinary READABLE includes closure. INJECT authorizes
WRITABLE and WRITE_CLOSED, meaning input capacity and input closure; ordinary
WRITABLE includes closure. Either direction automatically reports ERROR on
hangup. Output closure can coexist with queued records, which must be drained
before zero-byte EOF. Application input handles support READABLE/PEER_FIN under
READ, including END_INPUT after buffered bytes drain. Input and output support
RESIZED under READ and WRITE respectively. Session generations start at one
and advance on committed attachment-authorized resize. Framebuffer console READ/WRITE handles
observe local resize generations. Both input kinds also support INTERRUPT on
an [armed handle](terminal.md#interrupt-arming-and-passthrough).

Readiness remains advisory, with the [wait_many contract](../devices/tcp.md#readiness-and-transfer-attempts)
for deadline/poll precedence and removal of all registrations/references before
return. Queue mutations notify waiters; terminal state is observed under its own lock. A BSP readiness worker handles
terminal-only waits without a NIC. TCP and mixed waits remain in the network
worker, which exclusively observes lwIP state. Shared per-task request storage
bounds each wait at 32 interests without a new global waiter cap.

The [remote server and host client](remote-terminal.md) combine these attachments
with execution-group supervision. Resize, reconnection and new framebuffer
routing remain outside that implementation.
