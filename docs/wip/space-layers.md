# Graphics and terminal layers in a space

Status: **kernel layer switching implemented, 2026-10-08; native qualification pending.**
The owner accepted the contract in PR #502. The consumer update merged in
PR #507; kernel switching now implements the agreed behavior. Remaining work
is qualification and reference closure, not another design proposal.

## Today

Each space owns a local TTY and at most one
[graphics session](../interfaces/graphics.md). First PRESENT selects graphics;
Super+Up/Down then chooses the layer until RELEASE or exit ends the session.
The TTY keeps receiving and rasterizing output while graphics is shown.
Input capture stays acquired while hidden, but text is routed to the console.

## Idea

The user chooses the visible layer instead. In a space with a presented graphics
session, Super+Up shows the graphical program and Super+Down shows its terminal.
The space's tab ends with `+` while the graphical program is shown and `−` while
the terminal is shown; a space without a graphics session has no marker.

This is not a nested space. Both layers belong to the same space, processes and
grants. It lets Quake run while its terminal output is visible a keypress away,
and it is how graphical programs coexist with the planned
[terminal multiplexer](terminal-applications.md): they sit on the layer above
it instead of in a pane.

## Owner decisions

Accepted 2026-10-07:

1. **A hidden program keeps running.** Switching to the terminal only takes
   keyboard and pointer input away from it, releasing held keys and buttons as a
   focus loss does. Its output stays live. Input goes to the terminal layer.
2. **The marker** is `+` for the graphical layer and `−` for the terminal.
3. **The user's choice wins.** After Super+Down, the program's PRESENT does not
   bring it back; only Super+Up, or the session ending, changes the layer. A
   graphics session newly started from the terminal is shown, because the user
   just launched it.

Accepted 2026-10-08:

4. **An unselected space keeps running too.** Quake, Doom and Mandelbrot do not
   pause on a space switch. Input focus controls input eligibility, not game
   time or rendering eligibility.
5. **Terminal typing uses the normal queue.** While graphics is hidden, ordinary
   text enters the existing 4 KiB console queue. The shell still waits for its
   foreground job; it does not open a second prompt. If no program reads, bytes
   wait for a later reader. Returning to graphics clears unread terminal bytes.
   Ctrl+C retains the terminal's existing foreground-interrupt behavior.

## Source investigation

Inspected Pyxis `50e18a5`, its pinned userland `2430567f` and ports `03b3ae8e`.
These are source findings, not runtime qualification:

- [Display sessions](../../kernel/object/display.c) have an owner, mapping and
  `visible` flag. Every PRESENT sets `visible`; acquisition leaves it false.
  [Presentation](../../kernel/space.c) snapshots only the active space's selected
  graphics backing, or uses its TTY. It never copies both surfaces.
- [Keyboard](../../kernel/object/keyboard.c) and
  [pointer](../../kernel/object/pointer.c) capture are independent of display
  ownership. Focus changes discard pending events and reset accepted presses.
  Keyboard acquisition and release also discard console bytes today; that must
  change when hidden graphics leaves the console as the input destination.
- Quake and Doom use keyboard focus to block their game loops and exclude paused
  time. Quake also lets pointer control events alter that same pause state.
  Mandelbrot stops rendering on focus loss. Userland's `mousetest` also blocks
  its loop on focus loss and belongs in the consumer update.
- The shell waits for foreground completion and an armed Ctrl+C interrupt,
  without reading ordinary terminal bytes. Text routing while graphics is
  hidden therefore makes Ctrl+C available through that existing authority.
- The bar has one character cell of padding on each side of the title. Its font
  has no Unicode minus glyph; the marker can be drawn directly in its cell.

## Agreed contract

### Session and layer state

Keep graphics ownership, whether the session has ever presented, and the user's
layer choice distinct. All mutations remain BSP-owned. Layer switching allocates
nothing and neither releases capture nor removes the graphics mapping.

| State or action | Selected surface and marker | Input behavior |
| --- | --- | --- |
| No graphics session | TTY, no marker | Existing console/capture routing |
| ACQUIRE, before first PRESENT | TTY, no marker | Existing console/capture routing |
| First successful PRESENT of this session | Graphics, `+` | Existing acquired input sessions receive focus if the space is selected |
| Super+Down after first PRESENT | TTY, `−` | Console text; captured keyboard and pointer lose focus |
| Further PRESENT while hidden | TTY, `−` | Unchanged |
| Super+Up after first PRESENT | Graphics, `+` | Restore normal capture routing; discard unread console bytes |
| REPLACE | Preserve the layer and marker | Unchanged |
| RELEASE or owner exit | TTY, no marker | Resume existing routing for any surviving independent capture |

The first PRESENT is the observable start of a graphical session; acquisition
alone does not show a blank layer. A new session gets this first-PRESENT behavior
even if the previous one ended while hidden. The kernel does not infer whether
a launch was interactive. Starting or presenting in another space never selects
that space, but records its layer choice for the next visit.

Switching spaces preserves each space's layer choice. An unselected space gets
no physical input and no surface copy. Its programs remain runnable under the
ordinary scheduler. Returning to a hidden session restores terminal text routing.
RELEASE does not release separately owned keyboard or pointer sessions; normal
owner exit still tears down all sessions through existing process cleanup.

Super+Up/Down follows Super+Left/Right's modifier and held-arrow rules: either
Super key, no Shift/Control/Alt, one action per press, and consumed arrow repeats
and releases even if Super is released first. With no presented session the
shortcut is consumed without changing state. Repeating the current layer choice
does not reset input or discard terminal bytes.

### Focus and terminal input

Keep the public keyboard and pointer event layouts and focus flags. Focus means
eligibility for captured input. A layer transition sends the existing focus
notification when that eligibility changes, dropping stale captured events and
clearing held keys, buttons and motion. No separate space-selection event is
needed because programs keep running in both hidden and unselected states.

Quake and Doom clear held controls on focus/reset events, continue polling those
events, and advance game time without focus-based pauses. Their initial focus
waits are removed too, so starting in another space does not block startup. Mandelbrot
finishes pending rendering without focus and idles normally once complete;
`mousetest` continues its ordinary update loop. Input controls remain disabled
without focus. Pointer notifications must not change Quake's run/time policy.
These are application changes; the kernel does not force other programs to
render or prohibit an application's own explicit pause.

While a presented session is hidden, terminal text routing overrides an acquired
keyboard session. Acquisition/release of that capture cannot steal text routing
or clear the console queue. Captured ownership remains exclusive and blocking
reads may wait for focus/reset notifications; terminal typing never enters the
captured event queue. The pointer supplies no terminal input in this milestone.

Each routing change resets accepted held input, requiring fresh key/button
presses in the new destination. Console queue overflow retains the existing
`CALL_INPUT_LOST` behavior. Device loss must invalidate console input when it is
the destination, even while a hidden program retains keyboard ownership.
The queue-clear rule also applies on the first PRESENT when routing changes
from terminal text to graphics capture. RELEASE or exit while hidden preserves
queued text if routing remains terminal text; restoring a surviving raw capture
discards unread text. Decide preservation from the routing destination before
and after each operation: process cleanup releases keyboard before display,
and must preserve hidden-layer text through that order. Already-read bytes
cannot be recalled. Output stays live in the TTY regardless of the chosen layer.

### Bar, resize and presentation

Use the existing right padding cell at the end of each tab for `+` or a drawn
minus. Reserve that cell even without a marker, preserving current equal tab
widths, title centering, clipping, selection underline and scrolling. An inactive
tab's marker records its layer choice, not what the physical screen shows.
Draw the marker even when the title cannot fit, provided one whole marker cell
fits inside the tab; otherwise omit it without drawing outside the tab.

Physical resizing retains the existing all-space TTY transaction, fixed acquired
mapping contract and RESIZED notification. Hidden or unselected Doom adapts at
its continuing frame boundaries; Mandelbrot uses its existing resize wait and
render checkpoints. Quake and `mousetest` retain their current fixed mappings
and existing clipping behavior. REPLACE preserves hidden state; resizing must
not manufacture a focus transition or bring graphics forward.

Only the chosen surface of the active space is snapshotted and copied. Hidden
graphics retains its existing session backing without taking a presenter lease
or adding a pixel copy. Application rendering still costs CPU and memory
bandwidth; hiding does not promise a performance saving. An already-snapshotted
frame may finish after a transition, as today. Mapping retirement keeps the
existing presenter-reference and execution-group cleanup contracts.

## Tasks

1. [x] Investigate current code and write this proposal, recording accepted
   decisions separately from the remaining proposed contract.
2. [x] Proposal accepted 2026-10-08. Update Quake/Doom in ports and
   Mandelbrot/`mousetest` in userland so focus
   loss resets input without pausing execution or game time. Publish focused
   dependency PRs before a Pyxis integration PR updates their pins; merge the
   dependencies first. Build against the existing SDK and check focus loss,
   fresh presses and inactive startup interactively in QEMU.
3. [x] Implement session layer state, Super+Up/Down, capture/text routing,
   queue/loss handling, marker drawing and chosen-surface presentation in Pyxis.
   Include the published consumer pins. Update the
   [graphics](../interfaces/graphics.md), [keyboard](../devices/keyboard.md),
   [mouse](../devices/mouse.md), [shell](../userland/shell.md) and
   [space bar](../userland/init.md#space-bar) references in this PR. Explain
   hidden-layer Ctrl+C as the shell's existing foreground-interrupt escape hatch.
4. [ ] Qualify interactively in QEMU: repeated PRESENT after hiding; held
   keys/buttons and shortcut releases; queued terminal text and Ctrl+C;
   acquisition without presentation; hidden RELEASE/exit and fresh acquisition;
   switching away/back in either layer; continuing game time; live resize and
   hidden REPLACE; narrow tabs with titles touching the marker. Use debugger
   inspection for hidden-frame leases
   and backing lifetime. Check the exact submitted revisions' existing CI.
   Ask the owner for a native ThinkPad check with Quake and record its result.
5. [ ] After implementation and qualification, move the implemented contract to
   the interface references, remove this completed worklist and update inbound
   links. Record any accepted remaining limits in technical debt.

## Kernel task qualification

The ordinary `make -j16 image QUAKE_DATA=/quake-data` build passed using the
existing LLVM builder. Dependencies stayed at the merged pins: ports `8449065`
and userland `d86f9b7`; the zlib and C++ startup changes were preserved.

Interactive QEMU 10.2.2 used KVM, four CPUs, 512 MiB and a writable HOST export
restricted to the task's qualification directory. No network device was attached.
Standard VGA/Bochs checked layer/input behavior. GTK with VirtIO GPU checked
live geometry and narrow tabs. These were nested-VM checks:

- Hidden Quake retained its mapping, continued frames with both capture focus
  flags clear, and had only its session backing reference. Three console bytes
  survived repeated Super+Down and were cleared by Super+Up. Switching away and
  back preserved the hidden choice. A fresh session selected graphics again.
- Hidden-layer Ctrl+C ended Quake through the shell's existing armed interrupt.
  An `echo` queued after that interrupt executed after cleanup and wrote
  `survived` to HOST. Display, keyboard and pointer owners were then null and
  the marker state ended. This exercised keyboard-before-display teardown.
- Hidden Doom replaced its mapping on a real VirtIO resize to an 800x573
  destination, keeping the terminal layer selected. Its new buffer was 800x541.
  Hidden Mandelbrot similarly adapted to 640x453, retaining a 640x421 buffer
  with no presenter lease. Neither resize changed the input layer.
- At 320x213, a 36-character title was clipped beside both markers; widths and
  the title underline stayed intact. Ctrl+Super+Down was suppressed, while
  either Super key selected the terminal normally.
- `mousetest` accepted a button press, cleared it when hidden, withheld that
  still-held button after Super+Up and motion, and accepted a release/fresh
  press. Escape returned `mousetest` and Mandelbrot to their shells. Layer
  shortcuts without a session left the console queue empty.

Acquired-but-never-presented state, explicit repeated PRESENT while hidden,
independent surviving capture on DISPLAY_RELEASE, shortcut releases after Super
and device/queue-loss propagation were source-inspected. Those cases are not
claimed as additional runtime coverage. The native ThinkPad Quake check remains
task 4. All task-owned QEMU, GDB and virtiofsd processes are closed.

### Performance samples

The existing `quake +timedemo demo1` workload rendered 969 frames per sample.
Both images used the same standard VGA/Bochs, 1280x800 destination, KVM/four-CPU/
512-MiB configuration and HOST export, without profiling. Baseline source was
Pyxis `38684a9` with the pins above; after samples used this branch's uncommitted
implementation before the documentation/commit handoff. Kernel ELF identities:

- Baseline: `8d2490dff4707d3bd618403875afae5afb3e8667aa76f3833387f493d947643d`.
- After: `3c5439c71c231e4fcbeac4c4550a28110e6bee2ee8db2ad322f04da7589ae830`.

| Series | FPS samples | Context |
| --- | --- | --- |
| Baseline | 1022.7, 1036.2, 1049.5 | First startup timedemo, then two console repeats |
| After initial | 1179.5, 1162.4 | Startup timedemo and console repeat |
| After during input qualification | 700.0 | Followed intervening menu/layer/debugger checks |
| After fresh starts | 1560.6, 1525.7, 1562.9 | Three fresh launches after the variable initial series |

The short samples, different launch history and shared-host variation do
not establish a stable speedup or regression. No performance improvement is
claimed, and these figures are not native qualification. Hiding adds no graphics
copy or allocation; it does not remove application rendering work.

## Kernel task handoff

Branch: `space/layer-switching`, based on merged Pyxis `38684a9`.
Proposal acceptance was folded into task 2. The merged consumer PRs are:

- [Ports PR #56](https://git.internal/PyxisOS/pyxis-ports/pulls/56),
  `64e0067c6de7e1557e6669b381d96563563ca5ea`.
- [Userland PR #154](https://git.internal/PyxisOS/pyxis-userland/pulls/154),
  `30bfe0df2c0f1d32edacc8d297b07f097b13c723`.

Their merged dependency mains, including zlib and C++ startup, are already
pinned in this branch. No gitlinks were changed for the kernel task. PR #507's
stale Quake/Doom inactive-pause descriptions are corrected in their user guides;
PR #502's CPU-cost, hidden Ctrl+C and narrow-marker notes are covered by the
references and qualification above. Tasks 4 and 5 remain: owner native check,
any remaining edge qualification, then concise reference closure.
