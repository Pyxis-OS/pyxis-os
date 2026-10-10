# System power overlay

Status: **proposal, 2026-10-10. Not agreed; nothing here authorizes code.**
Code inspected at `3c3f9cfe`.

Ctrl+Alt+Delete opens a full-screen system overlay offering **Shut down**,
**Reboot** and **Cancel**, chosen by mouse click or keyboard. It works in any
tab, including over a game that holds keyboard capture and relative pointer
lock, and no program can intercept or produce it.

## What exists

- **Kernel-owned chords.** `handle_space_input_locked()` in `kernel/space.c`
  reads every physical key event before any program does. Super+Escape
  ([user escape](../interfaces/pointer.md#relative-lock-and-user-escape)),
  Super+arrows and the volume chords are taken there; only the remaining
  events reach the active space's keyboard. Programs have no call that
  injects keyboard events, and remote terminals deliver text into session
  queues, not key events.
- **A trusted kernel popup.** The [volume controls](../userland/audio-volume.md)
  are drawn by the kernel (`kernel/volume_ui.c`) over the composed frame.
  Explicit focus calls `keyboard_set_overlay()`, which suspends the space's
  keyboard input, resets held keys and sends focus loss and gain without
  releasing capture. Keys it consumed stay suppressed until their release.
- **Power without a capability.** A press of the power button runs the
  [power-off sequence](../kernel/acpi.md#power-off-and-restart) with no
  capability: physical access already allows holding the button. The
  `power` capability goes to shells in `power = true` spaces, and
  `remote_power` to the remote root shell.
- **Clean power-off.** The ACPI worker holds user tasks, flushes and seals
  every writable pool, then enters S5 or resets. A failed flush or firmware
  refusal unseals the pools, releases the tasks and returns the status; the
  system stays up.

## Proposal

### The chord

Delete pressed while Control and Alt are held is taken in
`handle_space_input_locked()`, beside Super+Escape and ahead of the volume,
clipboard and navigation checks. Either Control and either Alt count, and
extra Shift or Super does not prevent it. Like Super+Escape, the Delete press,
its repeats and its release are consumed even if the modifiers are released
first. Control and Alt presses were already delivered; opening the overlay
resets held keys, so the program sees them released through focus loss.

Every source that feeds the kernel's physical key queue counts: PS/2, and
USB and Bluetooth keyboards as they arrive. Nothing else does. A future
[remote desktop](remote-desktop.md) input path must not produce the chord;
if it needs one, it gets its own action.

The chord is taken in every tab, including Caelum's, and while the overlay is
open a second chord does nothing.

### Drawing and authority

**The kernel draws the overlay and requests the power operation itself**,
like the power button. Reasons:
- **Ctrl+Alt+Delete is for when things are stuck.** A kernel overlay still
  appears when every program, a shell or a supervisor has hung.
- **The flush holds user tasks.** A userspace helper would be parked during
  the flush it requested. It could show no progress, and it could not show a
  power-off failure until the tasks are released. The kernel overlay keeps
  drawing.
- **The authority is physical access.** It is the same as the power button's,
  so no new capability, grant or helper is needed. Spaces keep their `power`
  capability for `poweroff`/`reboot` and nothing changes for them.

The alternative, a trusted system helper holding `power` and drawing a
surface above every space, waits for the Continuum supervisor
([Asterism](spaces.md#asterism)). It needs a chord delivery channel, a
system-wide surface layer and a helper that is always alive.

### The overlay

- **Picture:** the whole display, including the bar, is covered by an opaque
  panel in the theme's colours with a title and three buttons in the kernel
  font. Programs keep running and presenting underneath; their frames are not
  shown until the overlay closes.
- **Keyboard:** Left/Right and Tab/Shift+Tab move between buttons, Enter or
  Space activates, Escape cancels. **Cancel is selected when it opens.**
  Keys used in the overlay are consumed until their release, as in the
  volume popup.
- **Pointer:** opening revokes relative lock exactly as Super+Escape does,
  so the system cursor appears. A click on a button activates it; other
  clicks are consumed. Hover highlights.
- **While open:** Super+arrows, the volume chords and clipboard actions are
  ignored. An open volume popup closes. Resize redraws the overlay at the
  new size; it does not close it.

### Shut down and Reboot

The chosen button issues the same power request as the power button, run by
the ACPI worker. The overlay then shows what happens:
- "Flushing pools; shutting down" (or "restarting") while the worker runs;
- on failure, "Shutdown failed (status N); the system stays up." with Cancel
  selected, so the user can retry or return;
- "Another power operation is running" for BUSY, and "Power control is
  unavailable" when there is no ACPI worker (no RSDP or a failed namespace).

No new kernel log line: the worker's existing `power:` lines record the
operation.

### Cancel

Cancel closes the overlay and presents the active space's current content
(not a snapshot from before). Keyboard focus returns to the same space with
focus gain; keyboard capture was never released. Held keys stay reset, and
no key typed in the overlay reaches the program. **Relative lock is not
restored:** the space needs a fresh activation click, as after Super+Escape.

### Remote, installed and live

- **Remote:** the chord comes only from local keyboards. Remote shells keep
  their `remote_power` behaviour, and a remote `poweroff` while the overlay is
  open runs as today. The overlay's BUSY message covers that race.
- **Installed and live** behave the same: the worker flushes whatever pools
  are writable. A live image's RAM volumes are lost on shutdown, as with
  `poweroff`; the overlay does not warn about it in the first task.
- **An Update in progress** is not protected, as with the power button today;
  holding off power operations while the installer writes is the existing
  [Battery-aware Update](later-os-directions.md#power-and-acpi) follow-up.

### Debugger and panic

- **Panic** disables interrupts and halts. No key is read, so the chord does
  nothing; the last presented frame stays, and panic output still goes to
  serial, UDP and the log ring. Only the firmware's forced power-off works.
- **A stopped kernel debugger** (the
  [network debugger](network-debugger.md)'s all-stop, or QEMU's stub) reads no
  input. Keys pressed while stopped may be processed after resume, opening the
  overlay; it only acts after a further choice. The debugger's own reset is
  unchanged. The overlay draws only on the BSP in normal operation, never
  from a stopped context.

## Owner decisions

1. **Authority and drawing:** the kernel draws the overlay and requests power
   with physical-access authority, like the power button. *Default: yes.*
   Alternative: a trusted helper holding `power`, after Continuum.
2. **Availability:** the overlay is always available from local keyboards,
   whatever spaces have `power = true`. A boot-configuration switch to
   disable it comes only with a profile that needs one, such as a future
   public server. *Default: always on.*
   Alternative: offer Shut down and Reboot only when some local space has
   `power = true`.
3. **Cancel and lock:** Cancel returns focus and keeps capture but does not
   restore relative lock; the program needs a fresh click, as after
   Super+Escape. *Default: no restore.* Alternative: the kernel restores the
   lock the overlay revoked.

## First task

One focused PR implementing the proposal above as accepted:
- the chord in `kernel/space.c`;
- a kernel overlay beside `kernel/volume_ui.c`, drawn at presentation time
  over the whole display, with keyboard and pointer handling;
- a power request from the overlay through the ACPI worker, with its status
  returned to the overlay.

**Validation:** QEMU with standard VGA and VirtIO (hardware cursor), one and
four CPUs: open the overlay over the shell, a mux pane and Quake with lock
held; Cancel by Escape and by click; Shut down and Reboot from a live image and
from an installed disk image, checking the pool's journal afterwards. Then
check the failure display with a throwaway, unmerged forced flush failure.
Native steps on the ThinkPad for the owner.

**After this task the owner can** press Ctrl+Alt+Delete in any tab, even in a
game with mouse lock, and shut down, reboot or return with a click or the
keyboard.

## Later

- A live-image warning that RAM volumes are lost.
- Holding off power operations while the installer writes.
- Other entries (lock screen, switch space) only with users and sessions.
