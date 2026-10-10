# Terminal profile for a full-screen editor

Qualification for task 3 of the [Neovim milestone](../../../wip/neovim-libuv.md):
the alternate screen, scroll regions, insert/delete line, reverse index, saved
cursor and charset designations in the kernel TTY and the multiplexer's pane
terminal, as listed in the [sequence table](../../../userland/terminal.md#tty-output-controls),
and BusyBox vi, less, Kilo and Links running on the alternate screen.

## Configuration

- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 4 CPUs, 8 GiB, standard VGA at 1280x800, display off. A tab is 160x48
  cells; a single multiplexer pane is 160x46.
- **Builds:** before is main `c29e0217` with userland `5aede1c`; after adds
  this change. Both images carry a local fifth space, "Mux", with
  `multiplexer = true`, so a pane can be measured; it is not committed.
- **Tools:** a scratch program, never committed, writes a workload to the
  console with `write` and reports elapsed monotonic time. Host CPU time of the
  whole QEMU process is sampled over 8 s from each command.

## Workloads

| Name | Output |
| --- | --- |
| scroll | 5,000 lines of 100 characters with a color change, 550,000 bytes, scrolling the whole screen |
| redraw | 200 frames of 24 positioned 79-character rows with erase-to-end, 468,607 bytes |
| region | after only: region rows 2–23, then 2,000 times a position on the bottom margin, LF and a 79-character row, 192,017 bytes |

## Method

Four rounds, alternating before and after boots: b1 a1 b2 a2 b3 a3 b4 a4.
Each boot runs every workload three times in the Development tab, then three
times in the Mux pane. Round b1's tab runs were not recorded, so
the tab's before figures come from three rounds. In a tab the kernel draws
during the write, so elapsed time is the rendering cost. In a pane the session
queue takes the output and mux draws it afterwards, so the writer's time
mostly measures queueing; the 8 s CPU window includes mux's work. With only the
shell running, the window measured 154–228 ticks.

## Results

Median, then range, in milliseconds; CPU in host ticks per 8 s window.

| Workload | Before | After | CPU before | CPU after |
| --- | --- | --- | --- | --- |
| Tab scroll | 2555 (2443–3307) | 2501 (2415–2653) | 579 | 544 |
| Tab redraw | 46.2 (44.3–163.5) | 45.3 (44.9–48.6) | 326 | 296 |
| Tab region | — | 469 (458–740) | — | 345 |
| Pane scroll | 93.7 (81.3–103.2) | 92.3 (81.2–113.4) | 360 | 350 |
| Pane redraw | 13.1 (8.2–93.9) | 12.5 (9.4–106.7) | 338 | 324 |
| Pane region | — | 4.4 (3.3–7.8) | — | 329 |

- **No regression:** scrolling and redrawing cost the same within run-to-run
  variation. The kernel TTY's extra style store per cell and the region checks
  don't show.
- **Scroll regions:** scrolling a 22-row region costs about 0.23 ms per line
  in a tab, against about 0.5 ms for a whole-screen line, since only the
  region's pixel rows move.
- **Outliers:** single slow samples (3307, 163.5, 740, 106.7 ms) appear on both
  sides, with normal neighbours in the same boot.

## Checks

In a tab and in a pane on the final image, by screenshots:
- **Sequences:** a scratch program, never committed, entered the alternate
  screen, kept a header and footer outside a scroll region while 60 lines
  scrolled through it, inserted and deleted lines, reverse-indexed at the top
  margin, restored a red saved cursor with `ESC 7`/`ESC 8` and a cursor with
  `CSI s`/`CSI u`, and consumed `ESC ( B`. Leaving restored the shell screen,
  its colors and the cursor after the command. Kernel and pane results matched.
- **Mux history:** after the program, history held the shell's output only,
  with none of the 60 region lines.
- **Programs:** `vi`, `kilo`, `ls boot:// | less` and `links` each drew on the
  alternate screen and, on quitting, returned the shell's screen with the
  earlier `ls` output intact. Before this change each left a cleared screen
  with the prompt at the top. Saved files read back with `cat`; Up recalled
  the last command. `lua` reported `TERM=pyxis` in a pane.
- **Rebase:** after rebasing onto main `7766dae0` (with #647 and #656), the
  program checks were repeated in a tab and a pane with the same results. The
  timings were not repeated.
- **Resize:** under Xvfb with the VirtIO GPU and a GTK window, the window grew
  from 640x480 to 1024x673 while the program was on the alternate screen: its
  rows were kept and the new area was blank. Leaving restored the shell screen
  from before the resize. Growing again to 1400x873 kept the shell working.

## Native results

On the ThinkPad, 2026-10-10, the owner booted a PXE build of this branch from
the default entry, at the panel's native mode:
- **Tab:** `vi`, `kilo`, `ls boot:// | less` and `links` each returned the
  shell's screen intact on quitting.
- **Multiplexer pane: pending.** The live image has no multiplexer space; the
  owner checks it on the installed stick after the next update.

The pending pane check, with the same programs as in a tab:

1. Run `ls boot://`, then `vi home://t.txt`; type `ihello`, Escape, `:wq`. The
   `ls` output should still be on screen, with the prompt below it.
2. Repeat with `kilo home://k.txt` (Ctrl-S, Ctrl-Q), `ls boot:// | less` (`q`)
   and `links home://t.txt` (`q`, then `y`).
3. Scroll back with the wheel afterwards: history shows the shell's output,
   not the programs' screens.
4. Note any flicker, leftover text or a misplaced cursor after quitting.

## Limits

- Native: tab behaviour checked, pane behaviour pending; no native timing.
- Pane timings measure queueing; mux's drawing shows only in the CPU window,
  which also includes QEMU's own work.
- The resize check used growth only; shrinking follows the same cropping as
  before this change.
