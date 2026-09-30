# Native remote terminal sessions

Status: task 1 (TCP listeners and accepted streams) is implemented; the remaining
remote-terminal tasks are agreed scope, not implemented. The prerequisite
[BSP request milestone](../kernel/bsp-service-requests.md) is complete.
Wire layouts and the bounded implementation details listed below still need
review before their respective tasks; this document does not authorize code.

Task 1 implements the agreed [listener contract](../devices/tcp.md#listening-and-admission): trusted init holds
LISTEN authority, binds an exact configured local IPv4 address/nonzero port, and
delegates the resulting listener. CONNECT grants remain separate. There are at
most four listeners, each allowing four pending connections combined across
half-open and established-but-unaccepted states. Listener records also consume
the existing 32-record transport budget. Handshakes expire after ten seconds;
full admission leaves new SYNs unanswered. INSPECT and blocking ACCEPT have
separate rights, with one outstanding ACCEPT per listener and the existing
absolute-deadline convention capped at 30 seconds. Final listener release aborts
pending connections while accepted streams remain independent. A changed local
address invalidates its listener; wildcard/ephemeral binding and reuse are absent.

## Completion target

Ordinary agent and developer work should use selectable, incrementally captured
text over a persistent native TCP terminal session. Running commands, launching
new programs, compiling with TCC and collecting benchmarks must not require
repeated framebuffer screenshots. Screenshots remain appropriate for graphical
validation. Interactive Kilo is also an acceptance consumer: this is a terminal
session, not merely a command runner.

One accepted connection creates an independent terminal session and contained
execution group, then launches the existing shell with explicit grants. Sessions
share the service's existing space/CPU; no new spaces or task migration are
required. Independent terminal/shell state does not imply private files, separate
principals or quotas. This is the first concrete slice of the
[terminal-session direction](storage-and-terminal-agenda.md#4-native-terminal-sessions-multiplexer-and-navigator).
Its multiplexer and navigator remain deferred.

## Investigation baseline

Inspected Pyxis `e887df5` (merged PR #241), userland
`66b7f117556237b8e307b9bf3428132ed2903363`, ports
`90678ff95dfefd3952edbef9d9c58611beece4a7`, and lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. Sources were inspected at the
parent's pins. No build, compiler probe, boot or runtime compatibility result is
claimed. Filesystem-core sources were unnecessary.

| Existing facility | Evidence and implication |
| --- | --- |
| Native outbound TCP | [TCP ABI](../../include/abi/tcp.h), [transport contract](../devices/tcp.md): CONNECT only; no listener/accept capability. Streams already permit one reader and one writer concurrently across copied grants, positive short writes, bounded storage, deadlines, EOF after peer FIN, and explicit abort. |
| lwIP passive-open machinery | [Pinned TCP implementation](../../third_party/lwip/src/core/tcp.c), [SYN handling](../../third_party/lwip/src/core/tcp_in.c): listen/accept exists internally. [Caelum connection allocation](../../kernel/net/lwip/connection.c) attaches transport state to each allocated PCB, but does not establish a listener ownership model. Listen conversion moves extension arguments; passive open overwrites the new PCB's callback argument. Listener/accepted-record lifetimes and callback installation need deliberate integration. Backlog configuration is not enabled in [lwipopts](../../kernel/net/lwip/include/lwipopts.h). |
| Explicit terminal grants | [libterm](../../userspace/include/term.h) borrows named input/output independently of libc stdin/stdout/stderr. Its [implementation](../../userspace/libterm/term.c) directly calls CONSOLE. A pipe can carry bytes, but cannot answer dimensions, fresh-line or tab-width operations. |
| Framebuffer consoles | [Console object](../../include/kernel/object/console.h), [implementation](../../kernel/object/console.c), [spaces](../../kernel/space.c): the console borrows a space-owned TTY/framebuffer; writes render, without a readable output queue or remote attachment. Reads also check physical keyboard availability. Dimensions derive from pixels/font size. There is no independent session creation or input EOF/hangup. |
| Shell and ordinary stream consumers | [Launch preparation](../../userspace/shell/launch.c), [startup ABI](../../include/abi/startup.h), [libc descriptors](../../userspace/libc/descriptor.c): streams support console/file/pipe; named terminal input is delegated only to the first foreground stage with console stdin. Named output is separate and still delegated. A new terminal backend must update these checks and libc together. |
| Userspace service transport | [Endpoints](../interfaces/endpoints.md) provide bounded copied messages, authenticated resource rights, receipts and lifecycle notices. [Stream validation](../../kernel/object/object.c) permits exported FILE streams only. Existing pointer-bearing console requests cannot be reused unchanged as a cross-process terminal provider protocol. |
| Launch and completion | [Process contract](../interfaces/processes.md#implemented-userspace-launch), [process ABI](../../include/abi/process.h): explicit caller-space launch on the caller's assigned CPU, with WAIT-only observers. Closing an observer or its launcher does not terminate execution. The existing [session program](../../userspace/session/main.c) configures/hands off and exits; it is not a supervisor. |
| Duplex scheduling | [Userspace execution](../kernel/userspace.md#switching-and-cleanup), [pipe ABI](../../include/abi/pipe.h): one task per process; blocking operations park that task. TCP READ/WRITE proceed independently across processes, but pipes have no nonblocking mode/deadline, and endpoint RECEIVE/process WAIT cannot be combined in a wait set. |
| Host forwarding | [QEMU launcher](../../scripts/run-qemu.sh) supports explicit UDP forwarding bound to host 127.0.0.1; no TCP forwarding option exists. The same host exposure boundary is a candidate, not proof of guest authentication or host-local client identity. |

The [Kilo port](../../ports/kilo/README.md) uses libterm for key decoding, fixed
dimensions, cursor/style controls, screen redraw and timed input for status
expiry. It needs memory, directory and clock grants, not a mapped display or
physical keyboard grant. Its pinned [terminal patch](../../ports/kilo/patches/0001-pyxis-terminal-and-file-access.patch)
maps LF to its Enter key; [libterm key decoding](../../userspace/libterm/key.c)
does not normalize all host terminal key forms. Shell pipelines and TCC already
work locally; remote transport does not establish their authority or terminal
behavior automatically.

## Agreed contracts

### Admission and authority

The service is opt-in through a development init script. Listen/bind authority
is explicit and separate from outbound CONNECT; grant a configured guest address
and port. Initial QEMU forwarding binds only to host `127.0.0.1`. There is no guest
authentication in this milestone: local host processes able to reach that port
receive the configured session privileges. This is a development setup, not a
secure remote-login service.

Allow at most four concurrent sessions initially, with bounded connection
handshakes and no idle-session timeout. Partially admitted and closing sessions
must remain accounted for until their resources are reclaimed. Admission must
respect actual transport capacity, including half-open connections and TIME_WAIT;
four sessions do not imply that transport allocation can never fail.

Init explicitly selects the grants available to the server for delegation. Shells
receive read-only `app://`, writable `home://`, memory, clocks, terminal, launcher
and pipe creation. Optional `host://` rights are selected by init. Service lookup
and outbound network grants are also explicit, supporting ordinary HTTPS tools
without granting namespace management. Preserve any concrete runtime dependencies
needed by those consumers when inventorying grants.

Granted filesystem roots are shared between sessions. Do not delegate listener,
network-configuration, mount-management, physical-keyboard or framebuffer authority
to remote shells. Session setup does not blindly copy every server capability.
Shells can launch contained descendants but cannot control other sessions or
create execution outside their containment.

### Terminal sessions and presentation

Use a kernel-managed terminal session with bounded input/output queues. Separate
application-facing terminal capabilities from attachment authority. Applications
read input, write output and use terminal controls; the userspace server injects
input, drains output and reports disconnect through its attachment. The kernel
contains neither the remote wire protocol nor host-terminal adaptation.

Reuse the application-facing terminal contract through libterm and standard
streams. Separate framebuffer/physical-keyboard assumptions without adding a
remote mode to shell, Kilo or ordinary applications. Preserve the local
framebuffer shell and renderer through the selected contract. Initially there is
one attachment per session and validated, fixed rows/columns supplied before
launch; resizing is deferred.

Output bytes and controls form an ordered sequence, including fresh-line and
tab-setting operations. Full queues apply backpressure; do not silently drop
output. Existing blocking operations remain available to applications.

The host client translates Pyxis terminal behavior to host-terminal sequences and
tracks the state needed for fresh-line, tabs, cursor position and wrapping. Raw
byte forwarding is insufficient: native LF resets the column, tabs preserve cells
and clamp at the right edge, and fresh-line handles pending wrap and incomplete
escapes. Normalize host Enter and navigation forms needed by libterm/Kilo.
Inspect whether existing parser/state logic can be shared without coupling host
code to framebuffer rendering. Both presentations must agree on supported
behavior; sharing code is a means, not a new abstraction requirement.

### Readiness and duplex progress

Use readiness waits and nonblocking transfer attempts instead of relay processes.
A server execution loop can wait for TCP input/EOF, terminal output, writable
capacity or execution-group completion. Listener acceptance also needs readiness
so waiting for a new connection cannot stop existing sessions.

Readiness is a notification to try, not a reservation. A transfer attempt returns
actual progress, EOF, an error or WOULD_BLOCK without parking. Registration and
state recheck must prevent lost wakeups; closure/error conditions must wake an
interested waiter. Short transfers advance only the accepted prefix. Bounded
buffers and fair servicing prevent one slow connection from blocking every other
session or hiding its own disconnect.

Scope the first wait facility to TCP listeners/streams, terminal attachments and
execution-group completion. Retain ordinary blocking interfaces. No callbacks,
retained userspace buffer pointers, submitted background I/O, process threads or
full asynchronous I/O framework belong to this milestone. It provides a useful
foundation for later [libuv work](neovim-libuv.md), not a libuv port.

### Execution lifetime and disconnect

A terminal session and its execution group are separate objects. The server owns
supervision authority over the group; every shell descendant joins automatically.
Closing observers, background launch or delegated launch authority must not let a
member escape containment. Several groups and local work may share a space;
ending a session must not tear down that space.

Disconnect or an explicit close-session request ends the whole group. Ending a
group prevents new launches, requests termination of all members and exposes
completion only after cleanup finishes. Define launch/publication ordering so a
concurrent batch cannot escape termination.

An owned supervision capability controls the lifetime: closing its final
controlling reference, including on server exit/fault, starts termination.
Existing process completion observers retain their non-owning meaning. Ordinary
children do not receive the supervision capability. Separate group observation
must not accidentally keep supervision alive.

Termination prevents further userspace execution once the scheduler brings a
member to a safe stopping point. It does not free a still-running stack or data
lent to a service. Reclamation waits until outstanding kernel operations release
ownership. Running userspace is stopped through rescheduling; blocked operations
need explicit wait detachment and cleanup paths. An already-published BSP/HOST
request must finish the relevant ownership handoff before its caller is reclaimed.
Audit IPC receipts/attachments, file ownership, network operations, timed waits
and service queues as well as simple terminal waits.

There is no immediate cleanup deadline guarantee: an in-flight host operation can
delay completion. Do not claim termination succeeded merely because a flag was
set. Capability references already delegated outside a group are not recalled
by killing its processes. No detached execution or reconnect is supported.

Ctrl+C initially cancels the shell's current input line; it does not interrupt a
running foreground command or terminate the entire session. Foreground-group
interruption remains separate work. The host client provides a distinct local
escape to disconnect deliberately.

### Wire protocol, host client and command completion

Use bounded typed frames for initial dimensions, input, ordered output/control,
command-completed events, explicit close-session and final session outcome/errors.
TCP is a byte stream: partial/coalesced frames and short writes are normal.
Peer EOF or transport failure means disconnect, not successful command completion.
Command completion leaves the connection open for subsequent work.

The host client has interactive and machine-readable modes. Interactive use enters
raw mode and restores terminal settings/cursor on normal exit and handled failure.
Machine use must not require a controlling terminal: accept explicit dimensions
and input, stream output promptly, and preserve event boundaries separately from
program text. An agent must be able to keep the client running through normal
command tools, send commands and incrementally collect results.

The shell optionally emits a completion event after each top-level command, with
a monotonically increasing command number and its existing exit status:

- Foreground commands/pipelines report after their existing completion waits.
- Background commands report launch completion, not eventual child exit.
- Syntax and launch failures also report completion.
- Output already accepted by the terminal precedes the event; background output
  may arrive afterward. This is not a guarantee that every descendant is silent.

Only a separate capability held by the session shell permits these events; it is
not delegated to ordinary child programs. Events are structured outside program
output, so prompt-like strings cannot impersonate completion. Do not infer success
from a prompt, silence or connection closure. Session completion remains distinct
from command completion and reflects execution cleanup and the defined output
drain outcome.

## Task-local decisions before implementation

The direction above is settled. Resolve these remaining details in the associated
PR proposal rather than inventing them during implementation:

- Wait registration/ownership, interest limits, deadline behavior, readiness flags,
  object closure and invalid-handle results; nonblocking operation spelling.
- Terminal queue/frame bounds, dimensions, attachment closure and input-EOF
  semantics; treatment of terminal reads during session teardown.
- A per-subsystem termination matrix: retained resources, safe stopping point,
  waiter removal and final completion owner. No partial mechanism may be advertised
  as whole-session termination.
- Exact frame encoding and machine-client representation, stdin-EOF handling,
  local escape, natural shell-exit policy for remaining descendants, and bounded
  output draining when a peer stops reading. Finite draining must not be confused
  with a guarantee of bounded kernel cleanup.
- Concrete init grants, startup configuration and names. Resource names are not
  authority; preserve containment when launch capabilities are copied/delegated.

## Focused implementation tasks

Each task is intended for a focused PR, with dependent repository PRs where needed.
Split tasks further when reviewability requires it, especially termination.
Do not implement unrelated async, scheduling, authentication or multiplexer work.

- [x] **1. TCP listeners and accepted streams.** Define the bounded admission
  contract; implement native listen/accept authority and libpyxis helpers under
  existing network-worker ownership. Preserve outbound TCP and accepted-stream
  lifetime. Audit lwIP listen conversion, passive-open callback installation,
  generation identity and queued-packet cancellation. Validate native host/guest
  serving before introducing a shell server.
- [ ] **2. Readiness waits and nonblocking TCP.** Establish the small wait contract
  and TCP listener/stream support, including closure/error notification and
  race-safe registration. Validate duplex progress and slow-reader behavior while
  preserving existing blocking users.
- [ ] **3. Independent terminal sessions.** Add application/attachment authority,
  bounded ordered queues, dimensions/controls, attachment readiness and nonblocking
  attachment input/output attempts. Update
  libterm, libc, stream validation and launch forwarding together. Preserve local
  shell/Kilo operation; define the shared presentation behavior for the host client.
- [ ] **4. Execution groups and launch containment.** Define supervision ownership,
  membership and no-new-launch ordering; enroll descendants, including batch,
  background and service launches. Preserve ordinary observer semantics. Establish
  the termination matrix before implementing cleanup; membership alone does not
  enable a remotely exposed server.
- [ ] **5. Safe termination and group completion.** Implement safe stopping,
  blocked-operation detachment and ownership return across the matrix. Add final
  supervisor-close cleanup and group-completion readiness. Verify descendants,
  CPU loops, blocked I/O, failed launches and server-owner death before claiming
  complete session termination.
- [ ] **6. Remote server and host client.** Add the opt-in userspace server,
  restricted init grants, four-session admission, framed single-loop forwarding
  and explicit disconnect handling. Add the native host client with terminal
  adaptation, interactive/machine modes and host-loopback QEMU TCP forwarding.
  Keep host code out of target libc. Separate client and server PRs if useful,
  while reviewing their shared protocol together.
- [ ] **7. Shell completion events and agent use.** Add the optional shell-only
  event capability and ordered completion reporting. Document a persistent client
  workflow using ordinary agent command tools; exercise status, launch errors,
  background launch and program output that resembles a prompt/event.
- [ ] **8. Combined validation and documentation closure.** Demonstrate commands,
  pipelines/redirection, text errors, TCC compile/run and existing benchmark
  collection without screenshots. Exercise Kilo open/edit/search/save/quit,
  navigation and idle status expiry. Validate simultaneous sessions, independent
  local framebuffer use, disconnect during running/blocked work, slow clients and
  reclamation. Finish subsystem/usage docs and move this completed milestone out
  of WIP, carrying accepted limitations into technical debt.

## Validation and exclusions

Task 1 validation: ordinary `make -j16` and full `make -j16 image` builds passed
with the existing compiler. The host needed a temporary local CMake installation
for the existing Mbed TLS recipe; no compiler-container change was needed.
Interactive QEMU used one/four CPUs, nested KVM, 256 MiB RAM, VirtIO NET/RNG,
raw OVMF and the documented patched QEMU 10.2.2. Host forwarding bound only to
127.0.0.1. On one CPU, text and a byte-for-byte 64 KiB transfer passed; the last
accepted stream continued after listener destruction, new admission failed, and
GDB observed zero listener/transport records after peer EOF. On four CPUs, GDB
observed four established pending children and matching native/lwIP backlog
counts, with one accepted child and one listener occupying six global records.
Extra admission did not grow those counts. Listener closure reclaimed queued
children while the last accepted stream continued echoing. The independent local
shell completed an outbound TCP exchange and received DENIED when requesting
LISTEN; its closed outbound transport retained an unowned TIME_WAIT record.
All validation processes were stopped.

Handshake expiry, address invalidation, shared ACCEPT exclusion and allocation/
handle-failure unwinding were reviewed in code, without fault injection or a
claim of runtime coverage. Readiness waits, whole-session cleanup and a remote
shell are not implemented by task 1.

Use ordinary `make -j16` builds, interactive QEMU and debugger inspection. Include
one- and four-CPU operation with matching networking/init configuration. Inspect
existing CI for each exact submitted revision and dependent repositories. Publish
userland/lwIP changes before parent gitlinks; change ports only if a concrete
integration need remains. No compiler-container rebuild is currently identified.

No tests, fault injection, boot/output automation or new CI are authorized by this
plan. The machine-readable host client is the requested product interface;
validation should demonstrate it through ordinary command tools. Record what was
actually exercised and distinguish inspected failure paths from runtime results.

Exclude SSH/Telnet compatibility, custom authentication/TLS, OIDC, resize,
reattachment, remote graphics, multiplexer/navigation, Neovim, Unicode expansion,
foreground Ctrl+C cancellation, task migration and process threads. Those remain
separate milestones. This document records agreed design, not an implemented
remote-access facility or completed runtime validation.
