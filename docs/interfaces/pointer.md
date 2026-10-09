# System pointer

One kernel-owned pointer serves the local display. The BSP presenter drains
[PS/2 input](../devices/mouse.md), maintains its physical hotspot and routes it
to the shown surface. Navigation belongs to the kernel; graphics and outer
terminal ownership are independent. [Display presentation](../kernel/display.md#software-pointer)
covers cursor composition, hardware ownership and capture.

## Userspace pointer sessions

Each space owns a pointer object. Boot gives each workload init a named `pointer`
grant, and the shell and session launcher forward it wherever they forward
`keyboard`: to a pipeline's first stage when its stdin is a console. Its `INPUT`
right authorizes [the pointer protocol](../../include/abi/pointer.h) only in the
object's own space. ACQUIRE also requires the caller to own that space's
[graphics session](graphics.md). Copying or closing a grant does
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
Readiness is advisory and consumes nothing. [Mousetest](../userland/mousetest.md)
waits on pointer/keyboard READABLE and display RESIZED with a UI deadline. SDL
and Quake retain polling in their event pumps; SDL blocking-event integration
remains deferred.

## Position, focus and dragging

The physical hotspot starts at screen center, moves one pixel per PS/2 count
without acceleration and stays inside the physical screen. `POINTER_INPUT`
carries signed surface-local `x` and `y` below navigation, held `buttons`, wheel
counts, destination `generation` and `mapping_identity`. Positive wheel counts
are toward the user. GEOMETRY returns destination and fixed mapping extents with
both identities. A committed physical resize changes generation; even a
same-size graphics REPLACE changes mapping identity. Clients re-query on either
change rather than stretching old coordinates.

Focus means that this space is active with graphics shown, independently of
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
and clear them on ordinary geometry, focus, boundary and reset notifications.
A packet with no relevant change produces no ordinary event.

The queue holds 64 events. At capacity, an ordinary event can coalesce with the
newest ordinary event only when buttons and both identities match: it retains
the latest position and saturates wheel and locked relative counts at the signed
32-bit limits. Otherwise the queue and accepted buttons are cleared and
`POINTER_STATE_RESET` is queued. Notifications wake blocked readers while
graphics is hidden or its space is inactive; a read may otherwise continue
waiting. Focus loss preserves execution authority. [Source loss](#input-source-coordination) resets accepted input and
revokes lock.

## Relative lock and user escape

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
warp and polling do not grant activation. One LOCK attempt consumes the
permission, including a refused attempt; focus loss and session teardown clear
any unused permission.
Voluntary UNLOCK creates no new activation requirement and does not remove an
existing one. This gives programs an explicit escape path without polling
LOCK until it succeeds.

## Images, visibility and warp

SET_IMAGE copies tightly packed BGRA8 bytes with straight alpha into immutable
kernel storage before returning. Both dimensions are 1 through 64; the integer
hotspot must lie inside the image. Failure preserves the previous image. Images
are clipped at physical screen edges and keep their pixel size through resize.
DEFAULT_IMAGE restores the surface default without changing saved visibility.
VISIBILITY takes zero or one: hiding retains the image and affects drawing,
while input routing and focus continue normally. An inactive owner can update
its own preference for its next visit; navigation and margins retain defaults.

The kernel defaults use opaque neutral black/white pixels and transparent
background in the same straight-alpha BGRA8 format:

| Default | Drawn shape | Image bounds | Hotspot |
| --- | --- | --- | --- |
| Arrow | 12x19 | 16x24 | `(0, 0)` |
| Terminal I-beam | 9x20, serifed | 9x20 | `(4, 10)` |

The character-row shapes are in [`kernel/pointer.c`](../../kernel/pointer.c).
These defaults do not change the ABI, image bounds or supplied program images.

WARP takes surface-local coordinates and both current identities. It requires
shown, focused graphics and a target inside the mapping/destination intersection.
Stale identities return `CALL_BUSY`; invalid coordinates return `CALL_BAD_REQUEST`.
Locked mode, unaccepted held/quarantined buttons, a drag anchored elsewhere
or a consumed tab/activation press returns `CALL_DENIED`.
Success updates the authoritative position and queues ordinary position without
synthesizing a device press or selecting a surface. Hidden graphics cursors can
still warp when these eligibility rules hold.

Ownership and image publication run on BSP with IF=0 through deferred requests;
READ and the event queue use the per-object lock. Lock order is pointer, then
scheduler queues. Allocation and user copying occur outside that lock. The sole
presenter leases one image/position/visibility snapshot through frame completion,
so replacement or exit cannot free pixels it is reading. See
[software presentation](../kernel/display.md#software-pointer).

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
Terminal DEFAULT_IMAGE restores the I-beam; graphics DEFAULT_IMAGE restores
the arrow.

READ returns one shared 56-byte spatial record, blocking or with POLL. The
separate bounded 64-record queue has the same coalescing/reset rules and native
READABLE readiness as graphics. Terminal GEOMETRY also supplies grid columns,
rows and pixel cell dimensions. Its mapping identity names the outer view,
independent of a graphics mapping. Physical resize advances it and discards old
coordinates. VIEW_CHANGED first applies pending PS/2 reports to the old view,
validates generation/view identity, then advances identity and clears queued input,
accepted buttons and anchored drag. A held button needs release/fresh press.
Stale identity returns BUSY without advancing the view. Mux uses this boundary
before replacing layouts or history views; the kernel never infers pane bounds
from escape output.

Without a controller, the kernel handles visible-cell left selection, including
Caelum's log terminal. [Local terminal selection](../userland/terminal.md#local-visible-cell-selection)
and [mux input](../userland/multiplexer.md#local-pointer-input) describe behavior
and invalidation. Highlighting changes presented pixels rather than retained
text/raster; capture receives the displayed result. Stored cells are 8-bit glyph
indices, with no kernel scrollback or Unicode decoding.

Selection has no Copy/publication operation and exports no text. A later
[clipboard milestone](../wip/clipboard.md) must define an owned snapshot and its
encoding before calling it `text/plain`. Publication, paste, converters and
cross-space authority remain separate decisions; arbitrary glyph bytes are not
advertised as UTF-8.

## Input-source coordination

PS/2 is the only implemented source. Its adapter submits a complete physical
button snapshot and continuity suppression with each normalized motion report
through [`pointer_input_report`](../../include/kernel/pointer.h). Suppression
is separate from physical state: after stream loss, held bits remain quarantined
until observed released. Pending discontinuity also refuses lock/warp before
routing the first confirmed report.

`pointer_source_lost()` accepts the remaining physical button mask, preserves
position, clears accepted input/drag/activation and revokes lock. The PS/2
adapter supplies zero on loss and resets every space's accepted input. Source
availability and quarantine remain adapter-owned.

The accepted future source integration aggregates physical snapshots by OR
and aggregates continuity suppression before routing. Loss of a source holding
buttons resets accepted input and cancels drag; lock is revoked when that source
held buttons or no live source remains. A buttonless loss with a live surviving
source leaves input and lock state unchanged. The current combined PS/2 loss
hook does not implement those conditional branches or a multi-source availability
view. Submitting independent masks would incorrectly release another source's
hold. Bluetooth producer authority, epochs, sequencing and reconnect belong to
its [separate milestone](../wip/bluetooth-mouse.md); USB HID is also deferred.
Neither adds a public pointer API here.

## Current boundaries

One local display, one physical hotspot, raw device counts without acceleration.
Remote pointer transport, Synaptics absolute-mode scrolling, multiple displays,
window composition and general presentation damage tracking remain deferred.
[Qualification](../development/system-pointer-qualification.md) records measured,
interactive and source-inspected evidence separately.
