# Remote terminal server and host client

The default four-CPU image selects `app://init-remote` on CPU 3, titled Remote.
It starts the native TCP terminal server on the configured guest IPv4 address,
port 2323. CPU 1 remains the network configuration owner; Remote waits for an
assigned address with a 100 ms clock sleep. An absent or unavailable NIC is
reported on the Remote tab. Init/server failures are not restarted, and address
changes invalidate the listener rather than rebinding it. Additional CPUs keep
the idle init; boots without CPU 3 do not start the default server.

## Host setup

Build and connect using the native host tool:

```sh
make -C tools remote
make run CPUS=4 VIRTIO_NET=1 TCP_FORWARD=2323:2323
build/tools/pyxis-remote 127.0.0.1 2323
```

`TCP_FORWARD=HOST_PORT:GUEST_PORT` requires networking and binds only host
`127.0.0.1`, forwarding to the stock guest address `10.0.2.15`. UDP forwarding
remains independent. There is no authentication or encryption: host processes
able to connect receive the configured shell privileges. The guest listener
binds its exact assigned address, not wildcard or loopback.

Remote init opens optional `host://` with read-write grants, using the same
[virtiofs setup](../devices/virtio-fs.md) as Development. Missing hardware leaves
HOST absent; operational mount errors stop that init. Host daemon restrictions
and host permissions still apply. All sessions share the actual export and
RAM-backed `home://`; they have independent terminals and execution groups,
not private files. `app://` stays read-only.

The trusted `session --start-remote-services` handoff applies session/network
configuration to the provider environment and starts the remote service script.
The final `session --remote-server PORT` creates the listener and delegates it
to `remote-terminal.pxe`. Only trusted init carries terminal CREATE and unbound
launcher CREATE_GROUP. Remote shells receive their bound launcher, named terminal
input/output, distinct standard streams, memory, clock, pipe creation, the selected
roots and optional ordinary networking, entropy, endpoint and profiling grants.
Only the root shell receives the separate `terminal_events` EMIT capability;
ordinary children, scripts and session successors do not inherit it.
The service namespace permits LOOKUP only. Shells receive no listener, mount,
network configuration, physical keyboard, framebuffer, terminal attachment or
group supervision authority. Nested launches remain in their execution group.

For a single-CPU manual boot, select a trusted init which mounts HOST if wanted,
creates its namespace, and calls
`session app://session.pxe --configure-network --start-remote-services`.
Do not give multiple init scripts network configuration ownership.

## Client modes

Interactive mode uses a raw host terminal with fixed dimensions and restores its
settings and cursor on normal exit and handled failures. Ctrl+C and Ctrl+D are
forwarded to the guest; Ctrl+] requests session closure locally. Ctrl+C retains
the shell's line-cancellation behavior and cannot interrupt a running foreground
command. A recognized Ctrl+] request has a five-second acknowledgment deadline,
after which the client disconnects and reports incomplete closure. A saturated
local outgoing queue pauses stdin, so an escape behind a long paste may itself
be delayed. Sending the host client SIGINT/SIGTERM or closing its terminal
forces disconnection. Unacknowledged failure paths use abortive TCP close so
a pending send backlog cannot hold FIN ahead of termination. Resize and reattachment are unsupported.

The presentation tracks the native TTY subset across output records: LF resets
the column, tabs move without erasing and clamp to the edge, wrapping is delayed,
and FRESH_LINE cancels an incomplete escape. Non-ASCII output is displayed as
one `?` cell; machine mode preserves every byte.

Machine mode requires no controlling terminal:

```sh
printf 'ls\nexit\n' | build/tools/pyxis-remote --machine --columns 80 --rows 24 127.0.0.1 2323
```

Standard input contains raw guest input bytes. Keeping stdin open keeps the
client usable for later commands; stdin EOF sends END_INPUT while continuing to
receive output. Standard output is newline-delimited JSON with separate ready,
output (base64), fresh-line, tab-width, command-complete, error and final events.
Program output cannot forge an event. Session FINAL describes shell outcome,
group cleanup and output draining. Transport EOF without FINAL is not successful
completion.

A completion event has this shape:

```json
{"type":"command_complete","command":1,"status":0}
```

The kernel assigns consecutive command numbers starting at 1 per terminal.
Status is the shell's success (0) or failure (1), not the child's numeric exit
code. Foreground commands wait for cleanup; pipelines wait for all stages and
use the last stage's result. Background commands report launch outcome only.
Syntax/launch errors and submitted overlong lines report failure. Empty or
space/tab-only lines below the line limit, cancelled/lost input and EOF with an
unfinished line emit nothing. Line-limit rejection takes precedence over blank
input. Explicit `exit` emits success before FINAL. Fatal wait, cleanup or
terminal errors, including failed event emission, end the shell unsuccessfully
without inventing or retrying a completion.

Events follow output already accepted by the terminal. Background output may
follow them; they do not mean every descendant is silent. Interactive clients
consume them without displaying them. Machine consumers must preserve the full
unsigned 64-bit command number and use the typed event, never a prompt, silence
or decoded output resembling JSON, to recognize completion.

### Persistent use through command tools

Keep the actual client running with a FIFO for later input. For example, create
a private directory once:

```sh
session_dir=$(mktemp -d /tmp/pyxis-remote.XXXXXX)
mkfifo "$session_dir/input"
printf '%s\n' "$session_dir"
```

Use that printed path in subsequent command-tool calls. Start the client as a
long-running command without a controlling terminal (replace `SESSION_DIR`):

```sh
build/tools/pyxis-remote --machine --columns 80 --rows 24 127.0.0.1 2323 \
  3<>SESSION_DIR/input <&3 >SESSION_DIR/events.jsonl
```

The extra read/write FIFO descriptor keeps stdin open between writes. In later
calls, submit a line and inspect newly appended JSON records:

```sh
printf 'ls\n' >SESSION_DIR/input
tail -n 20 SESSION_DIR/events.jsonl
```

Wait for its `command_complete` before submitting the next shell command;
foreground programs may read stdin themselves. Track the last consumed line or
file offset to collect output incrementally; a trailing partial JSON line is not
a record yet. Decode only `output.base64` as base64. The session stays open across
commands, including commands with status 1. After the final command:

```sh
printf 'exit\n' >SESSION_DIR/input
```

Collect its completion and FINAL, wait for the long-running client to exit, then
remove the FIFO and capture directory when no longer needed. Closing the client
instead terminates the entire remote session; losing it cannot be treated as a
successful command result.

## Framing and lifecycle

The shared [wire header](../../include/remote/terminal.h) is exported in the SDK
under `remote/terminal.h`. Each frame has two big-endian unsigned 32-bit fields:
type and payload length. Payloads are at most 4096 bytes. Integers are encoded
explicitly; no native C layout crosses the wire. Partial headers/payloads,
coalesced frames and short writes are ordinary TCP behavior.

| Direction / type | Payload |
| --- | --- |
| Client HELLO (1) | Two u32: columns 1–512, rows 1–256 |
| Client INPUT (2) | 1–4096 input bytes |
| Client END_INPUT (3) | Empty; preserve queued input, then EOF |
| Client CLOSE (4) | Empty; terminate the session group |
| Server READY (16) | Empty; shell successfully launched |
| Server OUTPUT (17) | 1–4096 native terminal bytes |
| Server FRESH_LINE (18) | Empty |
| Server TAB_WIDTH (19) | One u64, 1–32 |
| Server ERROR (20) | One u32 error code |
| Server FINAL (21) | Four u32: cause, process reason, signed exit-status bits, drain outcome |
| Server COMMAND_COMPLETE (22) | One u64 command number, then one u32 status (0 or 1); exactly 12 bytes |

HELLO must arrive within ten seconds, precede other client frames and appear
once. INPUT after END_INPUT, malformed lengths and unknown types are rejected.
COMMAND_COMPLETE follows READY and precedes FINAL in terminal output order.
The client rejects skipped/repeated numbers, overflow and status values above 1.
END_INPUT is idempotent. READY precedes terminal records and FINAL. Errors may
reject admission before READY or precede an admitted session's FINAL. Error codes
are bad frame (1), launch failure (2), resource failure (3), internal failure (4)
and greeting timeout (5). FINAL cause is shell exit (1), explicit client close
(2) or server error (3); process reason uses exited (1), faulted (2) or terminated
(3), with zero reserved for no process. Status is meaningful for exited processes.
Drain values are complete (1) or timeout (2); this server aborts a stalled
transport at its deadline, so timeout need not produce a deliverable FINAL.

The server admits four clients, including partial greetings and closing groups.
Each client holds one bounded input frame and one bounded output frame in
addition to the kernel's bounded terminal and TCP queues. A rotating loop gives
each client at most four service rounds before returning to the wait set. It
watches socket writability only with pending output and terminal input capacity
only with a pending INPUT frame. Stale readiness returning WOULD_BLOCK is normal.
The wait set contains at most thirteen entries: listener plus each client's TCP,
terminal attachment and process-or-group completion observer.

Natural root-shell exit seals and terminates remaining descendants. Explicit
CLOSE, transport failure or TCP peer FIN also terminates the group. TCP FIN is
session disconnection even if input bytes remain buffered; clients must use the
END_INPUT frame for graceful stdin EOF. This policy does not change native TCP
read semantics, where buffered bytes drain before read returns EOF.

During orderly closure the server drains accepted terminal output, awaits group
cleanup, emits FINAL, shuts down TCP writes and waits for the peer to close.
A five-second deadline covers closing output and transport shutdown. Expiry
abandons the connection/output; the admission slot remains occupied until group
cleanup finishes. A published HOST operation can delay cleanup indefinitely.
FINAL complete means output frames were accepted by TCP before FINAL, not an
independent acknowledgment of host display or storage. Disconnect discards
presentation queues, never fabricates completion and never leaves detached
session execution. Final server supervision-handle closure also terminates its
groups if the server exits or faults.
