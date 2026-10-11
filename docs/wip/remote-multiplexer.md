# Multiplexer over remote terminals

The owner accepted this contract on 2026-10-11 and assigned tasks 1–2 together.
Tasks 1–2 are merged (#728 and userland #218). Task 3 was owner-assigned
on 2026-10-11 and is implemented for review. The accepted route is guest VT composition
with a private pane broker in the trusted remote daemon. Implementation and
qualification have separate evidence in the
[task 1–2 experiment record](../development/experiments/remote-multiplexer-task12.md).
See also the [task 3 resize qualification](../development/experiments/remote-multiplexer-resize.md).
Native horse/PXE qualification remains owner work.

## Startup and authority

Set `remote_multiplexer = true` on the Remote space and connect with
`pyxis-remote --mux HOST PORT`. Packaged live Remote enables the flag (owner
accepted, 2026-10-11); installed defaults have no Remote space, and owner-authored
Remote entries can set it. Ordinary and machine connections retain their shell
path; unavailable mux requests fail without fallback. `--mux` rejects `--machine`,
`--no-shell-echo` and `--download-dir`. HELLO option bit 1 selects mux; bit 0
selects the ordinary quiet shell and cannot be combined with it.

Trusted startup preserves a distinct `remote_mux_terminal` CREATE grant through
init, shell and session to the daemon. It does not enable the local
`multiplexer` option, terminal pointer control or clipboard stores. The daemon
loads `bin://mux.pxe` with the configured tab width; installed mux stays outside
the minimal rescue set. Initial geometry comes from host character dimensions
(TIOCGWINSZ or explicit dimensions), within 1–512 columns and 1–256 rows. A pixel
resolution such as 3440×1440 does not determine terminal geometry.

Mux stays in the outer remote execution group. It receives streams, a private
SEND-only `pane_broker` caller, its own bound LAUNCH-only launcher, and the
existing remote resource allowlist, roots, namespace and initial environment.
The daemon retains terminal CREATE and unbound CREATE_GROUP. Neither mux nor
pane shells receive physical keyboard, DRAW, graphical pointer, terminal-pointer
CONTROL or clipboard-store authority. Optional screen capture follows the
existing Remote-space policy. `launch` delegation remains explicit; live-only
`remote_power` permits root-shell power commands, while installed defaults omit
it. The unauthenticated, unencrypted trusted-LAN exposure is unchanged: any peer
reaching an enabled service can request the configured program/filesystem grants
and bounded pane creation. Shared roots are not filesystem sandboxes.

## Private broker and cleanup

The [broker client](../../userspace/common/pane_broker.c) and
[daemon](../../userspace/remote-terminal/session.c) use private SEND transactions:

- Each connection has one daemon-owned receiver. Mux creates its own reply and
  cwd-bootstrap receivers and transfers only SEND callers. BEGIN carries those
  two grants; CHUNK carries at most four directory grants; successful COMMIT
  returns attachment, process WAIT and group CONTROL|WAIT in three grants.
  ACCEPT is acknowledged before mux publishes the pane. SEND receipt closure
  releases delivery bookkeeping; it is not a reply or execution acknowledgment.
- The daemon captures filtered initial environment, selected roots, namespace
  and resource masks once per mux connection and reuses them for every pane.
  The initial/fallback cwd is the selected `tmp://` capability. At an idle
  prompt, the focused root shell exports a fresh owned cwd chain directly to
  mux. Its existing one-second snapshot budget can fall back before broker
  publication. Actual directory rights survive transfer and synchronous launch
  capture; descriptions are never reopened as paths.
- The daemon processes at most four broker records per service pass and never
  waits for another cwd chunk. Complete directory/path storage is bounded by
  `LAUNCH_CAPTURE_MAX_SIZE`; each transaction currently has an absolute
  ten-second budget. Clock readings refresh per record and immediately before
  launch. Synchronous image capture/HOST work can still delay all clients.
- Transaction IDs never wrap or reuse. CANCEL names the original transaction;
  late records cannot attach a reply to a reused pane. Known failures before
  publication can refuse a split. Uncertain launch or result/acceptance failure
  cancels the tracked group and ends mux with `CALL_OUTCOME_UNKNOWN`; it never
  authorizes a new split retry. Expiry or clock failure after successful launch
  also preserves this uncertain outcome.
- Each of eight daemon pane slots counts incomplete creation and every created
  group. Cancelling an incomplete record releases it if no group exists; created,
  unpublished, response-failed and closing groups retain their slot until
  WAIT_COMPLETE. The daemon retains supervision before launch and after result
  delivery. Mux observes root exit/fault, terminates its
  descendants and drains final pane output; root process result and group
  cleanup completion are distinct.
- Disconnect, outer root exit/fault and daemon shutdown seal broker admission
  first, discard incomplete/queued transactions and terminate every tracked
  group. FINAL and connection-slot reuse require outer and all pane cleanup.
  The five-second output/transport drain limit does not bound attributed HOST
  cleanup, which can delay reuse indefinitely. EOF alone proves no cleanup.

Admission remains four total connections, including closing ones, with at most
two mux connections. The maximum daemon wait set is 13 ordinary interests plus
two × (eight pane groups + one broker receiver) = 31, below the native bound of
32. Pane process observation stays in mux; no daemon pane-process wait or new
kernel mechanism is added. Reverse mode remains one connection.

## Input and presentation

The existing [mux controls and layouts](../userland/multiplexer.md#controls-and-layout)
provide eight stable pane digits, arrow focus, BSP/equal layouts, history and
close confirmation. Ctrl+B and double Ctrl+B remain byte-based; Ctrl+C follows
the focused pane's interrupt rules, while Ctrl+] closes the host connection.
Accepted pending input retains its original pane across focus changes. The
256-byte outer/per-pane staging and existing terminal/server/client queues stay
bounded; saturated input can hold later controls behind accepted bytes.

`TERM=pyxis` and the existing
[colours and single-cell UTF-8 repertoire](../userland/terminal.md#tty-output-controls)
remain authoritative. Pane alternate screens and scroll regions stay inside the
emulator; only composed cells reach the host font. Unsupported scalars/invalid
bytes use replacement; there is no combining/wide-character claim. The host
client owns its alternate-screen presentation and restores modes/cursor on
handled exit or failure.

Remote mux is keyboard-only. Host selection and ordinary paste are host
operations. Bracketed-paste delimiters are consumed locally, while body bytes
reach ordinary input: newlines can submit commands and control bytes can enter
the mux prefix parser. Native safe-Paste semantics do not apply. Prompt-based
file drops, transfer interception and machine command sequencing are disabled.
`PYXIS_REMOTE_MUX=1` propagates to panes/commands as an informational execution
profile; stock `xfer` refuses before protocol emission. Other OSC bodies are
safely discarded. Session FINAL reports outer mux outcome and all-group cleanup,
without merging per-pane completion numbers.

The renderer resumes CONSOLE try-writes from a fixed 8 KiB pending buffer,
preserving partial CSI/UTF-8 suffixes and cell snapshots. Each turn limits output
to 8 KiB and difference scanning to 2,048 cells, services pane input/drains, and waits for
WRITABLE when stalled. New presentation coalesces behind an active frame. The
previous-frame state advances only after each complete glyph enters the outer
queue; this is queue acceptance, not host-display acknowledgment. History
retains 1,024 rows with 12-byte cells, without an unbounded output/history queue
or a physical-memory capacity promise. Frame composition and reset still visit
the bounded whole viewport; the encoder budgets do not bound all CPU work to
2,048 cells.

## Tasks

1. [x] **Trusted startup and brokered pane.** Explicit opt-in/request, contained
   mux startup, daemon-owned pane creation/supervision, unsupported-side-channel
   refusal and all-group close/FINAL accounting are implemented.
2. [x] **Splits, cwd and bounded presentation.** Incremental capability-chain
   transfer, eight slots, existing controls/history/layouts and resumable
   rendering are implemented. Qualification and measured limits are in the
   linked experiment record.
3. [x] **Live resize and end-to-end qualification — assigned 2026-10-11.** Host-window
   observation, bounded coalesced resize/applied-geometry frames and existing
   native RESIZED propagation are implemented. Shrink retains hidden panes and
   growth restores layout. Nested QEMU qualification covers tiny/wide geometry,
   Kilo/Neovim, RGB/UTF-8, busy/stalled panes, two mux plus two plain sessions,
   reverse mode and pending HOST cleanup. Matched costs and allocation peaks are
   in the task 3 record. The owner runs horse/native-PXE qualification separately.
   **After this task the owner can:** resize a remote mux window while keeping
   its panes and jobs, with measured costs and documented failure cleanup.

## Accepted task 3 choices (2026-10-11)

- Live resize is negotiated for `--mux` only. A HELLO option selects resize and
  request/applied frames carry a request ID and dimensions. One request remains
  outstanding while newer host sizes coalesce; ordinary/machine sessions stay
  unchanged.
- Explicit `--columns`/`--rows` limit subsequent geometry. Otherwise it follows
  actual host character dimensions within 512×256. Transient zero dimensions are
  ignored; tiny windows retain hidden panes for later growth.

Live remote resize is implemented for review; reattachment is unsupported. Pane-aware transfer, native
clipboard and pointer transport require a separate owner assignment.
