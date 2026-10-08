# A system pointer

Status: **assigned to Codex; documentation proposal, 2026-10-08.** Based on
Pyxis `9acf597`, after the multiplexer merged in
[Pyxis #523](https://git.internal/PyxisOS/pyxis-os/pulls/523) and
[userland #160](https://git.internal/PyxisOS/pyxis-userland/pulls/160).
Only the three [owner decisions](#owner-decisions) below are accepted.
Everything under **Proposed** is a recommendation awaiting the owner; neither
this document nor opening its PR authorizes code or placeholder APIs.

## Today

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

## Proposed scope

One system pointer on the existing local display, PS/2 only. Include program
cursor images, tab clicks, ordinary surface positions, game lock, local TTY
selection and mux selection/wheel scrolling. Preserve the existing
[space-layer choice](../userland/space-layers.md), hidden rendering and keyboard
routing. An unlocked pointer does not release keyboard capture or pause a game.

Include a VirtIO cursor backend after a common software path works; boot and
Bochs keep software composition. Coordinate changes with the separately assigned
[SDL2 backend](sdl2.md), replacing its private integration of relative counts.
Do not turn this proposal into a second SDL2 port milestone.

Selection produces a text source for the later [clipboard](clipboard.md)
proposal. Clipboard publication, paste, converters and sharing are not built
here. The accepted copy direction remains part of the eventual user behavior;
selection alone must not be advertised as a working Copy command.

USB HID mice, Synaptics absolute-mode scrolling, Bluetooth, multiple displays,
window composition, acceleration settings, remote pointer transport and general
presentation damage tracking are outside this milestone. Local mux gets pointer
input; remote mux still has its existing keyboard controls.

## Proposed cursor images and authority

Recommend tightly packed **BGRA8 bytes with straight alpha**, independent of
native scanout channel shifts. Each dimension is 1 through 64 pixels; the
hotspot is an integer pixel inside the image. The hotspot sits at the pointer
position, and the image is clipped at physical screen edges. Do not scale images
on resize. Fully transparent pixels preserve the background; software blending
uses the image's alpha, with alpha representation converted if a backend needs
it. No animation, image handles or unbounded stride are needed in the first slice.

The payload is at most 16 KiB. This is a proposed architectural capacity bound
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

Recommend tying cursor setting to the actual surface owner, not any holder of
the shared pointer grant:

- The process owning the graphics session may subscribe to its ordinary pointer
  stream and set that session's cursor, with explicit pointer-input authority in
  the same space. Copying a grant does not transfer ownership. A hidden owner
  may update its own image for the next visit, but cannot change the shown cursor.
- Mux receives an exclusive terminal-presentation control grant from trusted
  startup, tied to its outer local terminal. It controls that terminal surface's
  cursor and spatial events. Ordinary pane children receive no such grant.
- Without a terminal controller, the kernel handles the local TTY. Navigation,
  unused margins and the kernel log space use a kernel default. Kernel defaults
  include an arrow and a terminal selection cursor; programs can supply their
  images from the first implementation task. There is no global cursor-setting
  grant or pointer-warp operation in this proposal.

Selection of the shown image follows hit testing, not the last program to set
an image. Session release or exit removes its image and input ownership; terminal
controller release or exit returns terminal routing to the kernel. Acquisition
is exclusive and process-owned; even closing the last handle does not release it,
and copied handles surviving owner exit do not keep it acquired. Closing/copying
handles does not transfer a process-owned subscription. Process cleanup must
invalidate subscriptions before reclaiming their referenced surfaces.

## Proposed coordinates, routing and resize

Recommend one kernel-owned physical position, initialized at screen center,
with one pixel per PS/2 count and no acceleration. Clamp the hotspot to the
physical screen. Use checked geometry conversions and saturating delta/wheel
accumulation; an overflowing addition cannot be repaired by clamping afterward.
In lock mode, hide the cursor, leave its position parked and route relative counts
only. Restoring ordinary mode shows it at the parked position, clamped if resized.

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
The next ordinary
position uses committed geometry. Recommend keeping a lock across resize or
REPLACE of the same graphics session, preserving its accepted button state:
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
is 17 of the native 32 interests, so one additional interest fits. The existing
pointer object has no wait readiness support; adding the appropriate native
wake path is real work, not an assumed feature. Preserve bounded queue/reset
semantics: coalesce positions only within the same surface/geometry/button state,
retain the latest position, accumulate wheel counts, and reset on lost transitions.

## Proposed pointer lock and Quake migration

Recommend allowing an initial lock request when the requesting process owns the
focused, presented graphics surface and its ordinary pointer subscription.
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

Recommend revoking a lock on space/layer loss, owner exit/release or device loss.
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
observes refusal/revocation. Do not preserve the old protocol merely for ports.

## Proposed selection and clipboard boundary

Recommend a left drag selecting a linear range of cells, with release finalizing
it. No word/line multi-click modes or rectangular selection in this slice. A new
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

## Proposed presentation cost and capture

Keep today's full repaint and approximately 60 Hz cadence for the first software
cursor. Snapshot its position/image once per frame, compose after navigation,
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

Recommend screenshots include the system cursor, on hardware and software paths,
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

## Decisions to put to the owner

Present at most three decisions per round. Recommendations below remain **open**;
no response, PR creation or review counts as acceptance. Record an explicit owner
answer here before treating a recommendation as agreed.

**First round, awaiting the owner:**

1. Image contract: recommend BGRA8 straight alpha, 1–64 pixels per axis,
   in-image hotspot and kernel default until the surface owner supplies an image.
2. Routing/geometry contract: recommend separate terminal and graphics ownership,
   surface-local pixels, and destination plus mapping/view identity on events.
3. Lock lifetime: recommend initial lock on focused presented graphics,
   revocation on space/layer loss, retention on resize, and fresh click after
   Super+Esc or focus/device-loss revocation. Every-lock click consent is the
   alternative; it would require clicking Quake before initial mouse play.

**Queued for a later round, not additional questions in this round:** selection
interaction/export boundary; software-first then VirtIO hardware presentation and
cursor-inclusive capture; task ordering and qualification/closure limits. Their
recommended defaults are described above. Review those in groups of at most three
only after the first answers; any newly discovered policy question returns to the
owner rather than silently becoming a requirement.

## Proposed task breakdown

No implementation task is started or authorized by this proposal.

- [x] **Documentation proposal.** Inspect current main and describe contracts,
  recommendations, boundaries and a task sequence without code or placeholder APIs.
- [ ] **Owner review.** Settle the decision rounds, record accepted limits, and
  agree the implementation sequence before code. Proposal delivery does not
  complete this review.
- [ ] **Surface input, lock and software cursor.** Kernel owns position, routing,
  tab hit testing, subscription/geometry lifetimes, images and lock escape.
  Add the terminal-controller queue/readiness foundation without mux pane policy.
  Implement bounded software composition on all current backends, including
  capture. Update libpyxis and migrate Quake, `mousetest` and the current SDL2
  consumer in focused dependent PRs. Qualify cursor/lock/focus/resize and capture
  with ordinary builds, interactive QEMU and read-only debugger inspection.
- [ ] **Terminal selection and mux wheel.** Add local TTY text retention and
  selection overlay; give trusted mux startup terminal-controller authority.
  Mux maps events to panes, selects visible live/history text and handles wheel
  browsing without interfering with pane games. Qualify equal/BSP, focused-only
  clipping, changing output, history eviction, hidden graphics and controller exit.
  Record the owned-text handoff direction; do not build or fake the clipboard.
- [ ] **VirtIO hardware cursor.** Add cursor resource/queue ownership and uploads,
  image/hotspot changes, lock hiding and capture-only software composition.
  Qualify ordinary/captured pointer appearance, resize, focus and teardown;
  compare matched software and hardware cost samples. Boot/Bochs retain the
  common software path.
- [ ] **Close the milestone.** Review cost and owner-run PS/2 ThinkPad checks,
  explicitly accept any deferred qualification, rewrite implemented contracts
  into device/interface/userland references and move remaining work to WIP/debt.
  Keep clipboard and USB HID milestones separate.

Kernel/ABI and SDK export changes belong to Pyxis; helpers, mux and `mousetest`
to userland; Quake/SDL2 adapters to ports. Publish dependency commits and focused
PRs before parent gitlink updates, linking merge order. No compiler-container
rebuild or upstream-source addition is expected for these adapters. SDL2 timing
and ownership coordination does not authorize changes to its separate plan.
