# System power overlay

Ctrl+Alt+Delete opens a full-screen kernel overlay offering **Shut down**,
**Reboot** and **Cancel**, chosen by mouse click or keyboard. It works in any
tab, including over a game that holds keyboard capture and relative pointer lock,
and no program can intercept or produce it. Implemented in #659 and extended by
the NVMe shutdown gate (#710). The
[power overlay qualification](../development/experiments/power-overlay/README.md)
records the QEMU and native ThinkPad checks; the NVMe force path is checked in
the [NVMe task 1 record](../development/experiments/nvme-task1/README.md). The
installed-system flush has not been checked natively; it waits for the next stick
update. Parked follow-ups are in [later directions](../wip/later-os-directions.md#power-and-acpi).

## Authority and drawing

The kernel draws the overlay and requests the power operation itself, with the
power button's physical-access authority: no capability, grant or helper
(accepted 2026-10-10). Reasons: it must appear when every program, shell or
supervisor has hung; the flush holds user tasks, so a userspace helper would be
parked during the operation it requested and could show neither progress nor
failure; and physical access already allows the power button. Spaces keep their
`power` capability for `poweroff`/`reboot` unchanged. A trusted helper holding
`power` and drawing above every space waits for the Continuum supervisor
([Asterism](../wip/spaces.md#asterism)).

The overlay is always available from local keyboards, whatever spaces have
`power = true`. A boot-configuration switch to disable it comes only with a
profile that needs one, such as a future public server.

## The chord

Delete, or keypad Delete, pressed while Control and Alt are held is taken in
`handle_space_input_locked()` in `kernel/space.c`, beside Super+Escape and ahead
of the volume, clipboard and navigation checks. Either Control and either Alt
count; extra Shift or Super does not prevent it. The Delete press, its repeats
and its release are consumed even if the modifiers are released first. Control
and Alt presses were already delivered; opening the overlay resets held keys, so
the program sees them released through focus loss.

Every source of the kernel's physical key queue counts (PS/2, USB and Bluetooth
keyboards); nothing else does. Programs cannot inject keyboard events and remote
terminals deliver text, not key events. A future
[remote desktop](../wip/remote-desktop.md) input path must not produce the chord;
if it needs one, it gets its own action. The chord works in every tab, a second
chord does nothing while the overlay is open, and before presentation has started
or without a display it is consumed and ignored.

## The overlay

- **Picture:** an opaque panel in the theme colours with a title and three
  buttons in the kernel font covers the whole display, including the bar.
  Programs keep running and presenting underneath, unseen until it closes.
  Drawing is in `kernel/ui/power_overlay.c`, composed by the presenter.
- **Keyboard:** Left/Right and Tab/Shift+Tab move between buttons, Enter or Space
  activates, Escape cancels. Cancel is selected on opening. Keys used are
  consumed until release. Explicit focus uses `keyboard_set_overlay()`, as the
  [volume popup](../userland/audio-volume.md) does: it suspends the space's
  keyboard, resets held keys and sends focus loss and gain without releasing
  capture.
- **Pointer:** while shown, the active space's graphics and terminal pointer
  surfaces count as unfocused. Opening revokes relative lock with the same
  fresh-activation requirement as Super+Escape (see the
  [pointer contract](../interfaces/pointer.md#relative-lock-and-user-escape)),
  ends drags and sends LEAVE; the default arrow appears. A click on a button
  activates it, other clicks are consumed, hover outlines a button.
- **While open:** Super+arrows, Super+Escape, volume chords and clipboard actions
  are ignored and an open volume popup closes. A display resize waits until the
  overlay closes, because applying one takes the log lock and every terminal's
  output lock.
- **Cancel** closes the overlay and presents the active space's current content.
  Keyboard focus returns with focus gain; capture was never released and held
  keys stay reset. Relative lock is not restored: the program needs a fresh
  activation click (accepted 2026-10-10).

## Shut down and Reboot

The chosen button calls `acpi_power_local()`, which hands the operation to the
ACPI worker with the power button's authority; the worker runs the clean
[power-off sequence](acpi.md#power-off-and-restart): hold user tasks, close storage
admission and drain admitted/deferred work, flush and seal retained RW FAT mounts,
then seal writable npfs pools, stop NVMe admission, drain and confirm bounded
normal controller shutdown, then S5 or reset. FAT sealing retains handles and
publication intervals. It clears an owned dirty mark only without detached
allocated chains; otherwise the flush leaves dirty set and that condition never
refuses power. An unresolved FAT mutation blocks clean power. If power fails and
programs resume, the next mutation durably re-marks a cleared FAT instance before
effects. See [FAT power handling](../devices/fat32-mounts.md#failure-and-clean-power)
and its [scratch qualification](../development/experiments/fat32-mounted-writes.md).
The overlay shows:

- "Shutting down: checking storage..." (or "Restarting: ...") while the worker
  runs, with buttons disabled; Escape does nothing, since programs are held.
- On a completed failure, "Shutdown failed (status N); the system stays up." with
  Cancel selected. The power buttons become **Force off** and **Force reboot**
  beside "Unsafe: bypasses storage checks; data may be lost." A further explicit
  selection asks the worker to enter S5 or reset without FAT/npfs storage checks or
  checking NVMe, and logs the forced operation. The failure and warning frame
  must complete presentation before a fresh key press or click can select force.
  Force is never automatic and never available through userspace `POWER` rights;
  ordinary requests and short power-button presses stay clean (accepted
  2026-10-11; NVMe controllers stay retired until reboot). Cancel returns to the
  space; reopening offers the clean choices.
- "Another power operation is running." for BUSY and "Power control is
  unavailable on this machine." with no ACPI worker.

The worker's `power:` log lines record the operation and NVMe shutdown stage.

Remote shells keep `remote_power`; a remote `poweroff` while the overlay is open
runs as today and the BUSY message covers the race. Installed and live behave the
same: the worker seals writable FAT instances and npfs pools, and a live image's
RAM volumes are lost on shutdown as with `poweroff`, without a warning. An Update
in progress is not protected, as with the power button.

## Button sound

Fresh keyboard selection changes (arrows or Tab/Shift+Tab) and button activation
(keyboard or pointer, including Cancel) request the owner's
[button click](../../assets/ui/power/README.md). Opening, hover, Escape and key
repeats are silent; held navigation can still move selection as before. A fresh
press fades the current voice out over 5 ms, then starts the clip again rather
than stacking voices. Requests during that fade coalesce without extending it.
The build-time conversion adds 5 ms fades to the clip's start and end. The
mixer cannot withdraw up to 80 ms already committed to the shared DMA ring.

This is a trusted kernel UI source, with no session slot, allocation, program
entry point or per-space volume. It obeys master volume/mute through the
[ordinary mixer](../interfaces/audio.md); unavailable audio simply stays silent.
Input and clean/force power operations never wait for playback, so reboot or
power-off may cut the activation sound short. Existing codec startup and
headphone settling remain unchanged. Owner listening on ThinkPad speakers
confirmed navigation/Cancel, silent mute and clean rapid restarts after the
fade correction. Headphones were not tested. See the
[qualification record](../development/experiments/power-overlay-sound.md).

## Emergency coverage

The overlay is meant to work when something else has stopped working.

- **Covered, wedged programs.** The kernel reads the chord before any program,
  and overlay frames are composed from the overlay alone: not from the active
  space's frame or TTY, the bar or battery reading, without the global output
  lock and without allocating. A program that spins, floods its console, or holds
  capture and display and never submits again cannot delay it (measured on one
  CPU in the qualification).
- **Covered, the power operation.** User tasks are held but the overlay is kernel
  work and keeps drawing, so progress and failure stay visible. NVMe drain and
  shutdown waits are bounded; a reported failure enables the force choices
  without ordinary output locks.
- **Not covered, a stuck presenter.** The presenter reads the keyboard between
  frames. If it never returns to that loop (a CPU holding the log lock or a
  terminal output lock forever, the BSP stuck with interrupts off, a kernel fault) the chord is not
  read. Opening the overlay during a terminal selection drag also takes the output
  lock once, to end the drag.
- **Not covered, a display that stops completing frames.** Nothing can be drawn;
  the drivers' timeouts mark the display unavailable.
- **Not covered, a stuck ACPI worker or flush.** The overlay keeps showing
  "checking storage" until it returns; force follows a completed failure and does
  not preempt a stuck worker.

In those cases a short press of the power button asks the ACPI worker directly,
and holding it forces power-off without flushing. These limits are also in
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1323).

## Debugger and panic

Panic disables interrupts and halts, or with the
[network debugger](network-debugger.md) enabled stops in it for good; no key is
read, so the chord does nothing. Panic text goes directly to the screen where the
display allows it, over the overlay if shown (VirtIO stays serial-only), and to
serial, UDP and the log ring. Only the firmware's forced power-off works. A
stopped kernel debugger (all-stop, or QEMU's stub) reads no input; keys pressed
while stopped may be processed after resume, opening the overlay, which acts only
after a further choice. The overlay draws only on the BSP in normal operation,
never from a stopped context.
