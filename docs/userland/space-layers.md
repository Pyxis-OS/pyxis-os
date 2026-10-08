# Graphics and terminal layers in a space

Each local space has a terminal and at most one acquired graphics session.
Super+Down shows the terminal; Super+Up restores graphics. Both layers keep
receiving output while hidden. They share the space's processes and grants.

## Choosing a layer

| Session state or action | Visible layer and tab marker |
| --- | --- |
| No session, or ACQUIRE before first PRESENT | Terminal, no marker |
| First successful PRESENT | Graphics, `+` |
| Super+Down after PRESENT | Terminal, `−` |
| Super+Up after PRESENT | Graphics, `+` |
| Later PRESENT, REPLACE, resize or space switch | Preserve the choice |
| RELEASE or owner exit | Terminal, no marker |

Starting a session in another space does not select that space. Its first
PRESENT records graphics for the next visit. A later session starts with its
own first-PRESENT behavior.

Either Super key works without Shift, Control or Alt. Each arrow press acts
once; its repeats and release are consumed even if Super is released first.
Without a presented session, Up/Down is a consumed no-op. Repeating the current
choice leaves input state and queued terminal bytes alone.

The marker occupies the tab's existing right margin cell, beside the border.
Tab widths, title centering, clipping and selection underline are unchanged.
An inactive tab records its saved choice. See the [space bar](init.md#space-bar)
for layout and narrow-tab behavior.

## Input and execution

Hiding graphics retains its mapping and independently acquired keyboard/pointer
sessions. Captured input loses focus and accepted held controls are reset;
returning requires fresh presses. Terminal typing uses the existing 4 KiB console
queue. Super+Up clears unread text before restoring capture. The foreground
shell still waits for the graphical job, so there is no second prompt;
[Ctrl+C](shell.md#interrupting-foreground-commands) uses its existing armed
foreground interrupt.

Keyboard acquisition/release while hidden preserves terminal bytes. Session
teardown preserves them when text remains the input destination; if an independent
capture survives and resumes routing, unread text is discarded. Pointer capture
supplies no terminal input. See [keyboard routing](../devices/keyboard.md) and
[pointer focus](../devices/mouse.md) for ownership, event and loss rules.

Quake, Doom, Mandelbrot and `mousetest` keep running while hidden or unselected.
Focus describes input eligibility. Applications retain their own explicit
pause or idle behavior. Unattended rendering still costs CPU and memory bandwidth;
see [the resource-cost limitation](../technical-debt.md#unselected-graphical-applications).

Only the selected space's chosen surface is copied. Hidden graphics retains its
backing without a new presenter lease or pixel copy; an already-snapshotted frame
may finish after a transition. Layer switching allocates nothing. Mapping lifetime,
REPLACE and resize behavior remain in the [graphics interface](../interfaces/graphics.md).

## Qualification

The ordinary `make -j16 image QUAKE_DATA=/quake-data` build passed using the
existing LLVM builder. Qualification used dependency pins: ports `8449065`
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

The remaining native check and source-only coverage limits are recorded in
[technical debt](../technical-debt.md#space-layer-qualification). The owner
closed the milestone on 2026-10-08 with native validation deferred. Build and
filesystem CI passed on submitted kernel revision `f838b21`
([PR #512](https://git.internal/PyxisOS/pyxis-os/pulls/512), workflow #1182).

### Performance samples

The existing `quake +timedemo demo1` workload rendered 969 frames per sample.
Both images used the same standard VGA/Bochs, 1280x800 destination, KVM/four-CPU/
512-MiB configuration and HOST export, without profiling. Baseline source was
Pyxis `38684a9` with the pins above; after samples used the kernel implementation
committed as `f838b21`. Kernel ELF identities:

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
