# Terminal multiplexer

`mux`, from the `mux` package, presents independent native terminal sessions in
one window.
Each pane starts an ordinary shell with its own execution group, terminal
state, cursor, dimensions and colored scrollback. This is userspace terminal
emulation over the [native TTY subset](terminal.md#tty-output-controls).

## Startup

The live image includes a **Multiplexer** space immediately after Development.
It shares Development's flags, grants and writable roots, except Development
retains network configuration ownership. Its `boot://init-readonly` startup skips
network setup without restricting those roots; panes use the network after
Development configures it. Installed configuration is unchanged.

For other spaces, add `multiplexer = true` in `userspace/config/live.lua` and
rebuild the image, or in an installed system's `system://config/boot.lua` and
reboot. The flag defaults to false:

```lua
{ name = "development", title = "Development", init = "boot://init",
  network = true, launch = true, multiplexer = true,
  roots = { home = "read-write" } },
```

Trusted init receives a distinct `mux_terminal` creation grant. The trusted
shell/session handoff preserves it; after any configured service setup the
session launcher selects mux instead of the shell, forwarding it as `terminal`
and forwarding LAUNCH on mux's launcher, which permits scoped group creation.
Ordinary shells receive no terminal creation grant.
Remote startup uses a separate `remote_multiplexer = true` opt-in and
`pyxis-remote --mux`, with pane creation brokered by the trusted daemon. Packaged
live Remote enables it (owner accepted, 2026-10-11); plain remote clients still
select the shell. Installed defaults have no Remote space; owner-authored Remote
entries can set the same flag. See the [remote mux contract](remote-terminal.md#remote-multiplexer).
Direct ordinary command launch lacks the administrative grants local mux requires.
Missing resources or failed startup produce
a diagnostic, with no automatic restart or fallback shell.

The configured terminal tab width reaches mux through `--tab-width 1..32` and
is applied to every newly created pane. Session and the remote daemon start
it by command search, so it comes from the installed `mux` package, or from the
archive of a `DISTRIBUTION=full` live image. It stays outside the core archive. Rescue startup keeps the ordinary shell.
The first shell inherits the space's starting directory. On split, the focused
root shell exports an owned snapshot of its real shared-libc directory chain
and optional description through a private userland endpoint. The new shell
receives that chain with its rights preserved; no printed text is parsed or path
description reopened. Later `cd` operations remain independent. Busy, non-shell,
unavailable or timed-out panes quietly fall back to the space start directory.
Each new pane receives explicitly selected environment entries from the space's
profile; service controllers remain private to the trusted manager.
Filesystem grants are shared explicitly, so panes are not filesystem sandboxes.
`launch = true` independently controls ordinary child-launch delegation.

## Controls and layout

Press Ctrl+B, release it, then enter a command:

| Command | Behavior |
| --- | --- |
| `%` / `"` | Split left/right or top/bottom; focus the new shell |
| Arrow | Focus a pane in that direction |
| `1`–`8` | Focus the numbered pane shown in its title; unused/out-of-range digits do nothing |
| `b` | Restore the recursive BSP layout |
| `e` | Arrange equal strips along the most recent split axis |
| `[` | Browse focused pane scrollback |
| `z` | Toggle zoom: show the focused pane full screen, then restore the layout |
| `x` | Close focused pane; confirm `y` to terminate running work, `n`/Escape cancels |
| Ctrl+B | Forward one literal Ctrl+B byte |
| Escape | Cancel the prefix |

BSP splitting replaces the focused leaf with two children. Splits use half of
the available extent, adjusted when nested children's minimum sizes require it.
Equal layout keeps the BSP tree for later restoration and orders panes by that
tree's leaves. There are at most eight panes; each split needs at least 12
columns and four content rows per pane, plus a heading and the split dividers.
Insufficient room, the pane limit and failed session creation preserve existing
panes. Failure to allocate resized presentation storage stops mux with a
diagnostic and requests group termination. A global footer shows layout and mode.

Zoom (`z`) uses the same focused-only presentation without touching the layout
tree, so unzoom restores the exact layout. The footer reads `(zoomed)`. Hidden
panes keep running and draining at their previous size; the zoomed pane
receives a normal session resize to the full area, and unzoom resizes it back.
Focus commands (arrows cycle, digits select) move the zoom to the newly
focused pane. A successful split, `b`/`e` and closing a pane unzoom first; a
refused split leaves the zoom. `z` with one pane only shows a notice. Remote
mux has the same behavior, as it shares this presentation.

If display shrink prevents the full layout, only the focused pane is shown.
Other sessions keep running and draining at their previous dimensions. When
even the focused pane does not fit, its session retains at least 12x4 content
and the outer view clips it; an off-view cursor is hidden. Arrows
then cycle through the retained panes; growing the display restores the layout.
Split admission uses the current geometry snapshot, independently of cached
render buffers. An empty outer view retains sessions and defers presentation.
Visible panes receive [session resize](terminal-sessions.md#resize), with the
existing SIZE generation and RESIZED event. Apps that observe resize redraw;
other apps retain their own existing behavior. The presentation uses at most
512 columns and 258 rows, reflecting the native session bounds plus headings.

Remote mux observes host character-window changes within 512×256. Explicit
host-client dimensions are ceilings; tiny windows clip the retained panes and
growth restores the layout. The daemon acknowledges outer native resize before
later terminal drains; that metadata does not acknowledge pane redraw or host
display. Old queued output and partial CSI/UTF-8 suffixes continue in order at
the current geometry. Allocation failure preserves its current screen or frame;
resize across panes is not atomic. A later failure can follow earlier updates
and ends mux with group cleanup.
See [remote resize qualification](../development/experiments/remote-multiplexer-resize.md)
for QEMU checks and the owner-reported native pane Neovim, TCC and live resize results.

History retains the rows scrolled off each pane, including colors and original
widths, without reflow, within a memory budget per pane (not a row count): one 512th
of the allocator's capacity, at least 8 MiB, or `terminal.scrollback_bytes` in the
[session configuration](session-configuration.md). Rows are allocated as they scroll
in and the oldest are dropped when the budget is reached; the history view's status
line then says how many were dropped and the byte limit. Page Up/Down moves a screenful; Up/Down moves
one row; Home/End selects oldest history/live screen. Escape or `q` returns to
live input. Output continues while browsing; existing text stays anchored until
eviction of old rows. Screen erasure and resize cropping do not create history.

## Authority, input and exit

Local mux owns attachments, RESIZE, HANGUP and group supervision. Remote mux
receives those handles from a daemon which retains independent group supervision
through cleanup. Each root shell receives application streams, interrupt arming, its separate completion
event grant and a group-bound launcher. Commands receive existing explicitly
delegated resources. Local pane shells receive the same explicitly held display,
keyboard, pointer and power grants as ordinary local session shells. Remote panes
receive the existing remote grants, with no local display/keyboard/pointer or
clipboard-store authority. Local graphical programs launched from a pane draw on the shared space's
[graphics layer](space-layers.md), above mux; they never draw inside the pane.
Pane shells receive no session creation or group-supervision grant.

The cwd channel belongs only to mux and each root shell; ordinary children do
not inherit it. Each endpoint receiver stays with its creating process, with
SEND-only clients delegated privately. The shell serves snapshots only while
editing its prompt. One absolute one-second budget covers the complete chunked
transfer, including chains longer than the four-grant message bound. Partial
transfers release their grants and use the fallback; no old cwd snapshot is
cached. The complete owned snapshot remains alive through synchronous launch
capture and is then released. Remote mux forwards the complete chain through
its private daemon broker, which retains the connection's initial policy. An
uncertain broker publication ends mux and cleans tracked groups; it never
falls back or retries as another split. No kernel interface or directory
authority is added.

Ordinary input remains byte-exact. Mux holds passthrough on its outer input so
Ctrl+C reaches the focused session, whose ordinary shell interruption rules
apply. Prefixes, history and confirmation input are consumed by mux. Each pane
has 256 bytes of pending input, and outer input has a 256-byte staging buffer;
accepted suffixes retain their pane and order across focus changes. Queue
backpressure preserves ordinary bytes. Prefix commands can proceed when they
are the next staged input, but a full queue can hold later commands behind
ordinary staged bytes. Explicit closure discards pending input for that pane.

Root-shell exit or fault requests termination of remaining descendants. Final
output drains while cleanup proceeds, then the pane retains its text and status
until dismissed. A shell `session` successor therefore cannot survive the
original pane root's exit. Confirmed closure removes the pane after output EOF and group
cleanup; siblings reclaim its layout region. Closing the last pane ends mux.
Output grants intentionally delegated outside a pane group can delay output
EOF independently of group completion. Local mux exit/fault drops the final
controlling grants and requests termination of every remaining group. The remote daemon seals its broker and explicitly
terminates every tracked pane group before outer/all-pane completion permits
FINAL or connection reuse. Group cleanup has no fixed deadline: published HOST
work can delay it. See [execution groups](../interfaces/execution-groups.md).

Every turn drains a bounded number of records from each pane. Rendering resumes
changed colored-cell output through CONSOLE try-write, with a fixed 8 KiB pending
buffer, an 8 KiB output budget and a 2,048-cell scan budget per turn. Partial
CSI/UTF-8 suffixes and cell snapshots survive short writes; previous-frame cells
advance only when complete glyphs enter the outer queue. New presentation
coalesces behind an active frame while input and pane drains continue. This
records queue acceptance, not host-display acknowledgment. Whole-viewport frame
composition and reset still visit all cells; the scan budget does not bound that
CPU work. Pane escape sequences
are interpreted inside the emulator; unsupported OSC bodies are discarded.
Up to nineteen readiness interests cover outer input/output, the optional
terminal spatial queue and eight output/lifecycle pairs, without an idle
polling loop.
The native wait bound is independently 32 interests; a compile-time check
ensures the eight-pane budget fits it. Per-interest copies use about 40 bytes
of kernel stack. The readiness request fits the existing 4,928-byte reserved
task area, sized by HOST; increasing this bound from 17 to 32 does not grow it.

Multiple windows, ratio adjustment, detach/reattach, Unicode widths and a broader
VT escape set remain deferred.

## Full-screen programs and history

Each pane terminal implements the kernel TTY's
[sequence table](terminal.md#tty-output-controls), including the alternate
screen, scroll regions and saved cursor.

Each live, alternate and history cell is 12 bytes, retaining supported Unicode
scalars, tagged colours and attributes. UTF-8 follows the shared terminal
repertoire; Copy exports scalars, while safe Paste remains ASCII-only. Each history row is allocated when it scrolls off, at the width it had, and the
ring of rows doubles as it fills, up to the pane's budget; a row that cannot be stored
is counted as dropped and output is never refused. Creation and resize allocate new backing before
publication and leave the old state intact on failure. The frame and previous
frame use the same cells and compare fields rather than padding. At 160 columns,
46 pane rows and a 160×48 outer frame, one pane's two screens, history and width
array plus the frame pair request 2,335,232 bytes, excluding allocator overhead.

History follows the primary screen only:

- **Alternate screen:** it keeps no history. Rows scrolled off it are
  discarded, and while it is live, wheel and keyboard browsing and selection
  stay on the live screen. Entering it ends browsing. Leaving it returns the
  shell's screen and its whole history unchanged.
- **Scroll regions:** on the primary screen, rows leaving a region that starts
  at the top row enter history; rows leaving a region that starts lower are
  discarded. Scrolling anything but the whole screen clears the selection,
  whose rows are counted against history.

## Local pointer input

Configured local startup gives mux a separate `terminal_pointer` CONTROL grant
through its trusted init/shell/session chain. The
[terminal controller contract](../interfaces/pointer.md#terminal-control-and-selection)
is independent of graphical pointer INPUT authority. Acquisition is exclusive
and process-owned. Pane shells retain graphics/pointer grants for their games but
receive no outer terminal control. Mux adds the spatial queue to its native
`wait_many` interests, with no idle polling loop. Remote mux remains keyboard-only.

A content or heading click focuses that visible pane; only content participates
in linear cell selection. A left press clears the old selection and records a
pending anchor. Selection starts only on a held move to a different cell; a
click or movement within the starting cell leaves none. A one-cell displacement
includes both endpoint cells. Left drag stays anchored to the starting pane and
clamps to its visible content, excluding headings, dividers, footer, other panes
and clipped cells. Highlighting changes rendered frame copies, not retained
live/history cells. Selected glyph mutation or row eviction clears selection;
colours and unrelated output preserve it. Unchanged selected rows moving with
output remain selected only while visible. Explicit history movement, layout
change, closure and outer resize clear selection and synchronize a new terminal
view identity before more spatial input is consumed. Hidden graphics/focus
loss suspends input and cancels an active drag; unchanged completed selection
may remain.

Wheel over content moves that pane's history three rows per detent without
changing keyboard focus. Negative viewport-up scroll moves older, positive
viewport-down scroll moves toward live, with
bounded endpoints. Reaching the newest endpoint returns to ordinary pane input.
The existing prefix/keyboard history controls still work. Drag autoscroll and
selection across off-view history remain deferred. Controller release/exit
restores kernel local-TTY selection and cursor handling.

## Clipboard

Exact Ctrl+Shift+C/V copies/pastes the local layer; Super+Shift+C/V selects the
shared layer. Trusted startup explicitly delegates separate clipboard grants to
mux; pane children inherit neither. Actions arrive on the terminal spatial queue
with native operation/view identity, keeping them outside the Ctrl+B byte parser.
Copy exports the completed selection's pane as owned UTF-8 text;
physical rows are LF-joined and trailing spaces trimmed. The encoded byte limit, missing,
pending or dragging selections refuse without replacing the previous item.
A plain click clears selection, including when it focuses another pane.

Paste targets the focused live pane and its opted-in stock libterm receiver.
Outer staged bytes, partial key/prefix/confirmation state, pane pending bytes,
history browsing and native pending input refuse admission. The native transaction
pins pane session, receiver, item and view; short records go directly to that
receiver and bypass mux input routing. Focus/view invalidation cancels it without
retargeting. Output scrolling that advances the visible-row view can also cancel
an unfinished Paste. A cancellation may leave an editable prefix, never a submitted
line; a fresh Enter is required after consumed completion. See the
[clipboard interface](../interfaces/clipboard.md) for authority, framing and bounds.

The [pointer qualification](../development/system-pointer-qualification.md#task-3-qualification)
records spatial-input validation separately from the original mux results below.

## Cwd and digit-focus qualification (2026-10-11)

Ordinary `make -j16 image` passed with the existing LLVM23 builder, rebuilding
the SDK/runtime and consumers. Manual nested-KVM QEMU used four CPUs, 2 GiB and
VGA at 1280x800, with a temporary Development-only opt-in. Parent `a7b16fe2`
and userland `76d0508` reproduced the old split-at-`home://` and unknown-digit
behavior. Userland `79fda8e` checked both split orientations, independent cwd
changes, a six-directory chain, relative `..` and directory creation, digit
focus, ignored 0/9 and an unused number, busy foreground Lua timeout, recovery
after Ctrl+C, and exited-pane fallback. The submitted dependency adds only an
ownership comment after that code revision:
[d560fbb](https://git.internal/PyxisOS/pyxis-userland/commit/d560fbb),
[userland PR #215](https://git.internal/PyxisOS/pyxis-userland/pulls/215).
Packaged profiles leave the local `multiplexer` setting disabled.

GDB observed `CALL_OK` with six owned directories and a complete description;
the busy case returned `CALL_TIMED_OUT` with an empty snapshot. It inspected
SEND-only client authority, preserved directory rights and released snapshot
handles, with capability reservations and VM borrowers at zero. Allocation
failure and absent descriptions were inspected, not injected; this is QEMU
qualification, not a native-machine result. No kernel change, compiler-container
rebuild or external source was needed.

## Qualification (2026-10-08)

Ordinary `make -j16 image` builds passed with the existing
`pyxis-llvm23.1.3-49e2c1a` builder. No compiler-container rebuild or new
upstream source was needed. Userland dependency is
[312fe7a](https://git.internal/PyxisOS/pyxis-userland/commit/312fe7a3c41c76817d237cab21ca93e9da11efca),
[PR #160](https://git.internal/PyxisOS/pyxis-userland/pulls/160). Manual images
used only a temporary `multiplexer = true` addition to Development; packaged
profiles keep the local `multiplexer` setting disabled. The final dependency
`fe07036` also passed live startup through the `bin://` alias; installed
program placement was inspected in the installer/rescue selection, rather
than claimed as a new installed-machine boot qualification.

Interactive QEMU 10.2.2 with the documented AHCI fix used nested KVM, four CPUs,
512 MiB, standard VGA at 1280x800, VirtIO RNG, no NIC/storage/HOST export, and
the matching `/usr/share/OVMF/OVMF_CODE.fd`/`OVMF_VARS.fd` pair. The following
were exercised through the ordinary keyboard/console paths:

- Initial shell, horizontal/vertical BSP splits, equal/BSP switching, focus,
  eight-pane admission and ninth-pane rejection.
- The then-editor's editing, save, quit and `cat` readback of `home://a`; another
  pane's shell remained usable while it ran, and it redrew after layout resizing.
- Lua output of 1,100 numbered lines; history filled to 1,024 retained rows,
  Page Up/Home browsed the oldest retained rows, and Escape returned to live.
- Natural root exit retained status/text; dismissal reclaimed sibling space.
  A background Lua CPU loop was also terminated on root-shell exit.
  Live closure displayed confirmation, cancellation kept the pane, and confirmed
  closure reclaimed it. Closing the last retained pane ended mux.
- A separate 80x64 Bochs-mode boot exercised the clipped focused-only view;
  GDB read the retained session geometry as 12x4, generation one.
- Mandelbrot launched from a pane onto the graphics layer; Super+Down restored
  mux and Ctrl+C terminated the foreground program and restored its pane prompt.

Read-only GDB inspection observed a sleeping 17-interest mux wait alongside
application waits. After all panes and mux exited, that wait disappeared and
the completed-task list was empty. Terminal generation/rights checks and
allocation unwinding were inspected in source. Full-input backpressure,
generation exhaustion, allocation failure, outer-window shrink/grow and
vi/less/Links runtime behavior were not separately exercised. Links' adapter
currently reads size only at startup.

### Matched RAM FILE-read samples

The existing command was identical in the ordinary shell and one pane:

```text
iobench read boot://share/iobench.bin
```

It verified 1,048,576 bytes, one warmup, five samples, a 4,088-byte buffer, 257
reads per sample, zero short reads and one EOF call. HOST profiling was off.
The baseline used parent `7d02389` and userland `e8ad039`; pane runs used
`21dd3e5` plus the submitted CONSOLE/WAIT header updates, userland `c6d3000`
and the temporary opt-in. The later `b0368f0` minimum-clipping follow-up was
qualified separately in the small-mode boot.
The baseline ISO SHA-256 was
`014e7366b05a3ae585aee477d5ff69e32d5b45a9c6f9e64544831a6324deda13`;
the final opt-in qualification ISO was
`474c3b8700f982bfe82db451b032495b9ab898ffef4a95fabda102dd3f26c862`.

| Run | Payload samples (ns) | Payload median/range (ms) | Complete-consumption samples (ns) | Complete median/range (ms) |
| --- | --- | --- | --- | --- |
| Ordinary shell | 147770, 126220, 126170, 125790, 137440 | 0.126 / 0.126–0.148 | 222590, 199960, 200160, 199440, 215700 | 0.200 / 0.199–0.223 |
| Pane | 184810, 147480, 153860, 153980, 151630 | 0.154 / 0.147–0.185 | 261670, 228350, 228340, 228140, 228470 | 0.228 / 0.228–0.262 |

Clock-call loops measured 35,792 and 35,504 ns/read respectively and
were not subtracted. The pane medians were 22% higher for payload consumption
and 14% higher for complete consumption, with tens of microseconds of absolute
difference. Mux drains and presentation add concurrent work; these collections
do not isolate its cost from nested-VM variation. They are RAM FILE-read
qualification, not disk/physical-host performance or terminal-throughput results.
No new benchmark infrastructure or scope expansion was introduced.

History wheel browsing retains signed vertical fractions until a complete
120-unit detent, moving three rows. Horizontal input is ignored. Focus, reset,
geometry, layout or explicit history-view changes clear the remainder.
