# Console rendering in the presenter

Owner decisions 2026-10-14, following the
[worker precedence record](../worker-precedence/README.md#kernel-console-rendering-cost):

- writers and `klog` change only text cells;
- whole-screen scrolling rotates a row index;
- the CPU 0 presenter draws only the visible space, once per frame, with
  interrupts on;
- a per-terminal lock replaces the global output lock;
- all kernel terminals change together.

The contract is described in [kernel terminal rendering](../../../kernel/display.md#kernel-terminal-rendering).

## Change

- `kernel/fb/tty.c`:
  - cells only, in a row ring per screen;
  - a handoff record of changed rows, net scroll and a full-redraw flag;
  - `tty_render_collect()` (under the lock) and `tty_render_draw()` (without it);
  - `tty_lock()`, which stops waiting once panic output has started.
- `kernel/space.c`:
  - staging buffers for the visible terminal, replaced on resize;
  - the frame path collects and draws;
  - resize copies cells only.
- Console writes, console geometry and readiness, pointer selection and clipboard
  copy take the terminal's own lock. A serial-mirrored console (Caelum) takes the
  log lock first, so its bytes stay ordered with kernel lines.

## Nested method

Horse VM, nested KVM, q35, 2 GiB, standard VGA at 1280×800 (VirtIO GPU for the
resize check), `intel-hda` with WAV capture, info logging. Revisions:

| Image | Revision |
| --- | --- |
| Base | main `fcf3def5` (later main commits change only docs) |
| After | this change on main `97a5c556` |

The workload runs in the Development space with scripts on a virtio-fs share,
timed with `pyxis.monotonic_ns`. After a 3 s delay it does one of three things:
- 400 writes of a 41-byte line, on screen or with the space hidden;
- 10 or 20 writes of 256 newlines;
- the same 400 writes to a `host://` file, as a control.

The tone A/B in each boot plays a 30 s tone alone, then a second 30 s tone during
20 writes of 256 newlines, and counts WAV gaps of 1 ms or more.

## Results

Per write, in µs. One timing boot per column: three at 4 CPUs and one at 1 CPU.

| | Base | After |
| --- | --- | --- |
| 41-byte line, on screen | 453 / 1310 / 482 / 482 | 1 / 1 / 1 / 1 |
| 41-byte line, hidden space | 507 / 540 / 520 / 542 | 1 / 1 / 1 / 1 |
| 256 newlines, on screen | 163546 / 165259 / 235762 / 164950 | 94 / 149 / 88 / 88 |
| 256 newlines, hidden space | 164958 / 169426 / 163966 / 169768 | 141 / 87 / 96 / 89 |
| Control: 41-byte line to `host://` | 359 / 545 / 530 / 346 | 368 / 1411 / 310 / 378 |

Tone A/B, gaps of 1 ms or more (total silent):

| Boot | Base: alone | Base: with writes | After: alone | After: with writes |
| --- | --- | --- | --- | --- |
| 4 CPUs, first | 0 | 17 (889 ms) | 0 | 1 (20 ms) |
| 4 CPUs, second | 22 (479 ms) | 23 (867 ms) | 0 | 0 |
| 1 CPU, first | 0 | 21 (1147 ms) | 5 (182 ms) | 0 |
| 1 CPU, second | 2 (89 ms) | 22 (1142 ms) | 0 | 2 (41 ms) |

Gaps during a tone alone, without any writes, occur in both images: the nested
host's noise floor.

**Presenter cost**, measured with a never-merged diagnostic in a separate boot.
A Lua loop printed lines continuously for 22 s with a tone playing:
- **Writer throughput:** about 22,700 lines/s, on screen or hidden.
- **Presenter:** a full redraw every frame (about 60 per second).
  - Collecting the changed rows (interrupts off): mean 52 µs, at most 1.1 ms.
  - Drawing (interrupts on): mean 0.67–0.8 ms, at most 3.7 ms.
- **While the flooding space was hidden:** the presenter drew nothing for it.
- **Tone:** 1 restart and one 12 ms gap over the 30 s.

**Resize:** copying cells for 5 terminals took 0.11–0.12 ms. The earlier
whole-raster copy was not measured in this run.

## Correctness checks (nested, after image)

- **Neovim**, on a 300-line file:
  - five Ctrl-E line scrolls and two Ctrl-Y showed the right rows;
  - `:split` scrolled each window within its own region;
  - leaving restored the shell's primary screen.
- **Selection and copy:**
  - a pointer drag highlighted the text;
  - Ctrl+Shift+C stored exactly the selected text, read with GDB from the clipboard
    store: before any scrolling, and again after 70 lines had rotated the row
    ring;
  - selecting blank cells stored 0 bytes.
  - Paste could not be driven through QMP key events on either image, so it was
    not checked.
- **`screenshot host://…`** matched the QMP screen dump, apart from the system
  cursor and the later prompt row.
- **Live resize** through QEMU's VNC desktop-size request: 1280×800 to 1024×640
  and back. The Development, Caelum and Read-only spaces showed correct content,
  including spaces that were hidden during the resize.
- **No assertion or panic** in any boot.

## Native run

ThinkPad, owner-typed in the Development space, 2026-10-14: main `97a5c556`
against this change (`580a0b74`), with a 30 s A4/A3 tone. One 256-line write took
530 ms on main and 255 µs with the change. Other figures from that run were not
recorded here.

To repeat it on an image that includes the
[check scripts](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/checks/README.md),
type in the Development space, waiting for each prompt:

```text
pcm 440 220 30 &
lua boot://share/checks/console-burst.lua
pcm 440 220 30 &
lua boot://share/checks/console-flood.lua
```

Listen for gaps in each tone while the screen fills, and note each script's
summary line (`us_per_write`, `lines`). Then collect `log | grep audio:`.
