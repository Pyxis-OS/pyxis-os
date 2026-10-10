# Power overlay qualification

QEMU checks for task 1 of the [system power overlay](../../../wip/system-power-overlay.md),
the Ctrl+Alt+Delete emergency screen, built from this change on main
`3c3f9cfe`.

## Configuration

- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 8 GiB. Standard VGA at 1280x800, or the VirtIO GPU (hardware cursor) at
  1280x800 with no display window; the resize check used VirtIO under a GTK
  window in Xvfb.
- **Input:** QMP key and relative mouse events, through PS/2.
- **Images:** the live image with a local, uncommitted fifth "Mux" space
  (`multiplexer = true`) for pane checks; and an installed image made by this
  build's installer on a 4 GiB sparse disk.
- **Tools:** screenshots by QEMU `screendump`. Screendumps include the
  software cursor of standard VGA but not VirtIO's hardware cursor. Scratch
  programs, never committed, wedge a space: a busy loop, an endless console
  writer, and a program that shows one frame, holds keyboard capture and
  display, then spins.

## Checks

| Check | Std VGA 1 CPU | Std VGA 4 CPUs | VirtIO 1 CPU | VirtIO 4 CPUs |
| --- | --- | --- | --- | --- |
| Shell tab: open, keys, Escape, shell intact | yes | yes | yes | yes |
| Mux pane: open, mouse Cancel, shell intact | yes | yes | yes | yes |
| Quake with lock: open, mouse Cancel, lock not restored, click relocks | yes | yes | yes | — |
| Shut down from live | yes | yes | yes | yes |
| Reboot from live | yes | yes | yes | — |
| Installed: Reboot, then Shut down | — | yes | yes | — |
| Wedged programs | yes | — | — | — |
| Forced failure | — | yes | — | — |
| Resize while open (GTK) | — | — | — | yes |

- **Overlay:** the whole display, including the bar, showed the panel with
  Cancel selected. Right from Cancel wrapped to Shut down; Left twice reached
  Shut down. Escape returned to the shell with its screen intact, and the next
  typed command ran whole. The overlay's keys did not reach the shell.
- **Mouse:** in the Mux pane, a click on Cancel closed the overlay; the click
  did not reach the pane.
- **Quake:** Quake locked after a click into the game and turned with mouse
  motion. With the overlay open the arrow appeared over the panel. After a
  mouse Cancel, motion did not turn the view: lock was not restored. One click
  relocked it and motion turned the view again.
- **Power:** each Shut down logged `power: flushing pools; powering off` and
  QEMU exited; each Reboot logged `power: flushing pools; restarting` and the
  system booted again. No other kernel log line was added.
- **Installed:** after writing `home://ov.txt`, Reboot from the overlay; the
  file read back after the restart. After writing `home://ov2.txt`, Shut down
  from the overlay. On the host, `npfs-inspect` on partition 2 reported
  `journal_state empty` and both files with their contents. The installer's own
  live boot was also shut down from the overlay after installing.
- **Wedged programs, one CPU:** the Read-only tab ran the console writer, the
  Mux tab the busy loop, and the Development tab the display program, all at
  once. Ctrl+Alt+Delete over the display program showed the overlay within
  300 ms (the first screenshot after the chord); Escape returned to its frame.
  Over the console writer's tab the overlay again appeared within 300 ms, and
  Shut down then powered off 0.55 s after Enter.
- **Forced failure:** a throwaway build, not part of the change, replaced the
  pool flush with a 3 s wait and an I/O error. With programs held, the overlay
  kept drawing "Shutting down: flushing pools..." with disabled buttons and
  ignored Escape. It then showed "Shutdown failed (status 19); the system
  stays up." in red with Cancel selected, matching the worker's log line.
  Cancel returned to a working shell.
- **Resize:** with the overlay open, the GTK window grew from 640x480 to
  1100x760. The guest kept 640x480 until Escape, then logged
  `display: resized to 1100x733`; reopening drew the panel centred at the new
  size.

**Performance.** No baseline was taken. With the overlay closed, the change
adds one flag test per presented frame, key event and pointer report, and none
on output paths. While open, each frame copies the whole screen like a
full-screen graphics frame.

## Native results

The owner ran the steps below on the ThinkPad on 2026-10-10, with a PXE build
of `a2d49db0` from the default entry:
- **Built-in keyboard:** the overlay opened, and Escape or Cancel returned the
  shell intact. Over Quake, the overlay and Cancel behaved as described above.
  Reboot by keyboard restarted the machine, and Shut down by click powered it
  off.
- **USB keyboard (Keychron):** the overlay opened, and Escape and Cancel
  worked. Reboot and Shut down were not repeated from it, because the power
  path does not depend on the keyboard.
- **Keypad:** Ctrl+Alt with keypad "." opened the overlay. Pyxis takes that key
  as keypad Delete whatever the Num Lock state; KDE Plasma on the same machine
  does not treat it as Delete.
- **Pending:** step 6, the installed system's flush, waits for the next stick
  update.

## Native steps for the owner

On the ThinkPad with a PXE build of this branch, default entry:

1. In a tab with the shell, run `ls boot://`, press Ctrl+Alt+Delete, then
   Escape. The shell screen should be intact and typing should work at once.
2. Start `quake +map start`, click into it and turn with the touchpad or mouse.
   Press Ctrl+Alt+Delete: the overlay and an arrow should appear. Click
   Cancel. Moving should not turn the view until you click into the game.
3. Ctrl+Alt+Delete, Left, Enter: the machine restarts.
4. Ctrl+Alt+Delete, click Shut down: the machine powers off.
5. If the USB keyboard is attached, repeat step 1 from it, and once with
   keypad Delete.
6. Pending until the next stick update: on the installed system, write a file
   in `home://`, shut down from the overlay, boot again and read it back.

## Limits

- Natively, the installed-system flush is still unchecked.
- The VirtIO checks could not see the hardware cursor in screenshots; mouse
  behaviour there was judged by its effect.
- Coverage of a stuck presenter, display or ACPI worker is by code inspection
  only; the [emergency coverage](../../../wip/system-power-overlay.md#emergency-coverage)
  lists what cannot work.
