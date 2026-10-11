# Multiplexer over remote terminals

Status: contract accepted by the owner, 2026-10-11; not implemented. Tasks 1–2
are owner-assigned together (2026-10-11), to start only after this plan PR (#721)
is merged. Task 3 requires a separate owner go. Inspection uses Pyxis main
`34dfce9e` and pinned userland `d560fbb`.
The owner wants the existing splits, cwd inheritance and digit focus inside one
remote session on horse, including a large display such as 3440×1440.

## Inspected starting point

The [local mux](../userland/multiplexer.md) already composes panes into changed
VT cell runs. [render.c](../../userspace/mux/render.c) emits absolute cursor
positions, SGR, supported UTF-8 and cursor visibility through `term_write_all`;
it has no framebuffer drawing dependency. Its named CONSOLE streams can be
[independent terminal sessions](../userland/terminal-sessions.md). The existing
[host presentation](../../tools/remote/presentation.c) accepts those sequences.
A second renderer backend is therefore unnecessary for the first route.

The missing boundary is creation/supervision. Today's
[remote launch](../../userspace/remote-terminal/session.c) puts its root shell
in one execution group and supplies a bound LAUNCH-only launcher. Existing
[mux session creation](../../userspace/mux/session.c) needs terminal CREATE and
CREATE_GROUP for each pane. A group member cannot receive an unbound or
foreign-group launcher; bound launchers cannot create groups. An ordinary remote
command or `session` successor cannot simply become administrative mux.
See [execution-group containment](../interfaces/execution-groups.md).

Other inspected constraints:

- The [remote wire](../../include/remote/terminal.h) carries one byte stream,
  initial HELLO geometry and one command-completion sequence; no resize, pane,
  pointer or clipboard frames. Geometry is 1–512 columns by 1–256 rows.
- Native attachments already implement RESIZE/generation and apps observe
  RESIZED. Mux already implements shrinking to a clipped focused pane, retaining
  and draining hidden panes, then restoring its layout on growth.
- `Ctrl+B` and digits reach the guest; the host reserves `Ctrl+]` for closure.
  Prefix/double-prefix, eight stable pane numbers, cwd snapshots and existing
  layouts can be reused. The line editor and safe Paste remain ASCII-only.
- Mux drops pane COMMAND_COMPLETE records. Its plain pane shells do not emit
  remote-prompt markers. The emulator has no OSC state: a transfer OSC body can
  become visible text after its escape prefix is consumed. Existing file-transfer
  and drop handling cannot work through ordinary composed cells.
- Rendering currently blocks in `term_write_all`. A full outer terminal queue
  can stop all pane input/drain servicing, even though the daemon uses try I/O.
- Owned endpoint receivers are now waitable with READABLE/CLOSED. The existing
  [cwd channel](../../userspace/common/pane_cwd.c) uses one reader and non-expiring
  SEND messages before RECEIVE. This can support a private broker without a new
  kernel wait mechanism. A queued CALL can expire; that safety argument does
  not apply to arbitrary CALL traffic.

These are source findings. No remote-mux latency, bandwidth or runtime result
has been measured for this proposal.

## Accepted route

Use **guest VT composition with a private pane broker in the trusted remote
daemon**. Keep mux itself in the existing remote root group. The daemon,
which already holds CREATE and CREATE_GROUP, creates each pane terminal/group
and launches its stock root shell. Return attachment, process observer and
CONTROL|WAIT supervision to mux; retain daemon supervision before publication.
No foreign-group launcher enters mux, and pane shells receive only their own
bound launchers. This changes userland launch ownership, not kernel containment.
The renderer/profile are reused; focus needs a network round trip, and redraws
can expand short application output into many cells. The broker preserves the
root-group completion boundary and centralizes admission.

## Authority and lifetime

Use an explicit Remote-space opt-in, `remote_multiplexer = true`, and
an interactive client request, `pyxis-remote --mux`. Keep ordinary and
machine connections on their current shell path; reject an unavailable mux
request without silently falling back; reject `--machine --mux` together.
Do not reuse local `multiplexer = true`
unchanged: it also enables terminal pointer control and defaults clipboard
stores on. The remote flag permits broker service without those local grants.

The daemon retains terminal CREATE and unbound CREATE_GROUP. Remote mux gets
outer streams, the private connection-scoped broker caller, MEMORY, CLOCK and
endpoint creation for replies/cwd announcements, plus the existing remote
allowlist, roots, namespace and initial environment. It receives no physical
keyboard, DRAW, graphical pointer, terminal-pointer CONTROL or clipboard-store
authority. Pane shells get the existing remote shell grants, never broker,
attachment or group-supervision authority. Preserve `launch` delegation and
live-only `remote_power`; installed remote shells remain without power. Optional
screen capture remains exactly the existing Remote-space policy.

The existing unauthenticated, unencrypted trusted-LAN exposure remains: any peer
reaching an enabled remote service can use the configured filesystem/program
grants and request bounded pane creation. Panes sharing roots can access the
same files; they are independent terminal/groups, not filesystem sandboxes.

The private protocol must obey these invariants:

- Only daemon-owned per-connection receivers service broker requests. Mux creates
  its own reply and cwd-bootstrap receivers and transfers SEND-only callers;
  receiver ownership cannot move. The root shell announces its own cwd service
  directly to mux, preserving the current prompt-only cwd snapshot protocol.
- BEGIN/reply/bootstrap and cwd chunks fit the existing four-grant message bound.
  A successful pane result has three grants: attachment, process, supervision.
  Initial roots/environment/resource masks come from daemon-owned connection
  state. Fresh cwd directory chains retain their actual rights, remain owned
  through launch capture and are released afterward; descriptions confer no
  authority and are never reopened as paths.
- Process broker records incrementally in the daemon's fair wait loop. Use bounded
  SEND transactions, close receipts, bound incomplete state and apply deadlines;
  do not block all clients awaiting the next cwd chunk. Only a complete request
  may launch. The existing one-second prompt snapshot may fall back before
  publication; an uncertain published launch must be cancelled/cleaned, never
  retried as a new split. Connection-local transaction IDs cannot retarget a
  late reply to a reused pane.
- Each of eight pane slots counts creating, unpublished, response-failed and
  closing groups until WAIT_COMPLETE. The daemon retains CONTROL|WAIT before
  launch; result delivery failure terminates that tracked group. Mux still
  observes root exit/fault, requests descendant termination and drains final
  pane output. A root-process result is distinct from group cleanup completion.
- On disconnect, root fault/exit or daemon shutdown, seal broker admission first,
  discard queued/incomplete requests and terminate the outer group and every
  tracked pane group. Preserve the five-second transport-drain limit and
  unbounded attributed HOST cleanup. FINAL and connection-slot reuse require
  outer plus all pane-group cleanup; terminal EOF alone does not prove it.

Keep the existing four total connection slots, with **at most two mux
connections** initially. The daemon's conservative maximum is existing 13
interests plus two × (eight pane groups + one broker receiver) = 31, within
`WAIT_MAX_INTERESTS=32`. Root process/group observation alternates as today;
there is no extra daemon pane-process wait. Closing groups still occupy slots.
Reverse mode remains one connection. This avoids increasing the native wait
bound or adding service threads solely for this feature.

## Presentation, input and cost

Start with actual host character dimensions from TIOCGWINSZ/HELLO, not pixels;
3440×1440 alone says nothing about font size or terminal rows/columns. Later
wire resize reuses the daemon's outer RESIZE grant, native generation and mux
RESIZED handling. Coalesce host changes; order an applied-geometry response
before subsequent output, reset/clamp host presentation and redraw the mux.
Queued bytes are interpreted at current geometry, as in native sessions. Shrink
retains clipped/hidden panes; growth restores layout. Existing allocation rollback
is per screen/frame, not whole-layout atomicity: later resize failure can follow
earlier updates and ends mux with diagnostics and group cleanup.
The interactive client owns full-screen host presentation and restores its
modes/cursor on handled exit or failure; qualify that handoff separately from
pane alternate screens, which remain inside the guest emulator.

Keep `TERM=pyxis` and the single [sequence/repertoire table](../userland/terminal.md#tty-output-controls):
Aardvark first 16 indices, xterm cube/greys, semicolon true colour, bold/italic/
underline/reverse and the supported one-cell UTF-8 box/arrows/Latin-1 subset.
The host font renders glyphs and attributes; no bitmap image is sent. Unsupported
scalars/invalid bytes show replacement, with no combining/wide-character claim.
Pane alternate screens/scroll regions remain in the emulator; outer output is
only its composed cells. Do not advertise xterm or expand the profile here.

Ctrl+B commands and double Ctrl+B stay byte-based. Ctrl+C reaches the focused
pane's interrupt rules. Accepted pending input keeps its original pane across
focus changes. Current 256-byte mux/per-pane staging and server/client queues
remain bounded; a full input queue can delay later focus, resize or close frames.
Ctrl+] and forced host disconnection retain their existing escape/cleanup behavior.

Remote mux is keyboard-only initially. Host text selection and ordinary paste
are host operations, not native mux selection or safe clipboard transactions.
Current bracketed-paste delimiters are consumed locally and the body becomes
ordinary input; newlines can submit commands and control bytes can reach the
mux prefix parser. Do not claim local safe-Paste semantics. Disable prompt-based
drop/upload and cross-pane transfer interception in mux mode. Stock `xfer` must
refuse before protocol emission using an explicit propagated execution-profile
marker (informational, not authority); ordinary non-mux sessions keep transfer.
Consume unsupported OSC safely instead of displaying protocol bodies. Pane-aware
transfer, native clipboard and pointer transport need a separate owner go.
Do not merge per-pane completion numbers into the client's one shell sequence;
initial mux mode exposes session FINAL, not machine per-command completion.

Bound render output with a resumable encoder, a fixed pending-byte buffer and
existing CONSOLE try-write/WRITABLE readiness. Preserve partially accepted CSI/
UTF-8 suffixes and frame state, keep servicing input/pane drains, and coalesce
new presentation behind a stalled host. Advance the previous-frame state only
for accepted output; this is queue acceptance, not host-display acknowledgment.
Retain eight panes, 1,024 history rows and 12-byte cells; no unbounded output or
history queue. At maximum retained width a pane can approach 9 MiB of cell
storage, plus frame buffers and process backing; four connections are not a
physical-memory promise. Record allocation peaks and failure cleanup.

**Unmeasured arithmetic:** 400×80 mostly uniform ASCII cells need about 32 KiB
of glyph bytes per full redraw plus positioning/styles. Frequent colour changes
add many more CSI bytes. Changed runs usually send less, but a single scrolling
line can change most visible cells. A full 4096-byte OUTPUT frame adds 8 bytes
of wire header (~0.20%); today's interactive client frames individual ordinary
key bytes, adding 8 bytes per byte before TCP overhead. No throughput, latency
or redraw-rate promise follows. Measure input-to-visible focus/echo, split/cwd
latency, bytes for editing/scroll/layout, and slow-reader responsiveness against
a plain remote session at matching geometry. Synchronous launch/HOST work and
the existing cwd timeout remain separate latency sources.

## Ordered tasks

Tasks 1–2 are assigned together by the owner (2026-10-11), conditional on #721
being merged. Capture both pre-task baselines before implementation. Task 3
requires a separate owner go.
Before task 1 code, capture exact-main plain remote costs/backing and initial
host geometry. Before task 2, capture matched pane/render/backpressure workloads;
repeat them afterward. Task 3 repeats those comparisons at resize geometries.

1. [ ] **Trusted startup and one brokered pane (assigned 2026-10-11).**
   Add opt-in/request selection and
   a minimal private SEND broker, keep mux in its outer group, create one pane
   from the connection's initial cwd and return the three owned handles. Reuse
   the existing renderer/profile and disable unsupported side channels. Qualify
   rejection, launch/reply failure, root fault/disconnect, queued requests during
   close and all-group cleanup in interactive QEMU/GDB. Local/ordinary remote
   startup stays unchanged. Cap this slice at one pane and explicitly refuse
   split commands/admission. No resize, split cwd-transfer or kernel mechanism.
   **After this task the owner can:** open one remote mux pane on horse at its
   initial terminal dimensions and close it with honest cleanup/results.
2. [ ] **Splits, cwd and bounded presentation (assigned 2026-10-11).**
   Complete incremental directory-chain
   transfer and reuse BSP/equal layouts, eight slots, digit/arrow focus, history
   and close confirmation. Add resumable try-write rendering/fair waits before
   claiming responsiveness under output backpressure. Preserve cwd rights,
   busy-pane fallback, input ordering and slot limits through repeated close/split.
   **After this task the owner can:** use splits, inherited cwd and digit focus
   in one wide remote session for the JVM work, with a bounded slow-host path.
3. [ ] **Live resize and end-to-end qualification (unassigned).**
   Add host-window observation,
   negotiated bounded resize/applied geometry and native session propagation.
   Check shrink/grow, tiny windows, full-screen editors, RGB/UTF-8, busy/stalled
   panes, two mux plus two plain sessions, reverse mode and disconnect/fault with
   pending HOST cleanup. Repeat matched plain/mux latency, bytes, throughput and
   resident/resize peaks with existing tools. Owner
   runs the actual horse/native-PXE check; keep it distinct from nested QEMU.
   **After this task the owner can:** resize the remote window while keeping
   panes and running jobs, with measured costs and documented limits.

## Accepted owner decisions (2026-10-11; not implemented)

1. **Composition and lifetime — accepted: guest VT composition with the trusted
   daemon pane broker.** Keep the current renderer and root containment, separate
   pane groups and cleanup-complete FINAL/admission semantics.
2. **Entry, geometry and admission — accepted: explicit `--mux`, separate Remote
   opt-in, initial HELLO geometry first, live resize in task 3; two mux connections
   within four total.** Packaged opt-in stays false until explicitly configured;
   ordinary/machine sessions keep their current path. The count fits the native
   32-interest budget, including unreclaimed groups.
3. **First interaction profile — accepted: existing Pyxis colours/UTF-8 and
   keyboard controls, host selection/ordinary paste only.** No native mouse or
   clipboard, cross-pane xfer/drop automation or machine command sequencing;
   stock `xfer` refuses explicitly with the propagated profile; other unsupported
   OSC emitters are safely consumed. Add those only with pane-scoped
   authority, routing and a separate assignment.
