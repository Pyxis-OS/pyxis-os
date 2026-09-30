# Native remote terminal sessions

Status: tasks 1–6 are implemented: listeners/readiness, independent terminal
sessions, execution-group containment/termination, and the remote server/client.
The [implemented remote interface](../userland/remote-terminal.md) records startup,
authority, framing, shutdown and client usage. Tasks 7–8 remain agreed scope.
The prerequisite [BSP request milestone](../kernel/bsp-service-requests.md) is complete.

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

The default CPU 3 Remote init starts the service; networking and host forwarding
remain explicit. Listen/bind authority
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

Task 3's implemented contract is recorded in
[terminal sessions](../userland/terminal-sessions.md): 4 KiB input, 64 KiB output
including ordered record headers, 4 KiB transfers, fixed dimensions up to 512×256,
separate creation/application/attachment authority, drain-before-EOF on explicit
input closure and immediate error/discard on hangup. Ctrl+D retains its existing
line-editor meaning; actual EOF discards an unfinished line. A temporary manual
exercise program is authorized for task 3 validation, without committed tests
or boot/output automation.

### Readiness and duplex progress

Use readiness waits and nonblocking transfer attempts instead of relay processes.
A server execution loop can wait for TCP input/EOF, terminal output, writable
capacity or execution-group completion. Listener acceptance also needs readiness
so waiting for a new connection cannot stop existing sessions.

Readiness is a notification to try, not a reservation. A transfer attempt returns
actual progress, EOF, an error or WOULD_BLOCK without waiting for I/O readiness.
The BSP worker handoff may park a caller, including for polling. Registration and
state recheck must prevent lost wakeups; closure/error conditions must wake an
interested waiter. Short transfers advance only the accepted prefix. Bounded
buffers and fair servicing prevent one slow connection from blocking every other
session or hiding its own disconnect.

Scope the first wait facility to TCP listeners/streams, terminal attachments and
execution-group completion. Retain ordinary blocking interfaces. No callbacks,
retained userspace buffer pointers, submitted background I/O, process threads or
full asynchronous I/O framework belong to this milestone. It provides a useful
foundation for later [libuv work](neovim-libuv.md), not a libuv port.

Task 2 implements the agreed contract: a stateless `wait_many` syscall
observes 1–16 interests per call with one active wait per process. It retains
objects only through that call, validates the whole list before registration,
returns level-triggered results in input order and removes every registration
before return. Current readiness wins over an expired absolute deadline; zero
polls successfully with possibly empty results. Future deadlines are capped at
30 seconds. Invalid handles, unsupported interests and insufficient authority
fail without partial registration. Separate try-accept/read/write operations
have no deadline and leave no pending operation on WOULD_BLOCK. Stale readiness
followed by WOULD_BLOCK is ordinary contention; callers return to waiting.

READ authority permits readable data and peer-FIN observation; ordinary read
interest automatically includes FIN. FIN may coexist with buffered data, which
must drain before a successful zero-byte read reports EOF. WRITE authority
permits writable capacity and local write closure; FIN alone does not close
writing. ACCEPT authority permits accept-ready and listener closure. Relevant
terminal errors wake all these interests without requiring INSPECT. No extra
read/write authority is conferred by waiting.

The four-client echo consumer uses bounded per-client output and a rotating
service budget. It watches writable capacity only with pending output, suspends
reads while its buffer is full, and can retain a FIN-only READ-authorized interest
until FIN is observed. Removing that interest after observation avoids repeated
terminal-condition wakeups while waiting for output capacity.

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

Tasks 4–5 implement CREATE_GROUP returning CONTROL|WAIT supervision and a bound
LAUNCH-only launcher. The creator stays outside; descendants cannot replace their
membership with an unbound or foreign launcher. SEAL closes admission while allowing
natural drain; TERMINATE and final CONTROL closure also request safe stopping.
WAIT and WAIT_COMPLETE report immutable cleanup completion, including admitted
launches and attributed deferred releases. PROCESS_TERMINATED is distinct from exit
and fault. Explicit CONTROL delegation prolongs supervision; WAIT-only copies do not.
Natural shell exit causes the remote server to terminate remaining descendants. See
[execution groups](../interfaces/execution-groups.md) and the
[termination ownership matrix](execution-group-termination.md).
Temporary manual exercise programs are authorized for tasks 4–5, without committed
tests or boot/output automation.

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

## Remaining task-local decisions

Task 6 implements eight-byte type/length framing, NDJSON/base64 machine output,
END_INPUT distinct from transport disconnect, Ctrl+] local close, termination of
remaining descendants on root-shell exit, and five-second closing output expiry.
Cleanup still has no finite deadline. CPU 3 runs Remote with optional read-write
HOST; Development remains the single default network configuration owner.
The [interface reference](../userland/remote-terminal.md) is authoritative for
these implemented details. Task 7 still needs the concrete shell-only event API
and command-completion frame layout before implementation.

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
- [x] **2. Readiness waits and nonblocking TCP.** Establish the small wait contract
  and TCP listener/stream support, including closure/error notification and
  race-safe registration. Validate duplex progress and slow-reader behavior while
  preserving existing blocking users.
- [x] **3. Independent terminal sessions.** Add application/attachment authority,
  bounded ordered queues, dimensions/controls, attachment readiness and nonblocking
  attachment input/output attempts. Update
  libterm, libc, stream validation and launch forwarding together. Preserve local
  shell/Kilo operation; define the shared presentation behavior for the host client.
- [x] **4. Execution groups and launch containment.** Define supervision ownership,
  membership and no-new-launch ordering; enroll descendants, including batch,
  background and service launches. Preserve ordinary observer semantics. Establish
  the termination matrix before implementing cleanup; membership alone does not
  enable a remotely exposed server.
- [x] **5. Safe termination and group completion.** Implement safe stopping,
  blocked-operation detachment and ownership return across the matrix. Add final
  supervisor-close cleanup and group-completion readiness. Verify descendants,
  CPU loops, blocked I/O, failed launches and server-owner death before claiming
  complete session termination.
- [x] **6. Remote server and host client.** Add the default CPU 3 userspace server,
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

Task 2 validation: kernel and full image builds passed with the existing compiler
and the same local CMake prerequisite. Interactive one/four-CPU QEMU used nested
KVM, 256 MiB, VirtIO NET/RNG, raw OVMF and patched QEMU 10.2.2, with host-loopback
forwarding. One CPU echoed text beside an idle client and a byte-for-byte 64 KiB
half-closed transfer. GDB observed idle read-only interests, then no remaining
waits, listeners or transport records after finite admission completed. On four
CPUs, four accepted clients coexisted. A stalled reader reached zero send capacity
and a zero peer window; GDB observed its writable/FIN-only interest while other
clients continued echoing. Resuming that reader drained all 4,789,053 bytes
byte-for-byte through EOF. The separate local shell completed an outbound blocking
TCP exchange. Finishing the echo clients removed active/incoming waits and the
listener; the only transport record left belonged to that unowned outbound
connection after orderly shutdown. All validation processes were stopped.

Authority failures, poll/expired-deadline precedence, stale observations, terminal
error wakeups, busy-direction notifications and failed-try cleanup were reviewed
in code; no claim of runtime coverage or injected failures is made for those
paths. The echo consumer has no idle/output-drain timeout, and four stalled
clients can occupy all its active slots. No terminal-session API or remote shell
is introduced by task 2.

Task 3 validation: ordinary kernel, SDK, ports and full-image builds passed with
the existing compiler and local CMake prerequisite. Interactive one/four-CPU QEMU
used nested KVM, 256 MiB, VirtIO RNG, optional VirtIO NET, raw OVMF and patched
QEMU 10.2.2. The authorized temporary manual program exercised ordered records,
small-buffer preservation, bounded input, graceful EOF, actual key/line EOF versus
Ctrl+D, direction rights, controlling-grant closure and queued IPC grant delivery
and discard. Terminal-only poll, timeout and closure readiness passed without a
NIC. GDB observed a blocked writer at exactly 65,536 queued output bytes. Draining
allowed 128 KiB to complete; a four-CPU repeat verified every payload byte, and
the child's libc stdin reported EOF without error. Hangup woke a blocked writer.

On four CPUs, mixed waits woke first for delayed terminal output with TCP idle,
then for a host TCP connection while terminal input was full. Manual host echo
passed through loopback-only forwarding. Actual Kilo input EOF produced exit 0
for a clean buffer and exit 1 with unsaved edits. The ordinary local shell/Kilo
edit, navigation, save, quit and file readback workflow passed without a NIC;
Ctrl+D at an empty shell prompt still exited normally. All QEMU/debugger jobs were
stopped. No exercise source or automation is committed. Allocation-failure
unwinding and reader-queue handoff races were inspected, without fault injection
or claims of runtime coverage. Execution-group termination and remote serving
remain later tasks.

Task 4 validation: ordinary kernel, SDK and full-image builds passed with the
existing compiler and local CMake prerequisite. Interactive one/four-CPU QEMU used
nested KVM, 256 MiB, VirtIO RNG, no NIC, raw OVMF and patched QEMU 10.2.2. The
ordinary four-CPU shell completed a pipeline, redirected file readback and background
launch. The authorized temporary manual program covered creation/SEAL rights,
reply preservation, descendants and batches, rejection of unbound/foreign launcher
grants, and valid IPC delivery after rejected SEND admission. An ungrouped helper
used a delegated bound launcher; GDB confirmed its children/grandchildren joined the
group while the helper remained ungrouped.

Explicit sealing and final CONTROL closure rejected later single/batch launches
while existing delayed members exited normally. Zero-right supervision copies and
bound launchers did not keep supervision alive. Queued CONTROL grants retained
admission through sender closure and delivery; final discard/closure sealed it.
Closing a process WAIT observer did not stop its child. GDB observed published
membership and final destruction with zero members/controllers. On four CPUs, an
invalid second-stage grant unwound the first prepared child. A concurrent timed
sealer also won after one stage was prepared: the batch returned ENDPOINT_CLOSED
at stage 1 with all handles zero, no member published, and the group was reclaimed.
After validation, completed-task and retired-object queues were empty. All QEMU/
debugger jobs were stopped; no exercise source or automation is committed.

Final publication/seal lock ordering, cross-placement denial, reply-grant rejection
and allocation-failure unwinding were inspected in code. Those paths are not claimed
as injected or individually observed runtime cases. That task-4 revision exposed
sealing only; task 5 adds stopping and completion using the ownership matrix.

Task 5 validation on 2026-09-30 used ordinary kernel, SDK and image builds, plus
an uncommitted interactive native exercise. QEMU 10.2.2 with the local AHCI fix,
matching Fedora OVMF, CPU `max`, 256 MiB and nested KVM ran one and four CPUs.
Four-CPU local checks ran without a NIC; network checks used virtio-net, assigned
10.0.2.15/24 and the guest's own configured address for TCP connections. No
external network peer, HOST mount, committed tests or boot/output automation was used.

Four-CPU checks observed CPU loops, clock sleep, endpoint RECEIVE, queued and
delivered CALL, three competing pipe/terminal readers, full 64 KiB pipe/terminal
writers, terminal wait_many and group WAIT all stopping with TERMINATED/0. Outside
pipe/terminal owners remained usable; delivered CALL cancellation preserved the
outside provider's receipt. Final CONTROL closure left WAIT-only observation usable.
Sole supervisor exit/fault stopped members while preserving its EXITED/FAULTED result.
A three-member spinning batch and a member waiting on its spinning descendant both
completed shutdown. A bad second-stage image left every returned child handle zero
and allowed sealed completion. Empty-open polling, sealed/repeated WAIT, mixed
group/terminal readiness, WAIT/CONTROL rights rejection, repeated TERMINATE and
post-stop launch rejection produced their specified results.

Network checks covered blocked ACCEPT, TCP READ, TCP WRITE to an unread peer,
externally retained UDP receive and a member's solely owned UDP endpoint. GDB
confirmed CONTROL_ACTIVE/ACCEPT, READ_ACTIVE and WRITE_ACTIVE before stopping;
RNG_ACTIVE with a waiting syscall was observed at the stop request. Completed groups
left outside peers usable. GDB also observed zero members with complete=false and
two deferred cleanup tokens still pending, confirming that member reclamation alone
did not publish completion. At the end, completed-task/object-retirement queues,
readiness active requests and TCP/RNG operation slots were empty/free; the UDP
receive slots were likewise free after the UDP checks. One-CPU checks covered CPU
loops, clock sleep, terminal wait_many and blocked TCP READ with group completion.

The remaining ownership-matrix paths were reviewed in code, including file busy
handoff, launch capture/preparation stop races, BSP provisional-object failures,
HOST loans/retirement, display pixels, keyboard ownership, ARP-token cancellation
and receive-credit reclamation. Those specific races/failure paths are not claimed
as individually observed runtime cases. Published HOST work may delay completion
indefinitely; external capability ownership and independent TCP maintenance remain
outside the cleanup boundary. QEMU and debugger jobs were stopped after validation.

Task 6 validation on 2026-09-30: ordinary kernel, SDK, userland/full-image and
native host-client builds passed with the existing compiler and local CMake
prerequisite. QEMU 10.2.2 with the AHCI fix, matching Fedora raw OVMF, CPU `max`,
256 MiB, nested KVM and VirtIO RNG ran one and four CPUs; network runs enabled
VirtIO NET with loopback-only TCP forwarding. Four-CPU validation used a temporary
writable virtiofs export; one CPU used an explicit trusted network-configuring
init and no HOST device.

Native machine sessions produced READY, ordered data/control records and clean
FINAL after shell exit or protocol input EOF. Four concurrent shells ran commands;
GDB observed twelve idle read/process-completion interests, no writable interest
and no listener admission while full. HOST file creation/readback was checked from
both guest and host. Natural shell exit stopped a background Lua CPU loop; host
client termination stopped a foreground CPU loop and blocked terminal-input child.
After all clients left, GDB observed only the listener in the server wait set and
an empty completed-task queue. A stopped host reader produced pending-output TCP
writable interest with terminal draining disabled; another session still completed.
The local Development framebuffer shell also ran `ls` successfully.

Interactive native-client use opened Kilo on HOST, entered text, normalized Enter
and SS3 navigation/Home, saved and quit; host readback contained the expected
edited text. Ctrl+] returned acknowledged group termination. The one-CPU client
also completed ordinary input-EOF work. A CPU loop with 6000 queued input bytes
exposed FIN stuck behind send backlog on local-close timeout. The corrected client
uses abortive close without validated FINAL; repeating that case expired the
five-second acknowledgment deadline and GDB observed the loop/session reclaimed,
one remaining transport record (the listener), listener-only readiness and an
empty completed queue. This is nested-VM behavior, not a latency benchmark.

Malformed frame/authority/allocation-failure paths, the server's closing-output
deadline and host presentation edge semantics were inspected in code; no fault
injection or exhaustive renderer/runtime coverage is claimed. Task 8 retains the
broader combined acceptance work. A restored default four-CPU image without
NET/HOST showed the Remote tab reporting network unavailable and preserved local
startup. All QEMU, client, virtiofsd and debugger jobs were stopped. No tests,
temporary exercise program or
boot/output automation was added; validation used the actual client and ordinary
manual QEMU/GDB controls. Published HOST cleanup remains unbounded and command
completion events remain task 7.

Use ordinary `make -j16` builds, interactive QEMU and debugger inspection. Include
one- and four-CPU operation with matching networking/init configuration. Inspect
existing CI for each exact submitted revision and dependent repositories. Publish
userland/lwIP changes before parent gitlinks; change ports only if a concrete
integration need remains. No compiler-container rebuild is currently identified.

No committed tests, fault injection, boot/output automation or new CI are
authorized by this plan. Tasks 3–4 have explicit authorization for temporary manual
exercise programs. The machine-readable host client is the requested product interface;
validation should demonstrate it through ordinary command tools. Record what was
actually exercised and distinguish inspected failure paths from runtime results.

Exclude SSH/Telnet compatibility, custom authentication/TLS, OIDC, resize,
reattachment, remote graphics, multiplexer/navigation, Neovim, Unicode expansion,
foreground Ctrl+C cancellation, task migration and process threads. Those remain
separate milestones. Tasks 7–8 remain design and planned validation; completed slices and their
measured checks are recorded above.
