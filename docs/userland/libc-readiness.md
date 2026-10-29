# Libc descriptor readiness

`poll`/`ppoll`, `select`/`pselect` and `fcntl(F_GETFL/F_SETFL)` operate on libc
file descriptors. They observe existing native authority; they do not create
sockets, acquire a terminal, change a grant or add signals.

## Supported descriptors

- Native console/terminal application input and output, with their held access.
- Native PIPE byte/value endpoints. FILE values retain ordinary synchronous FILE
  I/O: nonblocking mode excludes waiting on the pipe queue, not on FILE backing.
- Ordinary native or callable/exported positioned FILEs are immediately ready in
  their accessible directions, including EOF. This is not a promise of bounded
  storage/provider latency or future file-growth notification.
- RESPONSE streams and transaction staging/reply transitions refuse with ENOTSUP.
  Native TCP/UDP/IPC handles are not libc socket descriptors and are not adopted
  by this interface.

`POLLIN` covers available descriptor read-ahead, native input or readable EOF;
`POLLOUT` promises some progress, not that a whole write fits. Short transfer,
EAGAIN or another real error can follow a readiness observation. Requested
unheld directions report POLLERR. ERR/HUP/NVAL are reported independently of the
requested data mask, including events=0. A negative fd is ignored; a nonnegative
invalid fd reports POLLNVAL for that row. Duplicate rows remain independent and
poll returns the number of rows with nonzero revents.

Pipe reader HUP preserves unread bytes/values; drain before interpreting zero as
EOF. Closed pipe writers report POLLERR and write fails EPIPE without SIGPIPE.
Terminal input EOF/output closure reports HUP; hangup also reports ERR. Console
input loss is ERR, not EOF, and remains pending until ordinary read handles it.
Priority/band events and populated select exceptfds refuse with ENOTSUP; resize
and foreground interruption remain separate native events.

## Nonblocking mode and aliases

O_NONBLOCK is an open-object status shared by dup/dup2 aliases, alongside
O_APPEND. F_GETFL reports original selected access plus status; F_SETFL changes
only supported status, ignores creation/access bits and preserves old flags on
failure. Unsupported fcntl commands do not fake descriptor/CLOEXEC/lock behavior.
Open accepts O_NONBLOCK on its existing native-file route; fdopen preserves it.

On console/terminal and pipe byte transfers, unavailable live progress returns
EAGAIN, EOF remains zero and closed output remains EPIPE. Return available
progress without filling the entire requested transfer. Nonblocking mode
overrides terminal MIN/TIME without changing their saved values or raw ownership.
Native/termios snapshot restore changes raw state/timing, not O_NONBLOCK.
Regular FILE I/O, including FILE values carried through a pipe, can still wait
for backing/provider work. This preserves native `cat FILE | less` pipelines.

Already admitted operations keep their selected policy. Native children receive
explicit grants and fresh descriptor objects, not an inherited fd table/status
word; existing append handoff is separate. Stdio keeps its normal buffering and
error indicators; fd readiness does not see FILE pushback, flush pending output,
consume input, receive native values or commit transactions.

## Lifetime and waits

The first implementation has one I/O owner per watched open object. Exclude
concurrent close/dup2, competing read/rebuffer, provider commit and state-changing
I/O through aliases while a wait runs. Storage pins and slot generations reject
observed replacement; close is not a cancellation/wakeup interface. A stronger
concurrent-reader/close contract needs libc buffer-state and lifetime wakeups.
Do not copy native pipe grants merely to pin a wait and postpone peer EOF.
The consequence and revisit point are tracked in
[debt #1552](https://git.internal/PyxisOS/pyxis-os/issues/1552).

Each call uses a temporary native readiness set. Registration captures all
simultaneous sources before sleep. Results arrive in batches of at most 64 and
are collected without repeating returned rows; this is not a 64-fd limit.
Poll has checked INT_MAX/allocation limits plus the existing process-wide 16 MiB
native readiness metadata account, without another libc fd quota. Resource
admission can fail. Select uses FD_SETSIZE 1024 as its bitset representation;
nfds is highest fd+1, not the count of set bits. Invalid selected fds return EBADF
and failed calls preserve output sets.

Poll timeout uses milliseconds; ppoll/pselect nanoseconds and select timeval
microseconds. Zero polls, negative poll/NULL timeout waits indefinitely; waits
that can park need the startup clock READ|PRECISE for deadlines. One monotonic
deadline survives native 30-second slices; expiry returns 0. Timeout objects are
not modified. Empty fd sets wait on a real empty native set. There is no
periodic scan/sleep emulation or guarantee against lock/scheduler latency.

`ppoll`/`pselect` use the real temporary-mask semantics of
[cooperative libc signals](cooperative-signals.md), including atomic pending
check/enrollment and restoration on every return path. NULL keeps the current
mask; worker non-NULL masks fail ENOTSUP. Caught events interrupt waits with
EINTR regardless of SA_RESTART, preserving staged revents/fd_sets. Signals made
eligible only by restoring the caller's old mask preserve an already selected
I/O/timeout result. Priority/band and populated exceptfds remain unsupported;
native foreground interruption and raw Ctrl+C ownership are unchanged.

## Native ABI and rebuilding

The native [wait ABI](../../include/abi/wait.h) adds WAIT_READ_STATUS (bit 10) and
WAIT_WRITE_STATUS (bit 11) for PIPE and console/terminal application endpoints.
Each requires its existing matching data right and reports only closure/error,
without ordinary data/capacity. Existing readiness/disarm behavior is unchanged;
these observations consume no input or loss notification and add no I/O rights.

The new libc uses those interests. Build a matching kernel, export that SDK and
rebuild/relink static libc consumers and affected ports; an older kernel rejects
the new masks. There is no new structure layout/version, compatibility layer or
compiler-container rebuild. Native programs keep their no-signals design.
