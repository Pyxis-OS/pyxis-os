# Remote terminal server and host client

The live boot configuration starts `boot://init-remote` in the Remote space.
It starts the native TCP terminal server on the configured guest IPv4 address,
port 2323. The Development space is the network owner; Remote waits for an
assigned address with a 100 ms clock sleep. Remote looks up the configured
selector under READ authority, then waits for the configuration owner to bind
and assign that candidate; lookup never activates hardware. An absent or unavailable NIC is
reported on the Remote tab. Init/server failures are not restarted, and address
changes invalidate the listener rather than rebinding it. The default `remote`
space starts the server on every CPU count.

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

Live/PXE Remote sets `remote_power = true`, allowing the remote root shell's
`reboot` and `poweroff` builtins. Any LAN peer reaching that unauthenticated
terminal can reboot or power off the machine; the owner accepts this exposure
for a sole-user home LAN. Installed defaults omit this grant while retaining
local power in `pyxis`. Ordinary programs and service providers do not inherit
power. See [boot authority](init.md#boot-configuration).

A successful power command does not return or send a final terminal
acknowledgment; `pyxis-remote` disconnects and can report the missing
acknowledgment. For a test loop, confirm the new boot before reconnecting after
`reboot`, or confirm shutdown after `poweroff`. A reported native power failure
keeps the guest running.

Remote init opens optional `host://` with read-write grants, using the same
[virtiofs setup](../devices/virtio-fs.md) as Development. Missing hardware leaves
HOST absent; operational mount errors stop that init. Host daemon restrictions
and host permissions still apply. All sessions share the actual export and
RAM-backed `tmp://`; they have independent terminals and execution groups,
not private files. `boot://` stays read-only.

The trusted `session --start-remote-services` handoff applies session/network
configuration to the provider environment and starts the remote service script.
The final `session --remote-server PORT` creates the listener and delegates it
to `remote-terminal.pxe`. Trusted startup carries terminal CREATE and an unbound
LAUNCH launcher. Remote shells receive their bound launcher, named terminal
input/output, distinct standard streams, memory, clock, system-information READ,
[kernel-log READ](../interfaces/kernel-log.md),
pipe creation, the selected
roots and optional ordinary networking, entropy, endpoint and profiling grants.
When the space opts into [screen capture](../interfaces/screen-capture.md), the
session and daemon explicitly preserve `screen_capture` with CAPTURE alone and
no transport rights. Remote shells and their ordinary commands can then observe
the whole local screen, even though remote shells have no DRAW grant. The
packaged Remote space opts in. A daemon without that resource passes none to
its shells; connecting remotely does not create capture authority.
Only the root shell receives the separate `terminal_events` EMIT capability;
ordinary children, scripts and session successors do not inherit it.
Selected exported service entries preserve their masks. Private controllers stay
with their trusted manager. Shells receive no listener, mount,
network configuration, physical keyboard, framebuffer, terminal attachment or
group supervision authority. Packaged live Remote sets `launch = true` (owner
accepted, 2026-10-11), supplying a separate `child_launcher` with LAUNCH alone.
Foreground children and their descendants stay in the session or mux pane group
and its cleanup. Installed defaults contain no Remote space; custom installed
Remote entries can enable the same setting.

For a single-CPU manual boot, select a trusted init which mounts HOST if wanted,
publishes its environment services, and calls
`session boot://session.pxe --configure-network --start-remote-services`.
Do not give multiple init scripts network configuration ownership.

## Remote multiplexer

Packaged live Remote enables `remote_multiplexer = true` (owner accepted,
2026-10-11). Request an interactive mux:

```sh
build/tools/pyxis-remote --mux 127.0.0.1 2323
```

Plain clients still receive the plain shell; enabling the setting admits mux
requests and does not change that default. Installed defaults have no Remote
space; owner-authored Remote entries can set the same flag. An unavailable request
is rejected without a fallback shell. `--mux` cannot accompany `--machine`,
`--no-shell-echo` or `--download-dir`. Both plain interactive and mux character
dimensions follow the host window through TIOCGWINSZ and SIGWINCH, within
1–512 columns and 1–256 rows.
Explicit `--columns`/`--rows` are ceilings, further limited by the actual host
window. Transient zero dimensions are ignored. A bounded signal pipe wakes the
client even while input, transport or stdout is stalled; one outstanding resize
and the latest desired size coalesce window changes.

Applied geometry confirms the outer native terminal's resize, independently of
pane layout completion or host display. Shrink clips the focused pane and keeps
hidden panes running; growth restores the retained layout. The host preserves
partial CSI/UTF-8 and OSC-discard state, colours, tab width and queued output.
Changed applied dimensions clamp its cursor, clear delayed wrap, reset margins
and clear the view for the application's repaint; identical dimensions leave it
unchanged. Native SIZE/WAIT_RESIZED reaches the shell or application, whose redraw
restores current content; the client does not retain a screen backing store.
One-row presentation clears its row without scrolling other host rows.

The daemon loads `mux` by command search inside its outer remote group and creates each
pane's independent terminal, group and stock root shell through a private
SEND-only broker. Trusted init, shell and session preserve a distinct
`remote_mux_terminal` CREATE grant for the daemon, which retains terminal CREATE
and an unbound LAUNCH launcher. Mux receives its bound LAUNCH-only launcher,
attachment, process WAIT and group CONTROL|WAIT;
the daemon retains supervision through cleanup. It freezes initial environment,
selected environment entries and remote resource masks per connection. Split cwd snapshots
preserve actual directory rights, with initial `tmp://` fallback before
publication. Mux receives scoped subgroup creation through LAUNCH, but no terminal creation, physical input,
DRAW or clipboard-store grant. Pane shells receive neither broker nor group
supervision. The live-only remote power and optional screen-capture policies
remain unchanged.

[Neovim's bundle](neovim.md) requires launcher and pipe-creation authority.
The existing `launch = true` delegates those grants through pane shells to
`nvim`; packaged live Remote enables it. An owner-added Remote space
needs the same setting; enabling mux alone does not provide it.

The [existing mux controls](multiplexer.md#controls-and-layout) provide BSP/equal
splits, eight stable digits, arrow focus, history and close confirmation.
Presentation uses the existing Pyxis colours and supported one-cell UTF-8;
resumable try-write rendering keeps pending output bounded while servicing panes.
Remote mux is keyboard-only: native pointer/clipboard and safe-Paste transactions
are unavailable. Host selection and ordinary paste remain host operations;
pasted newlines can submit commands and control bytes reach the prefix parser.
Prompt-based drop/upload and transfer interception are disabled. The propagated
informational `PYXIS_REMOTE_MUX=1` profile makes stock `xfer` refuse before
protocol emission; unsupported OSC bodies are discarded. Pane completions are
not merged into the client's single command sequence; mux exposes session FINAL.

Four total connections admit at most two mux connections, including closing
ones. Incomplete creation occupies one of eight pane slots; every created group
retains its slot through failed delivery or closure until cleanup completes.
A saturated host reader can exhaust the existing five-second close deadline
and disconnect before FINAL even when
guest groups are reaped. Already queued host-output bytes cannot be clipped
retroactively after physical shrink. Machine geometry stays fixed at its explicit
`--columns`/`--rows` values (default 80 by 24).

### Pane broker and cleanup

The [broker client](../../userspace/common/pane_broker.c) and
[daemon](../../userspace/remote-terminal/session.c) use private SEND transactions.
Each connection has one daemon-owned receiver. Mux creates reply and cwd-bootstrap
receivers, transferring only SEND callers in BEGIN; CHUNK carries at most four
directory grants. Successful COMMIT returns attachment, process WAIT and group
CONTROL|WAIT. ACCEPT is acknowledged before mux publishes the pane. SEND-receipt
closure releases delivery bookkeeping; it is not a broker reply or execution acknowledgment.

The daemon services at most four broker records per pass without waiting for
another cwd chunk. Directory/path storage is bounded by `LAUNCH_CAPTURE_MAX_SIZE`.
The ten-second transaction deadline is absolute. Clock readings refresh per
record and immediately before launch. Known pre-publication failures can refuse
a split. Uncertain launch or result/acceptance failure ends mux with
`CALL_OUTCOME_UNKNOWN`, cancels tracked groups and forbids a split retry; expiry
or clock failure after successful launch preserves that uncertain outcome.
CANCEL names the original nonreused transaction ID, so late replies cannot
attach to a reused pane. Unpublished and response-failed groups retain supervision
and their pane slot until WAIT_COMPLETE.

Disconnect, outer root exit/fault and daemon shutdown seal broker admission,
discard incomplete/queued transactions and terminate tracked groups. Root result,
output EOF and group cleanup are separate observations. Attributed HOST cleanup
can delay FINAL and connection reuse indefinitely; output EOF alone proves no
cleanup. Reverse mode admits only one connection. Pane-process observation stays
in mux; the daemon's maximum wait set is 31 interests, below the native bound of 32.

Qualification and measured costs are in the
[startup/splits record](../development/experiments/remote-multiplexer-task12.md)
and [resize record](../development/experiments/remote-multiplexer-resize.md).
The owner [qualified pane Neovim, TCC and live resize natively](../development/experiments/remote-multiplexer-resize.md#owner-native-qualification)
on main `a59a4ef3`.

## Reverse connections

Add `remote.beacon=NAME` to the PXE entry's kernel command line to make the
configured `remote` space connect to the host. For example:

```text
${PYXIS_REMOTE_BEACON}=t14
cmdline: init=boot://boot-init.pxe remote.beacon=${PYXIS_REMOTE_BEACON}
```

Place the macro definition at the top level of the Limine configuration, with
no spaces around `=`. Limine inserts its value once without recursively
expanding it, preserving names containing literal `${...}`. Generated entries
use this form automatically. See the
[pinned parser](https://raw.githubusercontent.com/Limine-Bootloader/Limine/v12.9.3/common/lib/config.c).

On the host:

```sh
make -C tools remote
build/tools/pyxis-remote --listen t14 0.0.0.0 2323
```

`--listen NAME HOST PORT` binds a numeric IPv4 address, broadcasts a beacon
immediately and every second on UDP 2324, then accepts one TCP session.
`--beacon-address IPv4` selects the destination instead of the default
`255.255.255.255`; use the subnet broadcast address when needed. The advertised
port is the host's TCP port. The guest connects to the beacon sender's address,
so no guest address needs to be entered. Names are case-sensitive, 1–63
printable ASCII bytes without spaces; they select a development machine and
provide no authentication. This mode remains unencrypted and assumes the same
trusted LAN as the existing server.

For native LAN use, allow the selected TCP port through the listening host's
firewall. With `HOST=0.0.0.0`, the default beacon follows the host's default
route; use `--beacon-address` with the LAN's subnet broadcast address when that
LAN is on a different interface.

The [beacon wire format](../../include/remote/beacon.h) has a 16-byte header:
`PYXISRT` plus NUL, version 1, name byte count, a big-endian TCP port and four
zero reserved bytes, followed by the name without a NUL. Wrong tags, versions,
lengths, reserved bytes, zero ports and different names are ignored.

Only reverse-mode trusted Remote bootstrap/session handoffs receive the
separate BROADCAST-only `udp_beacons` service grant. The daemon uses it to own
a wildcard endpoint on port 2324 while discovering. It shuts down and closes
that endpoint before connecting, dropping queued advertisements. Remote shells
retain UDP OPEN alone and receive neither the beacon grant nor its bootstrap
environment variable. Discovery does not grant network configuration authority
or replace DHCP ownership.

After connecting, HELLO selects the ordinary shell path or explicitly requested
mux path. One reverse session runs at a time. After disconnect, the daemon
terminates the outer group and any tracked pane groups, waiting for their
attributed cleanup before reopening discovery. Failed TCP
attempts wait one second before reopening; successful-session cleanup has no
added delay. The host tool exits when its session ends. Starting it again
advertises a fresh session, whose command numbering begins at 1. Waiting on the
host leaves stdin and terminal settings untouched; raw mode starts after accept.

For QEMU user networking, route the host's beacon into the guest with the
existing UDP forward and select host loopback as the beacon destination:

```sh
make run CPUS=4 MEMORY=2G VIRTIO_NET=1 UDP_FORWARD=2324:2324 REMOTE_BEACON=t14
build/tools/pyxis-remote --listen t14 --beacon-address 127.0.0.1 127.0.0.1 2323
```

`REMOTE_BEACON` only changes generated Limine command lines and image assembly;
native PXE entries can set the kernel option without rebuilding the kernel or
archive. Omit it for the ordinary four-client listener. Installed boot defaults
have no Remote space; an installed boot configuration must explicitly provide
one before using reverse mode.

## Explicit file transfer

Connect with an existing host download directory when downloads are wanted:

```sh
build/tools/pyxis-remote --download-dir /home/user/downloads 127.0.0.1 2323
```

In the remote shell, `xfer receive /home/user/Example.class Example.class`
uploads a host file into the current directory; `xfer send Example.class`
downloads it. Both ask for host confirmation before opening a host source or
accepting file data. Relative host paths, including `~/`, resolve under the host
user's home directory without shell expansion. Download names are reduced to a
single basename under the opened download directory. Without `--download-dir`,
downloads are refused. Host targets never replace existing names. Guest targets
require `xfer receive --overwrite HOST_PATH NAME` for replacement.

During a transfer the host consumes confirmation keys and Ctrl+C/Escape for
cancellation; Ctrl+] still closes the session. Pyxis holds raw terminal
passthrough and handles Ctrl+C itself. Other host input is consumed until the
transfer ends. A reply/confirmation can wait up to 120 seconds; unacknowledged
cancellation closes the host connection after five seconds rather than returning
late transfer bytes to the shell. Machine mode preserves OSC bytes in its normal
base64 output events and provides no transfer interception; `--download-dir` is
an interactive option.

Both sides stream with constant memory and require the negotiated `px_xfer=2`
protocol, with SHA-256, before data. A sender keeps up to 64 KiB of file data
unacknowledged, and the receiver acknowledges at least every 16 KiB;
`pyxis-remote` and the image must come from the same revision. There is no fixed size limit: a transfer is bounded by its declared size,
the destination's free space and the reply deadlines. The sender hashes the
source in a first pass, because the digest is announced before data, then reads
it again to send; a source that changes in between fails before publication.
The receiver exclusively creates a sibling staging file when data starts and
writes each chunk to it while hashing. Only after the size and SHA-256 match is
the staging file synchronized, renamed atomically, and its directory
synchronized. A handled failure/cancellation, including a full destination,
removes only that transfer's staging file. Abrupt death may leave
`.NAME.xfer-partial-ID`, holding a partial file of any size, beside the target:
in the Pyxis directory for uploads, in the download directory for downloads.
Stale names are never automatically removed. Pyxis `ls` lists them; the host
needs `ls -a`. One is safe to delete by hand once no transfer into that
directory is running. A large upload into a RAM-backed directory, such as
`tmp://` or a live boot's `home://`, holds its size in memory until the file is
removed. Atomic rename commits the complete destination, which
survives a later cancellation, synchronization failure or lost acknowledgement.
Host publication uses Linux `renameat2(RENAME_NOREPLACE)` or macOS
`renameatx_np(RENAME_EXCL)`; other host platforms fail publication explicitly.
Linux behavior is validated; the macOS path is unmeasured.

The frame format is kitty's [file transfer protocol](https://sw.kovidgoyal.net/kitty/file-transfer-protocol/)
(OSC 5113), single regular files only, with the mandatory negotiated SHA-256
extension (`px_xfer=2`). Peers that do not advertise it are refused, and stock
kitty, which lacks the extension, is unsupported. Every upload and download is
confirmed on the host, nothing is overwritten without an explicit flag, and names
arriving from Pyxis are reduced to a plain file name. Directories and projects
travel as an archive made with [tar](tar.md). Streaming has no size limit, keeps
unverified bytes only under the private staging name and fails if the source
changes. HTTP(S) fetch bodies stay out, and provider FILE snapshots keep their
16 MiB in-memory limit. Resume, compression, a progress display, transfers through
the local framebuffer console and changes to the remote wire protocol are not
provided. See the [native program notes](../../userspace/xfer/README.md) for
authority, filename limits and the extension, and the
[xfer qualification](../development/experiments/xfer-streaming/README.md) and
[pipelining](../development/experiments/xfer-pipelining/README.md) measurements.
Delivered in #445 (explicit transfer), #455 (drop), #551 and #554 (streaming and
the reply-read fix) and #600 (windowed `px_xfer=2`).

## Dropping a host file

Ordinary interactive `pyxis-remote` enables bracketed paste on the host terminal and
restores it on exit. At a known empty remote prompt, one pasted absolute or `~/`
path to an existing regular host file offers an upload into the current guest
directory. Single/double quotes and shell backslash escaping are removed without
executing a host shell. Accept with `y`; `n`, Enter, Ctrl+C or Escape refuses the
drop. Acceptance enters a safely quoted `xfer receive HOST_PATH NAME` command and
authorizes only its exact host-file query, with no second confirmation and no
`--overwrite`. Explicit transfers retain their usual confirmation.

Ordinary text, multiple paths, directories, final symlinks, non-ASCII/control
characters and unsupported quoting remain ordinary paste. The bounded candidate
buffer is 2048 bytes; longer pastes stream through normally. Paths are at most
1024 bytes and names at most 200 bytes. An injected command must fit the shell's
1024-byte buffer and a conservative visible-area bound, or the paste falls back.
Bracket delimiters are consumed locally; fallback sends the original body with
the client's existing Enter normalization. While a transfer or drop confirmation
is active, another paste is consumed without answering the confirmation.

The root remote shell opts into OSC 133;B after drawing an empty prompt.
The host combines this marker with ordered command completion and invalidates
readiness on ordinary input. Empty Enter/Ctrl+C and refused drops wait for the
next marker. Editing makes readiness unknown until a completed command and its
new prompt; canceling a partially edited line can therefore miss the next drop.
If input is forwarded while a submitted command is pending, including typing
into vi or queuing multiple commands, detection stays disabled for that connection.
Reconnect to restore it. This avoids mistaking an earlier command's prompt for
an idle shell while later queued input launches an application. Within a running
program, a dropped path is ordinary pasted text, never an injected upload command.
See [the tracking limit](../technical-debt.md#remote-drop-prompt-tracking).

Mux mode consumes bracketed-paste delimiters and forwards the body as ordinary
input, without prompt/drop tracking or upload injection. Machine mode neither
enables bracketed paste nor recognizes paths/markers.
Default machine sessions preserve the root shell's OSC marker bytes in OUTPUT;
`--no-shell-echo` uses the quiet editor and emits no prompt markers. No kernel,
terminal wire frame or filesystem grant is added.

The owner confirmed dropping files from host terminals into an idle remote shell
(2026-10-14, repeated); dropping into a running program or pasting ordinary text
behaves as a normal paste. Linux client/guest protocol checks are in the
[drop validation record](../development/experiments/xfer-streaming/README.md#drop-2026-10-07).
The macOS download-publication path (`renameatx_np(RENAME_EXCL)`) has no recorded
measurement.

## Client modes

Interactive mode uses a raw host terminal and restores its
settings and cursor on normal exit and handled failures. Ctrl+C and Ctrl+D are
forwarded to the guest; Ctrl+] requests session closure locally. Ctrl+C cancels
the line at the prompt and [terminates a running foreground job](shell.md#interrupting-foreground-commands).
After more than 4 KiB of typeahead that the running command does not read, the
server stops reading the connection until its pending input is accepted. A
Ctrl+C typed after that never arrives, so Ctrl+] is the fallback. A recognized Ctrl+] request has a five-second acknowledgment deadline,
after which the client disconnects and reports incomplete closure. A saturated
local outgoing queue pauses stdin, so an escape behind a long paste may itself
be delayed. Sending the host client SIGINT/SIGTERM or closing its terminal
forces disconnection. Unacknowledged failure paths use abortive TCP close so
a pending send backlog cannot hold FIN ahead of termination. Plain interactive
and `--mux` sessions negotiate live resize; machine sessions keep their
configured dimensions.
Reattachment is unsupported.

The presentation tracks the native TTY subset across output records: LF resets
the column, tabs move without erasing and clamp to the edge, wrapping is delayed,
and FRESH_LINE cancels an incomplete escape and flushes partial UTF-8.
Scroll regions, insert/delete lines, reverse index and region-bounded vertical
movement follow the [TTY control rules](terminal.md#tty-output-controls). Blank
rows keep current colours without glyph attributes. Geometry and application
screen transitions reset the region; repeated screen selection is idempotent.
The connection retains ownership of the host alternate screen. This supplies
visible-screen scrolling; host scrollback and separate application screen/cursor
backing are not added. During host-size negotiation, clipped operations still
wait for the applied geometry and repaint to recover hidden content. See the
[message-scroll qualification](../development/experiments/remote-message-scroll.md).
The shared [single-cell UTF-8 repertoire](terminal.md#tty-output-controls) is emitted
as UTF-8 for the host font; unsupported scalars and invalid bytes use U+FFFD.
Partial sequences survive output records; intercepted transfer/prompt controls
also interrupt pending text. Machine mode and transfers preserve every byte.
Interactive SGR uses the shared [colour and attribute rules](terminal.md#tty-output-controls):
bold, italic, underline, reverse, 256 indices and semicolon RGB, with atomic
rejection of malformed or oversized sequences. It resolves indices using
Aardvark and emits RGB and glyph attributes for the host font. Transfer controls
are intercepted before presentation as before; machine output remains byte-exact.

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

`--no-shell-echo`, accepted only with `--machine`, asks the server to start the
root shell with its quiet line editor. The shell then writes no prompt, input
echo, per-character redraw, cursor or style control, submission newline,
overflow color or `^C`. Editing keys, Ctrl+C cancellation, EOF and the line
limit, including its prompt-derived geometry, are unchanged. Application output
and controls, shell diagnostics and the foreground launcher's FRESH_LINE are
not filtered or altered. Commands and session successors started by the shell
do not inherit the option. Without it, machine and interactive sessions keep the
full line-editor presentation.

Completion events have a `kind`; only `exited` and `builtin` carry a value:

```json
{"type":"command_complete","command":1,"kind":"exited","exit_status":2}
{"type":"command_complete","command":2,"kind":"launch_failed"}
{"type":"command_complete","command":3,"kind":"builtin","status":1}
{"type":"command_complete","command":4,"kind":"launched"}
```

| Kind | Meaning |
| --- | --- |
| `exited` | Last foreground stage exited; `exit_status` is its exact signed 32-bit code |
| `faulted`, `terminated` | Last foreground stage faulted or was terminated |
| `launch_failed` | External command preparation or launch failed, such as a missing image or redirect |
| `builtin` | Builtin succeeded (`status` 0) or failed (1), including invalid builtin arguments |
| `rejected` | Parse error, command-form error or submitted line-limit rejection |
| `launched` | Background external command launched; its later exit is not reported |

The kernel assigns consecutive command numbers starting at 1 per terminal.
Foreground commands wait for cleanup; pipelines wait for all stages and report
the last stage. `session` and `service` remain builtin transactions even when
they launch processes. Existing diagnostics precede the completion. Empty or
space/tab-only lines below the line limit, cancelled/lost input and EOF with an
unfinished line emit nothing. Line-limit rejection takes precedence over blank
input. Explicit `exit` and a successful `session` handoff emit builtin success
before FINAL. Fatal wait, cleanup or terminal errors, including failed event
emission, end the shell unsuccessfully without inventing or retrying a
completion. The richer kind is reporting only; shell control flow and script
policy still use success or failure.

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
a record yet. Decode only `output.base64` as base64. Add `--no-shell-echo` after
`--machine` when captured output should contain only program output and shell
diagnostics. The session stays open across commands, including failed ones. After the final command:

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
type and payload length. Payloads are at most 4096 bytes. All integers use
explicit big-endian encoding; no native C layout crosses the wire. Partial
headers/payloads, coalesced frames and short writes are ordinary TCP behavior.

| Direction / type | Payload |
| --- | --- |
| Client HELLO (1) | Three u32: columns 1–512, rows 1–256, options (bit 0 NO_SHELL_ECHO, bit 1 MULTIPLEXER, bit 2 RESIZE) |
| Client INPUT (2) | 1–4096 input bytes |
| Client END_INPUT (3) | Empty; preserve queued input, then EOF |
| Client CLOSE (4) | Empty; terminate outer and tracked pane groups |
| Client RESIZE (5) | u64 request ID, u32 columns, u32 rows; exactly 16 bytes |
| Server READY (16) | Empty; root shell or mux successfully launched |
| Server OUTPUT (17) | 1–4096 native terminal bytes |
| Server FRESH_LINE (18) | Empty |
| Server TAB_WIDTH (19) | One u64, 1–32 |
| Server ERROR (20) | One u32 error code |
| Server FINAL (21) | Four u32: cause, process reason, signed exit-status bits, drain outcome |
| Server COMMAND_COMPLETE (22) | u64 command number, u32 kind, u32 status; exactly 16 bytes |
| Server GEOMETRY (23) | u64 request ID, u32 applied columns, u32 applied rows; exactly 16 bytes |

HELLO must arrive within ten seconds, precede other client frames and appear
once. Unknown option bits or any other HELLO size are rejected as a bad frame.
MULTIPLEXER combined with NO_SHELL_ECHO, an absent mux opt-in or exhausted mux
admission is rejected as resource failure. INPUT after END_INPUT, malformed
lengths and unknown types are rejected. COMMAND_COMPLETE follows READY and
precedes FINAL in ordinary shell terminal output order; mux emits no per-pane
command-completion frames.

RESIZE is independent of MULTIPLEXER in HELLO. Plain interactive and mux
clients request it; machine clients omit it. For a negotiated connection, empty
READY is followed by GEOMETRY
with ID zero and initial dimensions before terminal records. Subsequent request
IDs are nonzero and strictly increasing; the client has one request outstanding
and retains only its latest desired target. An acknowledgment must match that
request's ID and dimensions, even if the host window changed again meanwhile.
The daemon finishes its current wire frame before applying native resize, then
queues GEOMETRY before draining further terminal records. Native queues and
partial parser state remain intact; queued bytes use the presentation's current
geometry. Native resize failure reports ERROR internal and follows normal group
cleanup without a success acknowledgment. No stream grants or additional daemon
wait interests are needed for geometry metadata.

Kinds are exited (1), faulted (2), terminated (3), launch failed (4), builtin
(5), rejected (6) and launched (7). Status carries signed exit-code bits for
exited, 0 or 1 for builtin and must be zero otherwise. The client rejects
skipped/repeated numbers, overflow, unknown kinds and invalid status values.
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
Ordinary connections use at most thirteen wait entries: listener plus each
client's TCP, terminal attachment and process-or-group completion observer.
Two mux connections add eight pane-group observers and one private broker
receiver each, for at most 31 entries within the native bound of 32. The daemon
services broker records incrementally; synchronous launch/HOST capture can
still delay its shared service loop.

Natural root-shell exit seals and terminates remaining descendants. Explicit
CLOSE, transport failure or TCP peer FIN also terminates the group. TCP FIN is
session disconnection even if input bytes remain buffered; clients must use the
END_INPUT frame for graceful stdin EOF. This policy does not change native TCP
read semantics, where buffered bytes drain before read returns EOF.

For a successful `session` handoff, the remote root shell stays parked until its
successor finishes, then exits with the successor's signed normal exit status
(fault or termination becomes failure). Chained handoffs retain one parked shell
per handoff. The old input reader never resumes. The builtin success event
reports the accepted handoff after this wait; FINAL carries the propagated shell
outcome. Remaining descendants and disconnected sessions still use the same
group termination and cleanup path.

During orderly closure the server drains accepted terminal output, awaits outer
and any tracked pane-group cleanup, emits FINAL, shuts down TCP writes and waits
for the peer to close. Mux broker admission is sealed before termination.
A five-second deadline covers closing output and transport shutdown. Expiry
abandons the connection/output; the admission slot remains occupied until group
cleanup finishes. A published HOST operation can delay cleanup indefinitely.
FINAL complete means output frames were accepted by TCP before FINAL, not an
independent acknowledgment of host display or storage. Disconnect discards
presentation queues, never fabricates completion and never leaves detached
session execution. Final server supervision-handle closure also terminates its
groups if the server exits or faults.

## Consumers and limits

The client supports persistent command use, pipelines/redirection,
benchmark output capture and interactive Kilo through the same application
terminal contract as local framebuffer use. See the [shell guide](shell.md),
[edit/build/run walkthrough](../development/edit-build-run.md),
[allocation benchmark](../development/allocation-profiling.md) and
[I/O benchmark reference](../development/io-ipc-baselines.md) for command usage.
Machine output remains typed and incremental; graphical work still uses the
framebuffer.

Foreground remote commands receive the configured group-bound child launcher;
Neovim bundles can therefore launch their helpers. Plain commands still receive
no pipe-creation service, except a script run by `boot://init-shell.pxe`; foreground
bundles can request it explicitly.
Plain `ipcbench` and `iobench pipe` require the `session` handoff to obtain pipe
creation.
The parked shell lets these successors finish and report their results.
Ordinary `allocbench` and non-pipe `iobench` modes use the configured remote grants.
Ordinary children receive no group supervision. LAUNCH permits scoped subgroup creation.

On the LAN host, this captures all four IPC workloads hands-free, one reverse
connection per command. stdin EOF ends input while the client receives output;
no sleep, guest `exit` or session reattachment is needed. Set `run_dir` separately
for each staged A/B boot, and use the same fixed userland in both images.

```sh
run_dir="$HOME/xf/A1"
mkdir -p "$run_dir"
for mode in call send; do
  for size in 64 4096; do
    printf 'session ipcbench %s --size %s --messages 256 --rounds 5\n' "$mode" "$size" |
      pyxis-remote --machine --no-shell-echo --listen t14 0.0.0.0 2323 \
        >"$run_dir/${mode}-${size}.jsonl" || exit 1
  done
done
```

Each file contains warmup, five verified passes, a summary and FINAL with status
0 and complete drain. See the [before/after qualification](../development/experiments/remote-session-handoff/README.md)
for the QEMU NAT adaptation and inspected cleanup. Native post-fix qualification
remains an owner check.

The underlying [terminal sessions](terminal-sessions.md),
[TCP listener/readiness contract](../devices/tcp.md),
[execution groups](../interfaces/execution-groups.md) and
[termination ownership matrix](../interfaces/execution-group-termination.md)
record authority, queue and cleanup boundaries. Sessions with the same filesystem grants can access the same files. Four admitted sessions can occupy the service indefinitely;
there is no idle timeout or per-session CPU/memory quota. Transport record
capacity, including pending handshakes and TIME_WAIT, can prevent admission even
below that session count.

SSH/Telnet compatibility, authentication/TLS, machine live resize,
reattachment, remote graphics, a navigator, task migration and process
threads remain separate work. Current costs and revisit points are recorded in
[technical debt](../technical-debt.md#initial-independent-terminal-limits) and
[execution-group shutdown](../technical-debt.md#execution-group-shutdown).

## Validation evidence

The implementation slices were validated with ordinary kernel, SDK, ports,
userland/image and native-client builds using the existing compiler. The ports
recipe needed a temporary local CMake installation; no compiler-container rebuild
was needed. Interactive QEMU used the patched 10.2.2 with its local AHCI fix,
matching raw OVMF, CPU `max`, 256 MiB and nested KVM. Checks included one- and
four-CPU boots, VirtIO RNG and optional NET/HOST. Network forwarding bound only
to host loopback; one-CPU remote use selected a trusted network-configuring init.
These are nested-VM observations, not owner-host performance measurements.

Listener/readiness checks exercised bounded admission, continued use of accepted
streams after listener destruction, independent local outbound TCP, short transfers,
peer-FIN draining and a stalled reader while other clients progressed. Resuming
that reader delivered all 4,789,053 bytes unchanged. GDB inspected bounded native
and lwIP backlog counts, advisory interests and eventual transport/wait release.
Handshake expiry, address invalidation and allocation/handle-failure paths were
reviewed in code rather than individually runtime-exercised.

Temporary uncommitted native exercises covered terminal ordering/backpressure,
rights, EOF versus Ctrl+D, controlling-grant closure and queued IPC authority.
A blocked terminal writer filled exactly 65,536 bytes; later draining delivered
128 KiB unchanged. Terminal-only waits worked without a NIC, mixed TCP/terminal
waits progressed, and Kilo exited successfully on clean input EOF and failed with
unsaved edits. Local shell/Kilo editing, navigation, save, quit and readback passed.

Execution-group exercises covered membership, batch publication/sealing,
launcher-policy rejection, observer independence, final supervisor exit/fault,
CPU loops, clock/IPC/pipe/terminal waits and full writers. Network checks stopped
blocked ACCEPT, TCP READ/WRITE, UDP receive and an observed waiting RNG request.
Externally retained peers and a delivered IPC receipt remained usable. GDB observed
zero members while deferred tokens still held completion open, then empty completed
and retirement queues with free readiness/TCP/RNG/UDP request slots. File handoff,
launch preparation stop races, BSP provisional failure, HOST loans, display pixels,
keyboard ownership, ARP cancellation and receive-credit reclamation were inspected;
those specific races were not each measured or fault-injected.

The actual host client produced READY, ordered records and FINAL for shell exit
and END_INPUT; four concurrent shells remained independent. HOST creation/readback
was checked from guest and host. Root-shell exit stopped a background Lua loop;
client disconnection stopped foreground spinning and terminal-blocked work. A
stalled host reader left writable readiness pending while another session completed.
Kilo edited/saved a HOST file and normalized Enter and SS3 navigation/Home. Ctrl+]
received acknowledged termination. A loop with 6000 queued input bytes exercised
the client's five-second close timeout and corrected abortive-close path; GDB
observed reclamation and listener-only readiness afterward. The independent local
framebuffer shell remained usable; boots without NET/HOST reported network
unavailable on Remote while preserving local startup.

Persistent machine-client use through a FIFO exercised twelve numbered commands:
success, syntax/launch/builtin failures, last-stage pipeline status, Lua failure,
background launch, JSON-like program text, later success, line-limit rejection and
exit. Diagnostics preceded completion, background output could follow it, and
printed JSON stayed base64 output. Blank/cancelled/unfinished lines emitted no
completion; submitted input completed before END_INPUT. New sessions restarted
numbering at 1, interactive mode consumed events silently and restored the host
terminal, and GDB found listener-only readiness and an empty completed-task queue
after closure. Event authority/captured lifetime, allocation unwind, queue-full
cancellation, uncertain responses and sequence exhaustion were reviewed in code,
without separate injected runtime checks.

The historical checks above precede combined acceptance. They do not claim
exhaustive renderer, malformed-frame, failure-unwind or ownership-race coverage.
Published HOST cleanup remains unbounded. All jobs from those checks were stopped;
no committed tests, exercise programs or boot/output automation were added.

### Combined acceptance

Combined acceptance started from Pyxis `a2c4ab2` with userland `5cbfbbd`; the
Kilo fix was rechecked with ports `fc728f7`. It used four CPUs, nested KVM,
256 MiB, VirtIO NET/RNG and
a writable virtiofs export, with the default Remote init and loopback forwarding.
Ordinary image and host-client builds used the existing compiler. Two persistent
machine clients used separate input FIFOs and JSON captures; an interactive
client ran alongside them. The local Development shell also completed `ls`
through physical-key input while remote work continued. A framebuffer capture
was used only to inspect that local UI; remote commands and reports were read
as text.

Kilo created `host://hello.c`, searched for and edited its message, saved,
reopened and quit. Its idle status message cleared without another keypress.
TCC compiled the saved source to `host://hello.pxe`, which printed the edited
message and completed successfully. Missing compiler input produced a readable
diagnostic and status 1. The other session retained its independent `boot://share`
working directory while running a pipeline with redirection and reading back
its saved output. The shared HOST source and executable were visible on the host.

`iobench read boot://share/iobench-small.bin --bytes 32768 --rounds 1` reported a
verified warmup and sample with 32,768 bytes consumed and no failed pass.
`allocbench heap --rounds 64 --profile` reported 8,192 allocation attempts and
releases, zero failures and no backing-allocation requests in its measured
window. Both returned success and their full reports arrived through machine
output records. These checks establish usability and report collection, not a
new performance baseline.

Ctrl+] while interactive Kilo waited for input returned acknowledged group
termination and complete output draining. Disconnecting a client running a
foreground Lua CPU loop reclaimed its session. Pausing a host reader during a
large Lua output stream filled the terminal queue to exactly 65,536 bytes; GDB
observed a parked, interruptible writer and two group members while another
client continued completing commands. After disconnecting that reader and
exiting the remaining shell, GDB showed listener-only readiness, one transport
record and empty completed-task, object-retirement and TCP-retirement queues.

Page Down on a file shorter than Kilo's viewport exposed a cursor-boundary defect
in the pinned upstream editor: it could move past the EOF insertion row, and Left
could then read beyond the row array. The maintained port patch clamps paging to
EOF and bounds Left's previous-row lookup. A rebuilt image confirmed Page Down
on an eight-line file stops at the EOF insertion row, Left returns to the last
line, and editing/saving at EOF succeeds. Empty-file paging, Left, insertion,
save and quit were checked through the same native client.

The final ports change required a Kilo/ports image rebuild, not a compiler-container
rebuild. Validation used the actual clients, manual QEMU input and read-only GDB;
no tests, new exercise programs, fault injection or boot/output automation were
added. All client, QEMU, debugger and virtiofsd jobs were stopped after validation.

### Quiet input and typed completion

These checks used userland `f635ec9` with the Pyxis ABI, wire and client
changes from the same PR. After rebasing onto userland libc read-ahead as
`c99301f`, a rebuilt image repeated the core checks: TCC build, a `-9 | 42`
pipeline reporting 42, builtin failure, launch failure, Lua color output and
`exit` before FINAL. Quiet output again matched a byte-exact suffix of default
output for all seven commands. The build was an ordinary `make -j16 image` with the
existing compiler, and no compiler-container rebuild was needed. Interactive
QEMU 10.2.2 ran under nested KVM with four CPUs, 256 MiB, VirtIO NET/RNG, the
default Remote init and `TCP_FORWARD=12323:2323`. GDB was attached through
`make debug`. These are nested-VM observations.

A quiet and a default persistent machine session received the same commands
through FIFOs. The quiet session wrote no output for its prompt, typed input,
Backspace/Left/End editing, a Ctrl+C-cancelled line or a blank line. Neither
cancellation nor blank input produced a completion in either mode. For every
compared command, the quiet output records were a byte-exact suffix of the
default session's output. In the default session they were preceded only by
line-editor presentation ending in the submission newline. This held for the
`\x1b[31m`, tab, `\r` and `\x1b[2K` bytes from Lua and for an unterminated,
styled line followed by the launcher's FRESH_LINE. Completion kinds and values
matched between the sessions.

A TCC-built helper returned its argument, and exact codes 2, -3, 2147483647 and
-2147483648 were reported as `exited`. Pipelines `3 | 5` and `7 | 0` reported
5 and 0 after both stage diagnostics. A null-store program reported `faulted`
alone and as the last pipeline stage. `cd nowhere`, `exit now`, a failed
`session` launch and a denied `service start` reported builtin status 1.
Successful `cd` reported builtin status 0. An unfinished quote, a builtin in a
pipeline and an 1100-byte line reported `rejected`. A missing image, a missing
input redirect and a missing background image reported `launch_failed`. A
background Lua loop reported `launched`, and its output arrived after that
completion. At 20x2, both modes accepted a 30-byte line and rejected 31 bytes,
matching the limit derived from the 9-byte prompt. EOF after an unfinished line
emitted nothing. `exit` and a `session boot://init-shell.pxe` handoff each reported
builtin success before FINAL.

In this historical command-completion baseline, HELLO with then-unknown option
bit 1, option bit 31 or the former 8-byte payload each received ERROR bad frame.
Bit 1 now selects the explicitly enabled remote mux path. The interactive client
still rendered prompts, editing and diagnostics, and Ctrl+] closed it with acknowledged termination.
The local Development shell still showed its prompt, echo and `ls` output.
A GDB breakpoint on `terminal_events_call` showed 16-byte requests with
kind/status 1/2147483647 for a pipeline's last stage and 4/0 for a launch
failure.

`terminated` completions, fatal wait/cleanup/emission paths and the outcome of
allocation failures during launch preparation were reviewed in source, not
exercised. At that qualification revision Remote commands received no launcher,
so a child shell could not be started to observe non-inheritance at runtime. The option is carried only as
the root shell's own argument. The shell passes each command's own argument
list, and the successor's in the `session` case, so non-inheritance is
confirmed by source review. The FIFO clients, a small host script that compared
decoded captures, QEMU and GDB were stopped afterwards. No tests, exercise
programs or boot/output automation were committed.

### Packaged Remote child launch

Owner accepted `launch = true` for live/PXE Remote on 2026-10-11. Ordinary
kernel/userland image build passed with the existing LLVM 23.1.3 builder and
unchanged SDK/ports outputs. Interactive QEMU 10.2.2 (Q35, nested KVM, four CPUs,
2 GiB, VirtIO-net, reverse `t14` through UDP loopback forwarding) opened Neovim
in plain Remote and a `--mux` pane, edited/saved `home://test/test.c`, exited
and reopened it with the contents intact. The mux image enabled only the existing
`remote_multiplexer` setting in staged config. Client Ctrl+] with Neovim running
returned terminated/drain-complete for both session types. Existing QEMU GDB
inspected scheduler state; binding to the actual session/pane group and LAUNCH-only
rights were inspected in source. The later
[owner native check](../development/experiments/remote-multiplexer-resize.md#owner-native-qualification)
covers pane Neovim and live resize; native plain-session and disconnect cleanup
are not established by that report.

Packaged live Remote's permanent mux setting (owner accepted, 2026-10-11) passed
an ordinary kernel/userland image build and interactive four-CPU Q35/nested-KVM
QEMU on the same image, without staged config changes: plain `pyxis-remote`
opened the plain shell and exited successfully; `--mux` opened the mux, ran a
pane command and split into a second pane. Client close returned terminated/
drain-complete. Installed defaults still contain no Remote space.
