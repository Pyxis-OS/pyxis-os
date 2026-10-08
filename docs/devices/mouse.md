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
use the kernel handler or an independently acquired terminal controller. Tasks
1–3 integrate ordinary/relative graphics input, local/mux selection and native
spatial readiness. Interactive QEMU evidence, source-only
limits and the exact-revision CI location are in the
[qualification report](../development/system-pointer-qualification.md#task-3-qualification).

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
returns in `space_pointer_sync_input()`, conditioned on a true result, can print
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
Keyboard ownership remains independent. READ blocks for one 56-byte event, or
returns `CALL_TIMED_OUT` for an empty `POINTER_READ_POLL` read. Libpyxis exposes
acquire/read/release, geometry, image, visibility, warp and lock/state helpers in
`<pointer.h>`. An acquired subscription supports `wait_many` READABLE with INPUT
authority in its own space, together with keyboard, display, terminal and other
native interests. Queued state/reset events remain readable while unfocused.
Ownership loss reports ERROR; waiting retains storage without owning a session.
Readiness is advisory and consumes nothing. `mousetest` uses pointer/keyboard
READABLE and display RESIZED while retaining its existing UI deadline. The SDL
and Quake adapters still have their existing polling/event-loop ordering; SDL
blocking-event integration is separately assigned.

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
the kernel terminal cursor, including Caelum's selectable log. An acquired
terminal controller supplies its own cursor image/visibility for that terminal;
its preference never changes a shown graphics cursor. Selecting
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
ordinary input, cancel drags, clear accepted buttons and publish
`POINTER_GEOMETRY_CHANGED`. In the same locked graphics session, resize and
REPLACE retain the lock, queued relative input and accepted buttons while
reporting geometry. Clients retain held controls for that locked notification
and clear them on ordinary geometry, focus, boundary and reset notifications. A packet with no relevant change produces no
ordinary event.

The queue holds 64 events. At capacity, an ordinary event can coalesce with the
newest ordinary event only when buttons and both identities match: it retains
the latest position and saturates wheel and locked relative counts at the signed
32-bit limits. Otherwise the queue and accepted buttons are cleared and
`POINTER_STATE_RESET` is queued. Reported source loss resets every space's
accepted input and revokes the global lock, while the source adapter supplies the
remaining physical button snapshot.
PS/2 is currently the only source, so its loss supplies zero. Continuity quarantine
lives in that adapter and blocks held bits until release, preserving lock/warp
refusal while the first complete post-loss snapshot is still unconfirmed.
Notifications wake blocked readers while graphics is hidden or its space is inactive; a read
may otherwise continue waiting. Focus loss preserves execution authority.

### Relative lock and user escape

Header-only LOCK and UNLOCK operate on the owned subscription. STATE returns a
64-bit value containing current `POINTER_EVENT_FOCUSED` and
`POINTER_EVENT_LOCKED` flags; libpyxis exposes `pointer_lock()`,
`pointer_unlock()` and `pointer_state()`. STATE observes focus and mode;
it does not acquire lock or create user activation.

Only one graphics subscription can lock the physical pointer. LOCK requires
shown, focused graphics and can return `CALL_DENIED` for missing activation,
unaccepted held buttons or a drag anchored elsewhere; another lock owner returns
`CALL_BUSY`. Repeating LOCK for its current owner succeeds. Locking selects no
space or layer and leaves keyboard capture independent. Mode transitions discard
queued input, clear accepted buttons and publish `POINTER_LOCK_CHANGED`.

While locked, `POINTER_EVENT_LOCKED` accompanies events and INPUT's signed
`dx`/`dy` are relative device counts. `x`/`y` retain the parked position; ordinary
INPUT has zero relative counts. Relative motion does not move the physical
hotspot, hit-test tabs or establish ordinary drags. Wheel and freshly accepted
buttons go to the lock owner. The system cursor is forcibly hidden without
changing its saved image/visibility preference. Unlock restores ordinary routing
at the parked position, clamped to committed physical geometry, and restores that
preference. WARP is denied while locked.

Either Super key with Escape revokes lock before keyboard capture, including
with Shift, Control or Alt also held. The Escape press, repeats and matching
release are consumed even when Super is released first. The game remains
running on its selected layer with keyboard capture intact. Focus/layer loss,
device reset, subscription/display release and owner exit also revoke lock.

After such revocation, the space retains a fresh-activation requirement across
new subscriptions, display sessions and processes. A fresh left press on its
shown graphics surface consumes that press/release and queues
`POINTER_ACTIVATED`, permitting the owner to request lock again. Tab clicks,
warp and polling do not grant activation. Successful relock consumes the
permission; focus loss and session teardown clear any unused permission.
Voluntary UNLOCK creates no new activation requirement and does not remove an
existing one. This gives programs an explicit escape path without polling
LOCK until it succeeds.

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
Locked mode, a drag anchored elsewhere or a consumed tab/activation press returns
`CALL_DENIED`.
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

## Quake and SDL consumers

Quake acquires graphics before pointer input, presents its first frame, then
requests lock. It reads `dx`/`dy` only from locked input, re-queries STATE to
notice revocation and requests relock on a fresh surface activation rather than
polling LOCK. Same-session locked geometry notifications retain held controls.
Cleanup releases pointer input before graphics; keyboard-only play remains
available without a mouse.

SDL acquires pointer input only after graphics backing exists and allows relative
mode only after the first PRESENT. Its adapter maps ordinary surface positions,
locked relative counts, geometry identities, cursor image/show/hide and bounded
warp to the native protocol. Refused lock becomes an SDL error; revoked lock
clears SDL relative mode and accumulated motion without a synthetic warp or an
automatic relock. Window cleanup releases pointer input before graphics. The
normal image build includes these consumers; runtime qualification and CI are
tracked in the [qualification report](../development/system-pointer-qualification.md).

## Terminal control and selection

The separate [terminal pointer protocol](../../include/abi/terminal_pointer.h)
controls one space's local outer terminal. SPACE_FACTORY CREATE's explicit
`SPACE_CREATE_TERMINAL_CONTROL` opt-in mints a CONTROL grant for the trusted
first init, named `terminal_pointer`. Boot init sets it only for configured mux
startup; trusted shell/session successors carry it to mux. Ordinary shells,
pane children and remote clients do not receive it. Console READ/WRITE and
terminal-session creation rights confer no control. The native protocol is
separate from graphics INPUT; both owners can coexist in a space.

ACQUIRE is process-owned and exclusive, with repeated acquisition returning
BUSY. It owns the terminal view even without a live mouse, allowing keyboard
mux startup; cursor visibility still follows source availability. Handle closure
or copies do not release/transfer ownership. RELEASE or owner exit clears its
queue, drag and cursor preference and restores kernel terminal handling. It
confers no graphics lock/warp/acquisition. The common BGRA8 image/default/show
operations retain the graphics image bounds and copy/lifetime guarantees.

READ returns one shared 56-byte spatial record, blocking or with POLL. The
separate bounded 64-record queue has the same coalescing/reset rules and native
READABLE readiness as graphics. Terminal GEOMETRY also supplies grid columns,
rows and pixel cell dimensions. Its mapping identity names the outer view,
independent of a graphics mapping. Physical resize advances it and discards old
coordinates. VIEW_CHANGED validates generation/view identity, applies pending
PS/2 reports to the old view, then advances identity and clears queued input,
accepted buttons and anchored drag. A held button needs release/fresh press.
Stale identity returns BUSY without advancing the view. Mux uses this boundary
before replacing layouts or history views; the kernel never infers pane bounds
from escape output.

Without a controller, including on Caelum, the kernel retains visible 8-bit
glyph cells alongside each local TTY raster and handles a linear inclusive-cell
left selection. Complete-cell margins cannot start one; an anchored endpoint
clamps at grid edges. Release finalizes selection. Selected-character mutation
invalidates it; unrelated output and same-glyph/color changes preserve it.
Scroll and committed resize clear it. Focus/layer/stream reset cancels active
drag; unchanged completed selection survives hiding. Plain local wheel has no
scrollback effect. Controller-owned terminals bypass this kernel selection.

Selected glyphs are redrawn in contrasting colours into the presenter's existing
row staging buffer, followed by the block caret and system pointer. Backing text
and pixels remain unmodified by highlighting; capture receives final displayed
spans. Glyph allocation and transactional cropped/no-reflow resize are BSP-owned,
with publication under the output lock. This is visible storage, not kernel
scrollback, Unicode decoding or a clipboard. Selection has no Copy/publication
operation; a later clipboard task must freeze owned text and settle encoding.
