# Mouse input

The PS/2 mouse on the 8042's auxiliary port delivers relative motion, wheel
counts and button state through
[`mouse_read_event()`](../../include/kernel/mouse.h). One BSP kernel task
consumes them; the call is nonblocking and preserves interrupt state. This is
QEMU's default mouse. On the ThinkPad, the Synaptics touchpad in its firmware
relative mode reports as a standard PS/2 mouse without a wheel (device ID 0).
TrackPoint motion and buttons arrive through the same stream, and firmware
tap-to-click and tap-and-drag work; there is no scrolling in this mode.
Synaptics absolute mode and USB HID mice are not supported.

Events carry raw device counts without acceleration. Signs follow the display:
+dx is right, +dy is down and +wheel scrolls toward the user. The device's own
+Y is up, so the decoder negates it. `buttons` holds the left, right and middle
buttons after the packet. QEMU and some devices also send a packet with all
deltas zero, for example when a wheel step ends.

The presentation task drains these events into one kernel-owned physical pointer
position and routes ordinary input to the shown surface. Graphics owners use
[the pointer session](#userspace-pointer-sessions); navigation and local terminals
retain kernel cursor defaults. This task-1 integration is a draft: Quake and SDL
consumers still need the task-2 lock and protocol migration before integration.

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

`unsynchronized_bytes`, `overflowed_packets` and `lost_inputs` in
[`mouse.c`](../../arch/x86_64/mouse.c) count these cases for debugger
inspection.

Resynchronization relies only on bit 3. A device that drops a byte mid-packet
can produce misaligned packets until a first-byte check fails; there is no
inter-byte timeout. During setup, a packet byte from firmware-enabled reporting
that equals an ACK or error reply can be mistaken for one; the result is at
worst an unavailable mouse for that boot.

## Manual inspection

From QEMU's monitor (Ctrl-a c on the serial terminal), `mouse_move 10 -5`
moves right and up, `mouse_move 0 0 1` is one wheel step away from the user,
and `mouse_button 1`, `2` and `4` press left, right and middle (`0` releases).
Under KVM, a hardware breakpoint on the instruction after `mouse_read_event()`
returns in `handle_pointer_input()`, conditioned on a true result, can print
each event. A hardware breakpoint set before boot can stop inside setup.

## Userspace pointer sessions

Each space owns a pointer object. Boot gives each workload init a named `pointer`
grant, and the shell and session launcher forward it wherever they forward
`keyboard`: to a pipeline's first stage when its stdin is a console. Its `INPUT`
right authorizes [the pointer protocol](../../include/abi/pointer.h) only in the
object's own space. ACQUIRE also requires the caller to own that space's
[graphics session](../interfaces/graphics.md). Copying or closing a grant does
not transfer or end process ownership.

Acquisition is exclusive (`CALL_BUSY` for another or repeated acquisition), and
returns `CALL_UNAVAILABLE` without a working mouse. RELEASE, display RELEASE and
owner exit end the subscription and remove its image and visibility preference.
Keyboard ownership remains independent. READ blocks for one 48-byte event, or
returns `CALL_TIMED_OUT` for an empty `POINTER_READ_POLL` read. Libpyxis exposes
acquire/read/release, geometry, image, visibility and warp helpers in
`<pointer.h>`. There is no cross-session wait; applications reading keyboard
and pointer input poll or block on one session.

### Position, focus and dragging

The physical hotspot starts at screen center, moves one pixel per PS/2 count
without acceleration and stays inside the physical screen. `POINTER_INPUT`
carries signed surface-local `x` and `y` below navigation, held `buttons`, wheel
counts, destination `generation` and `mapping_identity`. Positive wheel counts
are toward the user. GEOMETRY returns destination and fixed mapping extents with
both identities. A committed physical resize changes generation; even a
same-size graphics REPLACE changes mapping identity. Clients re-query on either
change rather than stretching old coordinates.

Focus means that this space's graphics layer is selected, independently of
keyboard capture and execution. Only the top-left intersection of its mapping
and current destination receives ordinary input. Exposed margins and navigation
receive no program input and use the kernel arrow; local terminal content uses
the kernel terminal cursor, except the log space, which uses the arrow. Selecting
the terminal retains the graphics subscription while publishing focus loss.
Switching spaces or layers clears queued input and accepted buttons and reports
`POINTER_FOCUS_LOST` or `POINTER_FOCUS_GAINED`. Each event records focus in
`POINTER_EVENT_FOCUSED`. Hover boundaries report `POINTER_ENTER` and
`POINTER_LEAVE`.

A fresh press anchors a drag to its starting surface until its accepted buttons
are released. Motion and releases keep that destination, so signed positions
may extend outside it; wheel input follows the currently hovered surface.
A left press on a displayed tab selects that space and consumes the press and
release. Buttons held across acquisition, focus change or reset need a release
and fresh press before being accepted. Resize and REPLACE discard queued spatial
input, cancel drags, clear accepted buttons and publish
`POINTER_GEOMETRY_CHANGED`. Clients clear held controls on focus, boundary,
geometry and reset notifications. A packet with no relevant change produces no
ordinary event.

The queue holds 64 events. At capacity, an ordinary event can coalesce with the
newest ordinary event only when buttons and both identities match: it retains
the latest position and saturates wheel accumulation at the signed 32-bit
limits. Otherwise the queue and accepted buttons are cleared and
`POINTER_STATE_RESET` is queued. Device loss resets every space. Notifications
wake blocked readers while graphics is hidden or its space is inactive; a read
may otherwise continue waiting. Focus loss preserves execution authority.

### Images, visibility and warp

SET_IMAGE copies tightly packed BGRA8 bytes with straight alpha into immutable
kernel storage before returning. Both dimensions are 1 through 64; the integer
hotspot must lie inside the image. Failure preserves the previous image. Images
are clipped at physical screen edges and keep their pixel size through resize.
DEFAULT_IMAGE restores the kernel arrow without changing saved visibility.
VISIBILITY takes zero or one: hiding retains the image and affects drawing,
while input routing and focus continue normally. An inactive owner can update
its own preference for its next visit; navigation and margins retain defaults.

WARP takes surface-local coordinates and both current identities. It requires
shown, focused graphics and a target inside the mapping/destination intersection.
Stale identities return `CALL_BUSY`; invalid coordinates return `CALL_BAD_REQUEST`.
A drag anchored elsewhere or a consumed tab press returns `CALL_DENIED`.
Success updates the authoritative position and queues ordinary position without
synthesizing a device press or selecting a surface. Hidden graphics cursors can
still warp when these eligibility rules hold.

Ownership and image publication run on BSP with IF=0 through deferred requests;
READ and the event queue use the per-object lock. Lock order is pointer, then
scheduler queues. Allocation and user copying occur outside that lock. The sole
presenter leases one image/position/visibility snapshot through frame completion,
so replacement or exit cannot free pixels it is reading. See
[software presentation](../kernel/display.md#software-pointer).

## Mouse test program

`mousetest` needs named `display`, `keyboard`, `pointer` and `clock` grants. It
shows native surface-local positions, held buttons and wheel direction in the
left 30% of its view. The right 70% is a white drawing pad: each input event with
the left button held sets one black pixel, so fast strokes are dotted. It uses
the system pointer with a supplied 16x20 image and hotspot `(1, 1)`.

H toggles visibility, D restores the default image, C restores the supplied
image, W warps to the mapping/destination intersection's center, and Escape
releases the sessions and returns to the shell. It polls both input sessions
every 10 ms, clears held controls on state changes and adapts its mapping when
the destination generation changes. See the
[system pointer qualification](../development/system-pointer-qualification.md)
for configuration, measurements and the current runtime coverage.
