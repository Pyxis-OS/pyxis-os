# A system pointer

Status: **tasks 1 and 2 merged; task 3 delivered for review by Codex alpha, 2026-10-08.**
[Pyxis #545](https://git.internal/PyxisOS/pyxis-os/pulls/545),
[userland #164](https://git.internal/PyxisOS/pyxis-userland/pulls/164) and
[ports #65](https://git.internal/PyxisOS/pyxis-ports/pulls/65)/
[#66](https://git.internal/PyxisOS/pyxis-ports/pulls/66) are merged.
The parent merge is `abbeded`; published pins are userland `b83ff67` and ports
`a642f07`. Task 3 starts from that fresh main on `pointer/terminal-selection` in
[draft #550](https://git.internal/PyxisOS/pyxis-os/pulls/550).
It includes local TTY retention/selection, trusted mux terminal control,
terminal spatial readiness, mux hit-testing/selection/wheel history and the
owner's addition of graphics pointer subscription readiness through `wait_many`.
The SDL blocking-event adapter fix remains separately assigned to beta.
Task 3 is delivered in #550 with published [userland #166](https://git.internal/PyxisOS/pyxis-userland/pulls/166)
`63d4324`; merge that dependency before the parent.
[Task 3 qualification](../development/system-pointer-qualification.md#task-3-qualification)
records baseline, input/overlay/capture/wait checks and source-only limits.
Tasks 4 and 5 still require separate authorization.

The proposal merged as [Pyxis #530](https://git.internal/PyxisOS/pyxis-os/pulls/530).
All three original decision rounds are accepted on 2026-10-08; round three was
recorded in the first task 1 commit. Task 3's first decision round is
[accepted, 2026-10-08](#task-3-planning), and implementation is authorized.

[Qualification](../development/system-pointer-qualification.md) records the
pre-code task 1 baseline, joint task 1/2 validation and the fresh-main task 3
baseline. Default-image integration and exact submitted-head checks passed for
tasks 1 and 2. Task 3's baseline was captured before any task 3 code change.

## Pre-milestone baseline

This describes main before task 1; implemented ordinary behavior is now in the
[mouse reference](../devices/mouse.md), with the qualification limits recorded above.

The [PS/2 mouse driver](../devices/mouse.md) supplies relative counts, button
state and wheel counts. A per-space pointer session is exclusively acquired
by one process. Quake and `mousetest` consume it; there is no system cursor or
terminal pointer destination. Focus is the active space, except that showing
a presented graphics session's terminal layer withholds captured input.
Keyboard, display and pointer acquisition are independent.

The [multiplexer](../userland/multiplexer.md) now owns pane cells, layouts and
1,024-row histories. It composes these through the outer CONSOLE and delegates
the shared-space graphics/pointer grants to pane shells. Graphics launched
from a pane covers the space's graphics layer, not a pane rectangle.
Acquiring today's exclusive pointer session for mux would block those programs
and still would not deliver terminal selection events while graphics is hidden.

The kernel local TTY retains a raster, not selectable text cells. Its block
text cursor is drawn during presentation; references to a software cursor in
the [capture contract](../interfaces/screen-capture.md) currently mean that
text cursor. The [presenter](../../kernel/space.c) copies the whole chosen
surface and navigation at about 60 Hz. Application buffers can change without
a new PRESENT. VirtIO has full-frame transfer/flush and no enabled cursor queue.

## Owner decisions

Accepted 2026-10-08:

1. **Where the work lives.** The kernel draws the cursor, routes pointer input
   to the surface under it and handles tab-bar clicks. Selection and copy work
   in the kernel's local terminals as well as in the multiplexer. Scrollback and
   wheel scrolling live in the multiplexer.
2. **Programs supply their own cursors from the start.** No single cursor
   graphic is built in as the only option: a program, a game for example, can
   set the cursor image for its surface. VirtIO GPU's hardware cursor layer can
   show it; other displays need the presenter to draw it.
3. **Super+Esc always unlocks a locked pointer,** whatever the program asked
   for. After that, the program cannot lock the pointer again on its own; only
   the user's click on its surface allows a new lock, as browsers do.

First-round defaults accepted by the owner on 2026-10-08:

4. **Image contract.** Tightly packed BGRA8 with straight alpha, dimensions
   1–64 pixels per axis, an in-image hotspot and a kernel default until the
   surface owner supplies an image.
5. **Routing and geometry.** Separate terminal and graphics surface ownership;
   surface-local pixel coordinates with destination geometry generation and
   mapping/view identity.
6. **Lock lifetime as proposed.** Initial lock is allowed on focused presented
   graphics. Space/layer or device loss revokes it and requires a fresh surface
   click, as does Super+Esc. Resize/REPLACE of the same graphics session retains
   lock. The space's user-activation prohibition survives session reacquisition
   and process changes; a program cannot reset it by restarting itself.

Second-round defaults accepted by the owner on 2026-10-08:

7. **Bounded surface-owner warp.** The owner of shown, focused, unlocked graphics
   may move the pointer within its visible mapping/destination intersection,
   validated against geometry identity. The move updates kernel position and
   reports ordinary motion, never user activation or permission to relock.
8. **Terminal-controller grant.** Trusted local startup gives mux a separate
   grant tied to its outer terminal, withheld from pane children. Acquisition
   is exclusive and process-owned; a second acquisition returns busy. Release
   or owner exit restores kernel handling. Handle copies/closure neither
   transfer nor release ownership.

Third-round choices accepted by the owner on 2026-10-08:

9. **Selection and export boundary.** Visible-cell linear selection in local
   terminals and mux, with mux wheel/history browsing. Clipboard publication
   and paste are deferred to the clipboard milestone.
10. **Presentation and capture.** Software cursor first, then VirtIO's hardware
    cursor. Screenshots include visible system cursors; hidden/locked cursors
    are omitted. Keep full-frame cadence and defer general damage tracking.
11. **Task order and closure, with the owner's change.** Keep the five-task order
    and SDL2 migration after its milestone closes. Matched QEMU checks suffice
    to close the milestone. Native PS/2 ThinkPad validation is deferred while
    the machine is used for the Bluetooth investigation; the owner will run a
    batch of native checks once that investigation finishes. The consequence
    and revisit point are recorded in
    [technical debt](../technical-debt.md#native-system-pointer-qualification).

## Accepted scope

One system pointer on the existing local display, PS/2 only. Include program
cursor images, tab clicks, ordinary surface positions, game lock, local TTY
selection and mux selection/wheel scrolling. Preserve the existing
[space-layer choice](../userland/space-layers.md), hidden rendering and keyboard
routing. An unlocked pointer does not release keyboard capture or pause a game.

Include a VirtIO cursor backend after a common software path works; boot and
Bochs keep software composition. Coordinate changes with the separately assigned
[SDL2 backend](../development/sdl2.md), replacing its private integration of relative counts.
Its pointer adapter change lands **after the SDL2 milestone closes**, including
its DevilutionX consumer and that milestone's accepted qualification. Do not
change that milestone's tasks 4–5 or build a second SDL2 port.

Selection produces a text source for the later [clipboard](clipboard.md)
proposal. Clipboard publication, paste, converters and sharing are not built
here. The accepted copy direction remains part of the eventual user behavior;
selection alone must not be advertised as a working Copy command.

USB HID mice, Synaptics absolute-mode scrolling, Bluetooth, multiple displays,
window composition, acceleration settings, remote pointer transport and general
presentation damage tracking are outside this milestone. Local mux gets pointer
input; remote mux still has its existing keyboard controls.

## Devices

This milestone uses the existing [PS/2 mouse](../devices/mouse.md), including
the ThinkPad's relative-mode touchpad and TrackPoint. Their firmware stream has
no wheel or multi-finger scrolling. USB HID mice remain a separate track after
the [interrupt-IN foundation](../devices/usb-interrupt-in.md); neither USB HID
nor the [Bluetooth investigation](../development/bluetooth-investigation.md) is part of task 1.

## Cursor images and authority

**Accepted image contract, 2026-10-08:** tightly packed **BGRA8 bytes with
straight alpha**, independent of native scanout channel shifts. Each dimension
is 1 through 64 pixels; the
hotspot is an integer pixel inside the image. The hotspot sits at the pointer
position, and the image is clipped at physical screen edges. Do not scale images
on resize. Fully transparent pixels preserve the background; software blending
uses the image's alpha, with alpha representation converted if a backend needs
it. No animation, image handles or unbounded stride are needed in the first slice.

The payload is at most 16 KiB. This accepted architectural capacity bound was
chosen to fit the VirtIO cursor resource, whose size is 64x64 in the
[OASIS specification, section 5.7.6.6](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.pdf).
Smaller images are padded transparently for that backend. It does not make
native graphics pixels alpha-bearing or impose this bound on application buffers.

Setting an image validates dimensions, hotspot and byte extent, copies the
pixels into kernel-owned storage, then replaces the previous image atomically.
Failure preserves the previous image. The caller may immediately reuse its
source bytes. The presenter retains a bounded image snapshot for each frame;
replacement or owner exit cannot free pixels it is reading. Allocation and
publication follow the existing BSP ownership rules, outside the output lock.

Separate surface ownership and the terminal-controller grant are accepted on
2026-10-08:

- The process owning the graphics session may subscribe to its ordinary pointer
  stream and set that session's cursor, with explicit pointer-input authority in
  the same space. Copying a grant does not transfer ownership. A hidden owner
  may update its own image for the next visit, but cannot change the shown cursor.
- Boot grants terminal control to a local space's trusted init
  when mux startup is configured; the trusted init/session handoff passes it
  to mux, tied to its outer terminal. It controls that surface's cursor and
  spatial events. It is separate from terminal-session creation authority;
  ordinary console READ/WRITE does not confer it. Pane children receive no
  such grant.
- Without a terminal controller, the kernel handles the local TTY. Navigation,
  unused margins and the kernel log space use a kernel default. Kernel defaults
  include an arrow and a terminal selection cursor; programs can supply their
  images from the first implementation task. There is no global cursor-setting
  grant. The accepted warp authority is limited to graphics, as described below.

**Explicit hidden state:** distinguish the default cursor, a supplied image and
a hidden cursor for each owned surface. Hiding needs no transparent 1x1 image and
does not change the accepted 1–64 image dimensions. It retains the image so showing
again restores it, or the default if none was supplied. The surface owner may set its
own visibility, including while unlocked; this changes drawing, not input routing,
position, focus or lock permission. Lock forces effective visibility off, and
unlock restores the surface's saved visibility preference. Navigation and margins
retain the kernel default regardless of a program's preference. Release/exit
removes the surface preference with its image. Software composition, VirtIO cursor
state and capture all omit a hidden pointer.

This supports programs drawing their own cursor: the SDL adapter maps
`SDL_ShowCursor(SDL_DISABLE)` to the owning surface's hidden state. It must not
draw a second system cursor over DevilutionX's software cursor.

Selection of the shown image follows hit testing, not the last program to set
an image. Session release or exit removes its image and input ownership; terminal
controller release or exit returns terminal routing to the kernel. Acquisition
is exclusive and process-owned; even closing the last handle does not release it,
and copied handles surviving owner exit do not keep it acquired. Closing/copying
handles does not transfer a process-owned subscription. Process cleanup must
invalidate subscriptions before reclaiming their referenced surfaces.

## Coordinates, routing and resize

Surface-local coordinates and both geometry identities are accepted on
2026-10-08. The detailed movement and queue rules below remain recommendations.

Recommend one kernel-owned physical position, initialized at screen center,
with one pixel per PS/2 count and no acceleration. Clamp the hotspot to the
physical screen. Use checked geometry conversions and saturating delta/wheel
accumulation; an overflowing addition cannot be repaired by clamping afterward.
In lock mode, hide the cursor, leave its position parked and route relative counts
only. Restoring ordinary mode uses the parked position, clamped if resized,
and the surface's saved visibility preference.

Ordinary motion, button and wheel events carry **surface-local pixel positions**:
origin at the content's top-left, below navigation. Hit-test the visible layer of
the active space. A graphics surface receives only positions in the top-left
intersection of its mapping and current destination. Exposed background margins
receive no program input; the kernel owns their default cursor. Terminal events
use the whole terminal content rectangle, including its cell-edge margins; the
terminal handler maps pixels to cells and ignores unused cell margins.

Report the destination geometry generation and the identity of the presented
mapping/view with spatial events. Physical resize and graphics REPLACE are
separate changes: an old mapping keeps its own fixed extent until replaced,
even while the destination changes. REPLACE without a physical resize must
still invalidate old spatial events. A client re-queries geometry when either
identity changes; it never stretches pointer positions to fit an old mapping.
The proposal specifies these semantics, not ABI structs, operation numbers or
placeholder functions.

At resize/REPLACE, discard queued ordinary spatial input, publish a geometry/reset
notification and cancel active drags. For ordinary streams, clear accepted held
buttons and require a release/fresh press, as on today's focus/reset boundaries.
The next ordinary position uses committed geometry. The accepted lock rule keeps
a lock across resize or REPLACE of the same graphics session, preserving its
accepted button state:
relative counts have no surface coordinate to reinterpret. Failure preserves
the last valid geometry and mapping.

Routing destinations are independent of keyboard capture:

| Shown region | Pointer destination |
| --- | --- |
| Navigation tab | Kernel tab selection |
| Ordinary local terminal | Kernel selection handler |
| Terminal controlled by mux | Mux's terminal spatial queue |
| Presented graphics intersection | Its graphics surface subscriber |
| Unowned background or kernel log content | Kernel default; no program stream |
| Locked graphics | Its lock owner; relative motion, buttons and wheel |

Hover does not change keyboard focus or switch spaces. A fresh left press on a
visible tab selects it, including the already selected tab as a no-op; use the
bar's actual clipped tab rectangles. Unshown tabs and bar gaps are not targets.
Consume the full tab click, including its release, so it cannot become a press
or release in the new space. No new tab dragging or bar-scrolling controls.

Recommend retaining the press destination until release while it remains the
shown surface, allowing terminal selection to reach its edges. During such a
drag, signed coordinates may lie outside that surface's rectangle; its handler
clamps selection endpoints. All further presses and motion stay with that
destination until all its accepted buttons are released. Wheel events follow the
hovered surface and report only that recipient's accepted buttons, never another
surface's drag state. Without a drag, surface enter/leave notifications let a
subscriber clear hover/held state even when no further input targets it.
A drag crossing
navigation does not activate a tab without a fresh press. Switching spaces,
layers or owners cancels the drag, clears queued events and releases accepted
buttons. Programs receive no hidden-layer events; fresh input is required when
a surface returns. Cursor images remain saved with their owning surfaces.

Provide a typed terminal spatial queue separate from stdin and from the graphics
pointer subscription. Mux must wait on its readability together with its existing
outer-input/pane-output/lifecycle interests, rather than poll. Its current maximum
is 17 of the native 32 interests, so one additional interest fits. Both graphics subscriptions and terminal spatial queues now have native
READABLE readiness, implemented in task 3 through the existing worker. Preserve bounded queue/reset
semantics: coalesce positions only within the same surface/geometry/button state,
retain the latest position, accumulate wheel counts, and reset on lost transitions.

## Surface-owner warp

**Accepted, 2026-10-08.** DevilutionX's `SetCursorPos` calls
`SDL_WarpMouseInWindow` during keyboard/mouse interaction. Today SDL owns its
position; retaining only that private warp once the kernel owns position would
make motion and the drawn cursor disagree. The adapter must not report a
successful local-only warp.

Allow the owner of the **shown, focused, unlocked graphics surface**
to move the pointer within that surface's visible mapping/destination intersection.
The request uses surface-local pixels and the current destination and mapping
identities. Reject stale geometry, out-of-bounds destinations, hidden/inactive
surfaces, a locked pointer or a press held for another destination; do not clamp
a bad request into navigation or another surface. No global or terminal-controller
warp is proposed.

A successful warp updates the authoritative kernel position and queues an
ordinary position event for that surface, with unchanged accepted buttons and
no wheel movement. It creates no device counts, button transition or user
activation, so it cannot authorize relock. Failure preserves the old position.
SDL uses the resulting native position/event rather than maintaining a second
authoritative position. Warping a hidden cursor within an otherwise eligible
surface remains possible; visibility alone is not focus or lock.

## Pointer lock and Quake migration

**Lock lifetime accepted, 2026-10-08.** Allow an initial lock request when the
requesting process owns the focused, presented graphics surface and its ordinary
pointer subscription.
Acquiring input alone must not lock it. Deny lock while hidden or inactive and
never change space/layer selection to satisfy a request. There is one global lock.

Super+Esc is intercepted before keyboard capture. Either Super key suffices,
including when Shift, Control or Alt is also held: the accepted escape must remain
available. Consume the Escape press, repeats and matching release, including if
Super is released first. Unlock publishes explicit lock-state/reset information,
flushes pending relative motion and clears accepted buttons. It does not emit a
game Escape, select the terminal, release keyboard capture or stop execution.

After this escape, keep a kernel-owned relock prohibition in the space, across
pointer/graphics release and reacquisition and process exit. Neither restarting
the program nor handing grants to a child clears it. Only a fresh left click on
the currently shown graphics surface permits one subsequent lock request by that
surface's owner. Permission is bound to that session and is lost if it ends;
it cannot be saved for another surface or transferred to another process.
Consume that activation click's press/release rather than injecting an accidental
shot. No synthetic call or already-held button counts as user activation.

Revoke a lock on space/layer loss, owner exit/release or device loss.
After a focus/device-loss revocation, require a fresh surface click before relock
as well, so switching back does not unexpectedly hide the cursor. Voluntary
unlock can be followed by another request while still focused, unless the space
requires new user activation. The initial-lock rule applies only while that
space has no activation prohibition. Once required, user activation cannot be
recreated through session lifetimes or process identity changes.

Quake currently acquires the optional pointer once and treats focus as permission
for gameplay motion. Replace that assumption with an ordinary surface subscription
and an explicit lock request after first PRESENT. Apply gameplay deltas, buttons
and wheel only while the authoritative lock state says locked. Unlock/loss uses
its existing input reset to clear pending motion and held controls; continued
rendering and keyboard-only operation remain available. A refused lock is an
unlocked state, not a successful relative-mode switch. After user activation,
Quake may request lock again; its polling loop cannot repeatedly relock itself.

`mousetest` exercises ordinary positions instead of maintaining its own position
from counts, removes its independently drawn pointer marker, and supplies its
cursor image. Migrate other in-tree pointer consumers, including the current SDL2
adapter, together with replacement of the old exclusive relative-only contract.
SDL ordinary motion uses kernel positions; SDL relative mode requests lock and
observes refusal/revocation. Cursor creation/show/hide and bounded warp use native
surface state. Land this adapter change after SDL2 milestone
closure and qualify DevilutionX ordinary motion, warp, software-cursor hiding and
its program-supplied cursor. Do not preserve the old protocol merely for ports.

## Input-source coordination

Coordination on 2026-10-08 first inspected a proposed contract in
[Bluetooth #548](https://git.internal/PyxisOS/pyxis-os/pulls/548); the owner has
since accepted its source-loss adjustment. Per-source physical button snapshots
are aggregated by OR. Loss resets accepted input/cancels drag if the lost source
held buttons, and revokes lock if it held buttons or no live source remains.
A buttonless loss with a live surviving source leaves visible input/lock state
unchanged. These are the accepted future integration rules, not a second source
implemented by this pointer task.

The existing PS/2 adapter owns continuity quarantine separately from its physical
snapshot. The common reset/loss hook accepts the remaining physical mask,
preserves position and resets accepted input/drag/activation while revoking the
lock. PS/2 is the sole implemented source and currently supplies zero on stream
loss. Current stream-discontinuity quarantine and pre-report lock/warp refusal
remain intact. Availability and quarantine queries stay at the adapter boundary.

A later producer aggregates complete physical snapshots and continuity
suppression before routing; submitting independent source masks here would
incorrectly release another source's hold. Its adapter decides the two accepted loss conditions separately: accepted-input
reset/drag cancellation needs a lost held button, while lock revocation also
applies when no live source remains. The existing combined hook is used for
PS/2 stream discontinuity; it is not a completed implementation of those future
conditional branches. That integration must preserve surviving physical masks
and replace the PS/2-only availability view with its live-source view. Producer authority, epochs, sequences and
reconnect implementation belong to that separately assigned Bluetooth track;
this task adds none of them and changes no consumer ABI for a second source.

## Selection and clipboard boundary

**Accepted, 2026-10-08.** A left drag selects a linear range of cells, with release
finalizing it. No word/line multi-click modes or rectangular selection in this slice. A new
selection replaces the old one. Cancel a drag on reset/geometry change; invalidate
selection when selected content is changed or evicted, rather than silently copy
replacement text. Output keeps running. Layer hiding suspends selection input;
unchanged completed selections may remain when the terminal returns.

For the kernel local TTY, retain its visible character cells alongside the raster
and update them through printing, erasure, scroll, clear and transactional resize.
Preserve the same cropping and no-reflow behavior as pixels. This adds checked,
geometry-sized storage per local TTY, not kernel scrollback. Selection highlighting
is a presenter overlay; underlying text and application output remain intact.
There is no pixel-to-text reconstruction and no early-console selection work.

For mux, the kernel delivers pixels and generation for the outer terminal. Mux
hit-tests its own visible pane rectangles, translates to pane cells and selects
from that pane's retained live/history cells. A content click selects keyboard
focus; a drag stays anchored to its originating pane and clamps at its content
edges. Headings may focus a pane but are not copied. Dividers, global footer and
other panes are excluded from the selected text. Hidden/clipped cells cannot be
selected through the outer view. Pane closure/layout change cancels its selection
and discards pending spatial events before input can target the new layout.
The kernel does not parse mux output to infer pane boundaries.

Wheel over pane content selects that pane's history viewport without changing
keyboard focus. Recommend three rows per wheel detent, with the existing sign
convention and bounds, returning to live at the newest endpoint; this step is an
initial UI choice, not a kernel protocol limit. Explicit keyboard history controls
keep working. Drag autoscroll and selection across off-view history wait; history
browsing can first bring the desired text into view. The plain kernel TTY has no
scrollback, so a wheel there has no scrolling effect.

On an eventual explicit Copy action, the selection owner freezes selected text
into an owned object. Join selected physical rows with line feeds, omit unselected
cells and styling, and trim terminal padding at row ends; no soft-wrap inference
or Unicode-width terminal expansion is implied. Retained cells are currently
8-bit glyph indices, not UTF-8 decoding. The clipboard proposal must specify
that encoding's text representation before calling the result `text/plain`;
this milestone must not mislabel arbitrary terminal bytes as UTF-8.

Follow the clipboard's accepted direction: per-space and shared stores, MIME-typed
objects with a text form, owned data surviving source exit, and userspace conversion.
Selection is not automatic publication to either store. Clipboard choice, explicit
Copy/Paste gestures, cross-space authority, encoding and bracketed paste are
settled there. Do not add an ad hoc global text buffer or successful fake Copy
operation here. Kernel-local selection must eventually hand off an owned text
snapshot just as mux does; the two sources must converge on that later contract.

## Presentation cost and capture

**Accepted software-first policy, 2026-10-08.** Keep today's full repaint and
approximately 60 Hz cadence for the first software cursor. Snapshot its
position/image once per frame, compose after navigation,
chosen surface, selection highlighting and TTY caret, and send only final pixels
to the existing capture tee and driver. Stage only intersecting spans; do not
write cursor pixels into application, TTY or navigation backing and do not add
another whole-screen buffer or frame submission on each mouse packet.

On boot and Bochs, each frame already restores the old cursor location by copying
the underlying surface. A maximum-size cursor adds at most 4,096 blended pixels,
producing at most 16 KiB of final spans per frame, about 0.94 MiB/s at 60 Hz.
These spans replace their ordinary copies rather than adding device writes.
This is an arithmetic work bound, not measured bandwidth or latency: blending
also reads background/image bytes and may split bulk-copy paths. The existing
1280x800 full-screen write is about 234 MiB/s at 60 Hz before source reads.
Unselected games keep their existing rendering cost. General damage tracking
and regional framebuffer/GPU submission are separate work because mapped pixels
may change without any notification.

For VirtIO, implement the cursor queue, a transparently padded 64x64 resource,
fenced image upload and position updates under the sole BSP presenter. Pointer
motion can use that cursor layer without adding full-screen uploads, but this
milestone does not remove the existing full-frame stream. Keep resource ownership,
confirmed cleanup and failure retention consistent with the driver's current
contract; cursor resources cannot be freed while device ownership is uncertain.

Screenshots include the visible system cursor on hardware and software paths,
using the same frame snapshot and alpha composition. With a hardware cursor,
blend it into capture backing only, while the normal scanout gets the cursor
layer, avoiding a doubled pointer. This needs an explicit capture-only path:
today's capture tee also writes every staged span to scanout. Hold one image,
hotspot, position and visibility snapshot through both paths. Capture still fills
every pixel and publishes only after successful frame submission and successful
completion of that matching hardware cursor update; a failed cursor update cannot
publish a cursor the backend did not submit. It cannot obtain hardware scanout
timing or become an atomic application screenshot. Hotspot subtraction uses signed
intermediates before clipping. Normal cursor code adds no allocation,
locking or device operation to the panic/early-console path.

Before code, take matched baselines using existing tools; repeat after software
and hardware cursor tasks. Record source revisions, display/resolution, PS/2 input,
CPU count/accelerator, repeated samples and variation. Inspect idle pointer, sustained
motion, terminal selection, Quake timedemo with/without lock, capture and live
VirtIO resize. Boot framebuffer and Bochs must both be checked, plus VirtIO.
Separate nested-QEMU results from owner-run native ThinkPad results; no cost claim
is measured by this documentation PR and no new benchmark infrastructure is implied.
Matched QEMU checks are the accepted closure gate. Native PS/2 qualification is
[deferred](../technical-debt.md#native-system-pointer-qualification) to the owner's
batch of ThinkPad checks after the Bluetooth investigation; QEMU results must not
be reported as native qualification.

## Decision status

All three rounds are explicitly accepted on 2026-10-08 and recorded under
[owner decisions](#owner-decisions). No queued scope/delivery decisions remain.

**First round accepted, 2026-10-08:** image contract, separate ownership with
surface-local coordinates/geometry identity, and lock lifetime as proposed.
They are recorded under [owner decisions](#owner-decisions); review findings
do not reopen them.

**Second round accepted, 2026-10-08:** bounded surface-owner warp and the dedicated
terminal-controller grant through trusted local startup. These are recorded under
[owner decisions](#owner-decisions). A repeat terminal-controller acquisition,
including by its owner, returns busy; it grants no graphics ownership, lock or warp.

**Third round accepted, 2026-10-08:** selection/export boundary and software-first
presentation with cursor-inclusive capture. The five-task order is accepted with
the owner's closure change: matched QEMU checks alone may close the milestone;
native PS/2 validation waits for the ThinkPad batch after Bluetooth investigation.
The deferral is recorded in [technical debt](../technical-debt.md#native-system-pointer-qualification).

The proposal is complete. Task 1 has explicit owner authorization on 2026-10-08.
Any newly discovered policy question returns to the owner in groups
of at most three, with defaults, rather than silently becoming a requirement.

## Task breakdown

Tasks 1–3 have explicit owner authorization on 2026-10-08.
Proposal review/merge does not authorize tasks 4 or 5.

- [x] **Documentation proposal.** Inspect current main and describe contracts,
  recommendations, boundaries and a task sequence without code or placeholder APIs.
- [x] **Owner review.** All three rounds are accepted, including the native
  validation deferral and five-task order. Task 1 was authorized afterward.
- [x] **1. Ordinary surface input and software cursor (merged).** Kernel owns position,
  routing, tab hit testing and ordinary subscription/geometry lifetimes. Include
  program cursor images from the start, explicit hidden state, the accepted warp
  policy, and bounded software composition on all current backends
  with capture. Add libpyxis support and ordinary-position use in `mousetest`.
  Motion, tab clicks, image/hotspot/show/hide, live resize/REPLACE and capture
  were checked interactively; source-only cases and presentation measurements
  are listed in the [qualification report](../development/system-pointer-qualification.md).
  Default-image integration now includes task 2; this checkbox records the
  merged task, not milestone closure.
- [x] **2. Lock, escape and consumer migration (merged).** Add relative lock, Super+Esc,
  durable activation gating and authoritative lock/reset notifications. Migrate
  Quake and the current SDL2 backend; complete `mousetest` migration and replace
  the old relative-only protocol with its in-tree consumers. Qualify lock/escape,
  hidden layers, process/session teardown and mouse loss. SDL2 changes land after
  its milestone closes; qualify DevilutionX ordinary motion, bounded warp,
  explicit hiding for its software cursor and its program-supplied color cursor.
  Review tasks 1 and 2 as focused dependent changes and integrate the ABI and
  consumer pins together, without publishing a broken intermediate consumer or
  retaining a legacy compatibility interface. The default image builds with
  published userland and Quake/SDL2 changes; the
  [qualification report](../development/system-pointer-qualification.md#task-2-and-joint-integration)
  records QEMU input/cursor/warp/capture checks and source-only limits.
- [x] **3. Terminal selection and mux wheel (delivered for review).** Add local TTY text retention and
  selection overlay. Give trusted mux startup the accepted
  terminal-controller grant and implement its typed spatial queue and native wait
  readiness here, where mux consumes them; make the graphics pointer subscription
  waitable through `wait_many` alongside it; these are not tasks 1 or 2.
  Mux maps events to panes, selects visible live/history text and handles wheel
  browsing without interfering with pane games. Qualify equal/BSP, focused-only
  clipping, changing output, history eviction, hidden graphics and controller exit.
  Record the owned-text handoff direction; do not build or fake the clipboard.
  Delivered with native graphics/terminal readiness in #550 and userland #166;
  [qualification](../development/system-pointer-qualification.md#task-3-qualification)
  distinguishes interactive checks from source review. This is task delivery,
  not a merge or milestone closure.
- [ ] **4. VirtIO hardware cursor.** Add cursor resource/queue ownership and uploads,
  image/hotspot changes, lock hiding and capture-only software composition.
  Qualify ordinary/captured pointer appearance, resize, focus and teardown;
  qualify DevilutionX's hardware-cursor option and compare matched software and
  hardware cost samples. Boot/Bochs retain the common software path.
- [ ] **5. Close the milestone.** Review matched QEMU behavior/cost checks on
  boot, Bochs and VirtIO; these suffice for closure. Keep the accepted native
  PS/2 deferral in technical debt for the owner's later ThinkPad batch. Rewrite
  implemented contracts into device/interface/userland references and move
  remaining work to WIP/debt.
  Keep clipboard and USB HID milestones separate.

Kernel/ABI and SDK export changes belong to Pyxis; helpers, mux and `mousetest`
to userland; Quake/SDL2 adapters to ports. Publish dependency commits and focused
PRs before parent gitlink updates, linking merge order. No compiler-container
rebuild or upstream-source addition is expected for these adapters. SDL2 timing
and ownership coordination does not authorize changes to its separate plan.

## Task 3 planning

The owner authorized task 3, including graphics pointer `wait_many` readiness,
on 2026-10-08. Graphics readiness fits the terminal readiness work: both use the
existing native BSP readiness worker, without another worker, polling loop,
wait flag or interest-bound increase. `WAIT_READABLE` observes queued records,
including geometry/focus/reset records while unfocused. It requires the correct
grant, caller's own space and acquired ownership. Ownership loss reports
`WAIT_ERROR`; hiding or source reset is a queued notification, not a persistent
error that would spin a mixed wait. Waiting retains object storage, never session
ownership, and uses the existing cancellation and scan-to-sleep notification
contract. Direct blocking reads remain available. This task supplies readiness;
it does not change SDL or Quake event-loop ordering.

Implementation breakdown:

1. Retain checked, geometry-sized local TTY glyph storage with transactional
   resize and the existing cropping/no-reflow rules. Track visible-cell selection
   under the output lock; stage highlighted rows before the caret and system
   cursor, leaving retained raster/text and the capture tee intact.
2. Add the separate process-owned terminal-controller object and bounded spatial
   queue, image/visibility preference and geometry/view identity with cell metrics.
   Acquire/release
   and process exit restore kernel terminal handling. Carry the grant through
   boot-init, trusted shell/session successors and mux; exclude pane children and
   remote startup. An acquisition failure diagnoses and stops mux, consistent
   with its existing required-grant startup behavior.
3. Add owner-validated native readiness for graphics subscriptions and terminal
   queues, publishing notification after queue locks are released. Mux adds one
   spatial interest to its existing maximum of 17, remaining below 32.
4. Mux hit-tests only its current visible pane rectangles. Focus on content or
   heading click; select only content, with anchored drag clamping. Invalidate
   stale queued events and held drag state across layout/view changes. Overlay
   selection in rendered frame copies; wheel browsing does not focus another
   pane. Keep history and keyboard controls working.
5. Qualify boot/Bochs/VirtIO overlays and capture, local output/erase/scroll/resize,
   mux equal/BSP and focused-only clipping, history mutation/eviction, hidden
   graphics, controller cleanup and native mixed waits. Publish the userland
   dependency before the parent pin, then inspect exact-head CI. Clipboard
   publication/paste, USB HID and the second input source stay out of this task.

**Task 3 round accepted by the owner, 2026-10-08:**

12. **Selection lifetime:** clear selection on local TTY scroll,
    committed resize, mux layout change or explicit history-view movement.
    Preserve it through unrelated output and color changes while selected
    characters and the visible view remain unchanged; mutation or eviction
    clears it.
13. **Wheel step and live return:** three rows per detent over pane
    content, without changing keyboard focus, and return to ordinary live input
    at the newest endpoint. Keep existing keyboard history controls.
14. **Kernel-log selection:** allow visible-cell selection in Caelum's
    kernel log through the kernel handler, without a program stream or clipboard
    publication.
