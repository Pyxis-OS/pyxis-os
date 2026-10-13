# Mouse input

The PS/2 mouse on the 8042's auxiliary port delivers relative motion, wheel
counts and button state through
[`mouse_read_event()`](../../include/kernel/mouse.h). One BSP kernel task
consumes them; the call is nonblocking and preserves interrupt state. This is
QEMU's default mouse. On the ThinkPad, the Synaptics touchpad in its firmware
relative mode reports as a standard PS/2 mouse without a wheel (device ID 0).
TrackPoint motion and buttons arrive through the same stream, and firmware
tap-to-click and tap-and-drag work; there is no scrolling in this mode.
The opt-in absolute path below is awaiting native qualification; ordinary boots
keep this relative behaviour. [USB report mice](usb-hid.md) are separate
sources feeding the same system pointer.

Events carry raw device counts without acceleration. Signs follow the display:
+dx is right, +dy is down and +wheel scrolls toward the user. The device's own
+Y is up, so the decoder negates it. `buttons` holds the left, right and middle
buttons after the packet. QEMU and some devices also send a packet with all
deltas zero, for example when a wheel step ends.

The presentation task drains these events through the shared input coordinator
into the kernel-owned physical pointer and routes ordinary input to the shown surface. The
[system pointer interface](../interfaces/pointer.md) documents graphics sessions,
terminal control, geometry, cursor images, lock and source-loss handling.
[Mousetest](../userland/mousetest.md) provides an interactive consumer; the
[qualification report](../development/system-pointer-qualification.md) records
runtime checks and limits.

## Controller and setup

The [keyboard](keyboard.md) and mouse share one controller driver,
[`ps2.c`](../../arch/x86_64/ps2.c). Each received byte goes to the keyboard or
mouse queue according to the controller status register's auxiliary bit,
whichever of IRQ 1 or IRQ 12 announced it. Setup runs once on the BSP with
interrupts disabled. **A mouse failure leaves the mouse unavailable and never
fails keyboard setup or boot.** If keyboard setup fails, the mouse is not
attempted.

1. ACPI provides the IRQ 12 route alongside the keyboard's. An invalid IRQ 12
   override, or a route on another I/O APIC, beyond its inputs or sharing the
   keyboard's GSI, leaves the mouse unavailable. Without a route no auxiliary
   command is sent.
2. While the keyboard port is disabled, the controller must show a working
   second port: enabling and disabling it toggles the configuration bit, and
   the port test returns 0. A single-port controller would pass mouse commands
   to the keyboard, so this check precedes them.
3. After the keyboard selects scan set 2, and before it starts scanning, the
   auxiliary port is enabled and the mouse is reset, set to defaults, and
   probed for an IntelliMouse wheel (sample rates 200, 100, 80, then device
   ID 3). The sample rate returns to 100. Reset waits up to 4 s for its ACK and
   again for its self-test result, and other replies up to 500 ms. These are
   Linux libps2's bounds: Synaptics devices such as the ThinkPad's touchpad
   finish the reset before ACKing it. A responsive device costs nothing extra;
   a port with no device can add about 4 s to boot before the mouse is marked
   unavailable.
4. After keyboard scanning starts, the mouse enables reporting. Keyboard bytes
   that arrive while waiting for its ACK enter the keyboard queue. IRQ 12 is
   unmasked last.

A failure at any step logs the step, controller status and last reply, then
disables the auxiliary port and its interrupt.

## Packets

Packets are 3 bytes, or 4 with the wheel. The IRQ only queues bytes; the
consumer decodes them. A first byte without bit 3 set is skipped, which
realigns a shifted stream. A packet with an X or Y overflow bit keeps its
buttons and wheel but reports no motion. A full queue or a controller error
discards queued bytes and any partial packet, then the next read returns a
reset event; the consumer must release any buttons it considers held.

The shared coordinator applies per-source physical hold/loss quarantine and
scroll direction; decoders do not choose natural/traditional direction.

`unsynchronized_bytes`, `overflowed_packets` and `lost_inputs` in
[`mouse.c`](../../arch/x86_64/mouse.c) count these cases for debugger
inspection.

Relative resynchronization relies only on bit 3. A device that drops a byte
mid-packet can produce misaligned packets until a first-byte check fails; there
is no inter-byte timeout. During setup, a packet byte from firmware-enabled
reporting that equals an ACK or error reply can be mistaken for one; the result
is at worst an unavailable mouse for that boot.

## Synaptics opt-in absolute input

`ps2.synaptics=1` requests basic six-byte absolute W at boot. Default omission
or `ps2.synaptics=0` keeps firmware-relative input during qualification.
`ps2.synaptics.diagnose=1` prints query results and a bounded contact summary at
info level; without the option there is no new default-boot logging. Diagnosis
alone queries then restores relative input. No runtime setter or public raw
contact interface is added.

Setup in [the arch decoder](../../arch/x86_64/ps2/synaptics.c) disables reporting,
checks identity/model/capabilities, reads advertised query pages, validates
usable min/max coordinates and counts/mm, and checks mode readback plus a forced
master packet before publishing the absolute format. Serial-number queries are
never sent. Basic W leaves extended-W/AGM and four-byte guest negotiation off.
Geometry is queried, except for one owner-measured T14 Gen 1 AMD calibration
matched to the complete non-serial query fingerprint and raw bounds/resolution.
The owner measured 102x70 mm three times: X45 implies 98.04 mm usable width,
and queried Y54 implies 65.41 mm height. The roughly 2 mm borders are an authored
allowance pending native observed-extents qualification. Diagnostics retain raw
resolution `0,54`, report effective `45,54` and label the calibration source.
Query 01's live mode byte is excluded from matching. There is no square-unit
guessing or generic geometry default; see the
[accepted calibration](../wip/pointer-input-quality.md#t14-geometry-correction--accepted-2026-10-13).
Missing geometry without this match falls back to relative touch motion, with
firmware tap-off when proven; the owner's geometry-only exception can retain legacy taps if that
cannot be established. Such fallback does not qualify palm rejection, two-finger
scrolling or touch/TrackPoint separation. Other unsupported layouts or failed
mode negotiation recover only a proven relative format with tap-off; failure
leaves auxiliary input unavailable without affecting keyboard setup.

W hints distinguish one, two and three-or-more fingers only where advertised.
One-finger width is qualified by the width capability. Coordinates remain device
counts. The [kernel filter](../../kernel/input/touchpad.c) owns contact admission
and converts one-finger displacement to pointer pixels, and two stable fingers
to horizontal/vertical scroll through the common touchpad direction setting.
Three-or-more, ambiguous, invalid or unqualified extended contacts produce no
motion. There is no inertia, tapping, tap drag or gesture synthesis.

Initial tuning is Z-on 30/Z-off 25, strong palm at qualified W >=12 or Z >=220,
3 mm edge-origin bands with 2 mm deliberate inward entry, two stable samples,
6 pointer pixels/mm, a 0.5 mm scroll dead zone and 8 mm/detent. These are authored
starting values, not universal pressure/width scales; native contact evidence
must qualify and tune them. Strong palms remain rejected until a valid lift.
Typing from either physical keyboard suppresses touch motion and scrolling
during nonmodifier press/repeat and for 500 ms afterwards. A contact begun or
count-changed during suppression requires lift; an existing contact resumes with
a fresh baseline after the quiet interval. Lift, count change, rejection and
loss clear motion/scroll fractions; transport/queue loss requires confirmed lift.

Physical switches bypass contact rejection. A single-switch clickpad press maps
to primary click, with no zones or finger-count synthesis. Advertised guest
motion and TrackPoint L/M/R snapshots use a separate mouse-class source with
retained holds. A guest source exists only after an actual packet/switch snapshot;
initial holds are quarantined until release. The forced boot master snapshot is
consumed once and is never replayed after loss. Unsupported physical-button
layouts recover to relative input rather than silently discard switches.

Diagnosis reports pass-through capability separately from extended stick-button
capability: the latter can be absent even when a TrackPoint uses the guest port.
Five-second `synaptics-transport:` lines report cumulative decoded touch/guest
packets, guest overflows, malformed headers, unsupported packets and reset
packets; reporting stops after 150 seconds. These are observations, not guest
initialization or proof that every physical button has been exercised.

Malformed absolute framing, raw queue loss and controller errors reset both
logical sources, retaining the existing OR/lock-revocation/held-click rules.
A spontaneous reset (`AA 00`) stops auxiliary input for the boot, so changed
relative bytes never reach an absolute decoder; keyboard and USB input continue.
There are no runtime PS/2 commands. QEMU checks ordinary relative coexistence and
unrecognized-device fallback, not pressure, width, clickpad or TrackPoint routing.
See [task 2 qualification](../development/experiments/pointer-input-quality/README.md#task-2-before-code-baseline)
for the owner's required native pass before default-on.

## Manual inspection

From QEMU's monitor (Ctrl-a c on the serial terminal), `mouse_move 10 -5`
moves right and up, `mouse_move 0 0 1` is one wheel step away from the user,
and `mouse_button 1`, `2` and `4` press left, right and middle (`0` releases).
Under KVM, a hardware breakpoint on the instruction after `mouse_read_event()`
returns in `space_pointer_sync_input()`, conditioned on a true result, can print
each event. A hardware breakpoint set before boot can stop inside setup.
