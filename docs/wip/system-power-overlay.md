# System power overlay

Status: **decisions accepted 2026-10-10; task 1 implemented in this PR, native
check pending.** Code inspected at `3c3f9cfe`. The sections below describe the
implemented behaviour; [qualification](../development/experiments/power-overlay/README.md)
records the QEMU checks.

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

## Behaviour

### The chord

Delete, or keypad Delete (keypad period), pressed while Control and Alt are held is taken in
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
open a second chord does nothing. Before presentation has started, or without
a display, the chord is consumed and ignored.

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
- **Pointer:** while the overlay is shown, the active space's graphics and
  terminal pointer surfaces count as unfocused. Opening therefore revokes
  relative lock with the same fresh-activation requirement as Super+Escape,
  ends drags and sends LEAVE, and the default arrow appears. A click on a
  button activates it; other clicks are consumed. Hover outlines a button.
- **While open:** Super+arrows, Super+Escape, the volume chords and clipboard
  actions are ignored. An open volume popup closes. A display resize waits
  until the overlay closes, because applying one takes the output lock (see
  [emergency coverage](#emergency-coverage)).

### Shut down and Reboot

The chosen button calls `acpi_power_local()`, which hands the operation to
the ACPI worker with the power button's authority; the worker runs the same
[power-off sequence](../kernel/acpi.md#power-off-and-restart). The overlay then
shows what happens:
- "Shutting down: flushing pools..." (or "Restarting: ...") while the worker
  runs, with the buttons disabled; Escape does nothing then, because programs
  are already held;
- on failure, "Shutdown failed (status N); the system stays up." with Cancel
  selected, so the user can retry or return;
- "Another power operation is running." for BUSY, and "Power control is
  unavailable on this machine." when there is no ACPI worker (no RSDP or a
  failed namespace).

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

### Emergency coverage

The overlay is meant to work when something else has stopped working.
- **Covered:** wedged programs. The chord is read by the kernel before any
  program, and overlay frames are composed from the overlay alone: not from
  the active space's frame or TTY, the bar or the battery reading, without the
  global output lock and without allocating. A program that spins, floods its
  console, or holds keyboard capture and display and never reads or submits
  again cannot delay it. The [qualification](../development/experiments/power-overlay/README.md)
  measured this on one CPU.
- **Covered:** the power operation itself. User tasks are held during the
  flush, but the overlay is kernel work and keeps drawing, so progress and
  failure stay visible.
- **Not covered: a stuck presenter.** The presenter task reads the keyboard
  between frames. If it never returns to that loop, the chord is not read.
  Examples are a CPU holding the global output lock forever (ordinary TTY
  composition waits for it), the BSP stuck with interrupts disabled, or a
  kernel fault. Opening the overlay during a terminal selection drag also
  takes the output lock once, to end the drag.
- **Not covered: a display that stops completing frames.** Nothing can be
  drawn; the drivers' existing timeouts mark the display unavailable.
- **Not covered: a stuck ACPI worker or flush.** The overlay keeps showing
  "flushing pools" without a time limit.

In those cases, a short press of the power button asks the ACPI worker
directly, independently of the presenter and the keyboard, and holding it
forces power-off without flushing.

### Debugger and panic

- **Panic** disables interrupts and halts, or with the network debugger
  enabled stops in the debugger for good. No key is read, so the chord does
  nothing. Panic text is written directly to the screen where the display
  allows it, over the overlay if it was shown (VirtIO stays serial-only), and
  also goes to serial, UDP and the log ring. Only the firmware's forced
  power-off works.
- **A stopped kernel debugger** (the
  [network debugger](network-debugger.md)'s all-stop, or QEMU's stub) reads no
  input. Keys pressed while stopped may be processed after resume, opening the
  overlay; it only acts after a further choice. The debugger's own reset is
  unchanged. The overlay draws only on the BSP in normal operation, never
  from a stopped context.

## Accepted decisions

Accepted by the owner on 2026-10-10:

1. **Authority and drawing:** the kernel draws the overlay and requests power
   with physical-access authority, like the power button. A trusted helper
   holding `power` was not chosen.
2. **Availability:** the overlay is always available from local keyboards,
   whatever spaces have `power = true`. It is an **emergency screen**: it must
   keep working when a space or the presenter is wedged where that is
   achievable, and its limits are stated. A boot-configuration switch to
   disable it comes only with a profile that needs one, such as a future
   public server.
3. **Cancel and lock:** Cancel returns focus and keeps capture but does not
   restore relative lock; the program needs a fresh click, as after
   Super+Escape.

## Tasks

1. [x] **The overlay.** The chord in `kernel/space.c`, the overlay in
   `kernel/ui/power_overlay.c` composed by the presenter, pointer routing in
   `kernel/pointer.c`, and `acpi_power_local()` in the ACPI worker. Checked in
   QEMU on both display drivers, one and four CPUs, live and installed; the
   [qualification](../development/experiments/power-overlay/README.md) lists the
   checks and the owner's native steps. **After this task the owner can**
   press Ctrl+Alt+Delete in any tab, even in a game with mouse lock, and shut
   down, reboot or return with a click or the keyboard.

## Later

- Bounded output-lock waits in the presenter, so a stuck writer drops frames
  instead of stopping input. This changes ordinary presentation and needs a
  decision first.
- A live-image warning that RAM volumes are lost.
- Holding off power operations while the installer writes.
- Other entries (lock screen, switch space) only with users and sessions.
