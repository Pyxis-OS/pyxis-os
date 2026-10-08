# Terminal multiplexer

`boot://mux.pxe` presents independent native terminal sessions in one window.
Each pane starts an ordinary shell with its own execution group, terminal
state, cursor, dimensions and colored scrollback. This is userspace terminal
emulation over the [native TTY subset](terminal.md#tty-output-controls).

## Startup

Add `multiplexer = true` to a local space in `userspace/config/live.lua` or
`userspace/config/installed.lua`, then rebuild the image. It defaults to false:

```lua
{ name = "development", title = "Development", init = "boot://init",
  network = true, launch = true, multiplexer = true,
  roots = { home = "read-write" } },
```

Trusted init receives a distinct `mux_terminal` creation grant. The trusted
shell/session handoff preserves it; after any configured service setup the
session launcher selects mux instead of the shell, forwarding it as `terminal`
and retaining CREATE_GROUP on mux's launcher. Ordinary shells receive neither.
Remote startup does not select mux. Direct ordinary command launch lacks the
administrative grants mux requires. Missing resources or failed startup produce
a diagnostic, with no automatic restart or fallback shell.

The configured terminal tab width reaches mux through `--tab-width 1..32` and
is applied to every newly created pane. Each shell inherits the initial selected
roots, working-directory chain, descriptive path and environment; changing one
shell's directory does not change the starting directory of future panes.
Filesystem grants are shared explicitly, so panes are not filesystem sandboxes.
`launch = true` independently controls ordinary child-launch delegation.

## Controls and layout

Press Ctrl+B, release it, then enter a command:

| Command | Behavior |
| --- | --- |
| `%` / `"` | Split left/right or top/bottom; focus the new shell |
| Arrow | Focus a pane in that direction |
| `b` | Restore the recursive BSP layout |
| `e` | Arrange equal strips along the most recent split axis |
| `[` | Browse focused pane scrollback |
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

If display shrink prevents the full layout, only the focused pane is shown.
Other sessions keep running and draining at their previous dimensions. When
even the focused pane does not fit, its session retains at least 12x4 content
and the outer view clips it; an off-view cursor is hidden. Arrows
then cycle through the retained panes; growing the display restores the layout.
Visible panes receive [session resize](terminal-sessions.md#resize), with the
existing SIZE generation and RESIZED event. Apps that observe resize redraw;
other apps retain their own existing behavior. The presentation uses at most
512 columns and 258 rows, reflecting the native session bounds plus headings.

History retains the last 1,024 rows scrolled off each pane, including colors and
original widths, without reflow. Page Up/Down moves a screenful; Up/Down moves
one row; Home/End selects oldest history/live screen. Escape or `q` returns to
live input. Output continues while browsing; existing text stays anchored until
eviction of old rows. Screen erasure and resize cropping do not create history.

## Authority, input and exit

Mux alone owns attachments, RESIZE, HANGUP and group supervision. Each root
shell receives application streams, interrupt arming, its separate completion
event grant and a group-bound launcher. Commands receive existing explicitly
delegated resources. Pane shells receive the same explicitly held local display,
keyboard, pointer and power grants as ordinary local session shells. Graphical
programs launched from a pane draw on the shared space's
[graphics layer](space-layers.md), above mux; they never draw inside the pane.
Pane shells receive no session creation or group-supervision grant.

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
until dismissed. Confirmed closure removes the pane after output EOF and group
cleanup; siblings reclaim its layout region. Closing the last pane ends mux.
Mux exit/fault also drops the final controlling grants and requests termination
of every remaining group. Group cleanup has no fixed deadline: published HOST
work can delay it. See [execution groups](../interfaces/execution-groups.md).

Every turn drains a bounded number of records from each pane. Rendering sends
changed colored cells through the outer CONSOLE; it does not pass pane escape
sequences through to the outer terminal. Seventeen readiness interests cover
outer input and eight output/lifecycle pairs, without an idle polling loop.

Multiple windows, ratio adjustment, detach/reattach, Unicode widths and a broader
VT escape set remain deferred. Full-screen programs share the same retained
screen as their shell; no alternate-screen protocol is introduced.

## Qualification (2026-10-08)

Ordinary `make -j16 image` builds passed with the existing
`pyxis-llvm23.1.3-49e2c1a` builder. No compiler-container rebuild or new
upstream source was needed. Userland dependency is
[b0368f0](https://git.internal/PyxisOS/pyxis-userland/commit/b0368f091237733f81a89e382cf35b012cfd1433),
[PR #160](https://git.internal/PyxisOS/pyxis-userland/pulls/160). Manual images
used only a temporary `multiplexer = true` addition to Development; packaged
profiles keep the default opt-out.

Interactive QEMU 10.2.2 with the documented AHCI fix used nested KVM, four CPUs,
512 MiB, standard VGA at 1280x800, VirtIO RNG, no NIC/storage/HOST export, and
the matching `/usr/share/OVMF/OVMF_CODE.fd`/`OVMF_VARS.fd` pair. The following
were exercised through the ordinary keyboard/console paths:

- Initial shell, horizontal/vertical BSP splits, equal/BSP switching, focus,
  eight-pane admission and ninth-pane rejection.
- Kilo editing, save, quit and `cat` readback of `home://a`; another pane's
  shell remained usable while Kilo ran, and Kilo redrew after layout resizing.
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
