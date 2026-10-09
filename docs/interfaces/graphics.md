# Mapped graphics buffers

Run `mandelbrot` from the shell in an application space. It draws a
pixel-resolution Mandelbrot set progressively. Hold the arrow keys to pan,
`=`/`+` to zoom in around the centre, and `-` to zoom out. Escape releases the
keyboard and display sessions and returns to the shell. Super+Left/Right switches
spaces; Super+Down shows the terminal and Super+Up restores graphics. Losing focus
clears held controls without pausing rendering; held keys need a fresh press
after returning to graphics.

The application needs named `display` (DRAW), `keyboard` (INPUT), and `clock`
(READ and SLEEP) grants. Movement uses monotonic elapsed time, with at most
250 ms of catch-up per update. While moving, frames are paced to at most 30 per
second; expensive views may render more slowly. Once rendering is complete and
no controls are held, it waits for input or resize, including while unfocused.
The renderer checks events every eight rows so Escape and focus changes do not
need to wait for a complete frame. Zoom is bounded to widths of 1e-12 through 16
in the complex plane. The normal image and SDK contain the application and the
native display, keyboard and clock helpers.

Quake, Doom and `mousetest` also keep updating while graphics is hidden or their
space is unselected. Quake and Doom game time advances without input focus;
their explicit game pause still works. The presenter copies only the chosen
surface of the selected space, but an unattended game continues using CPU and
memory bandwidth. Focus loss removes input eligibility, not execution or
rendering permission.

## Authority and ownership

Each space owns a display object. Boot grants init a named `display` capability
with DRAW rights; the shell forwards it to foreground children and session
successors when present. A shell without this optional resource remains usable.
The display is restricted to processes in its owning space. Acquiring graphics
requires this capability, not a space index or a global framebuffer address.

One process may acquire graphics in a space at a time. Another acquisition,
including a repeated call by the owner, returns BUSY. PRESENT, RELEASE and
REPLACE are permitted only to the acquiring process. Copying a capability delegates the
ability to acquire a future session; it does not transfer the current session
or map its pixels into the recipient. Closing a handle does not end the session.
The owner can use another DRAW handle to the same display to release it, or exit.

## Choosing the visible layer

The first successful PRESENT of each session shows graphics. Acquisition alone
leaves the terminal visible, without a layer marker. After that first PRESENT,
Super+Down shows the terminal and Super+Up restores graphics. The tab ends with
`+` for graphics or `−` for the terminal. With no presented session either
shortcut is consumed without changing state. Repeating the current choice does
not reset input or clear terminal bytes.

The choice lasts for the session: further PRESENT calls, REPLACE, physical
resize and switching away from and back to the space preserve it. A session
started in another space never selects that space; its first PRESENT records
graphics as the layer for the next visit. RELEASE or owner exit ends the
session and removes its marker. A later session gets its own first-PRESENT
behavior.

Hiding graphics keeps its mapping, ownership and independent input sessions.
The program remains runnable, and terminal output stays live. Captured keyboard
input and ordinary pointer input lose focus; terminal typing uses the normal
console queue.
Super+Up clears unread terminal bytes before restoring capture routing. A
foreground graphical job still occupies the shell, so hiding it opens no second
prompt. [Ctrl+C](../userland/shell.md#interrupting-foreground-commands) can reach
the shell's existing armed foreground interrupt while the terminal is shown.

## Mapping and presentation

[The ABI](../../include/abi/display.h) has five synchronous requests. All but
REPLACE carry only a message header:

- ACQUIRE allocates zeroed RAM and maps it writable and non-executable into the
  caller. Its reply contains the address, mapped size, width, height, pitch and
  red/green/blue shifts and geometry generation in a 64-byte `display_buffer`.
  Acquisition leaves the TTY selected until PRESENT.
- PRESENT records that the session has presented. Its first successful call
  selects graphics; later calls preserve the user's layer choice. It returns
  before scanout or a complete frame copy.
- RELEASE removes the user mapping, ends the owner's pointer subscription and
  selects the TTY again. It returns no reply payload; the old pixel pointer must
  no longer be used.
- SIZE returns the current destination width, height, pitch, channel shifts and
  generation atomically in a 48-byte `display_size_reply`. DRAW permits this
  query without acquiring graphics or owning the current session; it grants no
  mode-setting authority. `display_size()` exposes the same native result.
- REPLACE carries the expected destination generation. It allocates a zeroed
  current-size buffer at a disjoint user address and returns a new 64-byte
  descriptor. A generation mismatch returns BUSY; allocation/mapping failure
  preserves the old mapping and session. Success removes the old user mapping
  within the call, so its pointer is invalid after return. The acquiring process
  remains owner, and the layer choice is preserved. If graphics is shown, the
  blank new buffer is selected until it redraws. Reply storage must not overlap
  the mapping being retired.

Pixels are 32-bit words with three 8-bit channels at the returned shifts. Pitch
is bytes between row starts. The layout matches the current display; dimensions
exclude the kernel-owned navigation bar. An acquired mapping's address, extent,
pitch and dimensions remain fixed until REPLACE or RELEASE; its generation identifies the
geometry at acquisition. Mapped size includes page padding, which is zeroed
along with the pixels. Applications draw only
within width/height and use pitch rather than assuming tightly packed rows.
The mapping is distinct from private-memory allocations and cannot be released
through the memory service.

After PRESENT, when graphics is chosen in the active space, the kernel reads the
same backing pages that the application writes. Further changes may appear
without another request. Hidden or unselected graphics takes no new presenter
reference and adds no pixel copy; an already-snapshotted frame may finish after
a transition. This single-buffer contract permits tearing; PRESENT neither
freezes pixels nor promises vblank,
atomic frames or completion notification. The kernel copies pixels into the
physical driver's target: directly to the boot/Bochs framebuffer, or into kernel RAM
followed by a fenced VirtIO transfer and flush. Application backing is never
attached to the GPU. A failed physical driver makes ACQUIRE/PRESENT/SIZE/REPLACE unavailable;
RELEASE still tears down an existing session. Double buffering and a compositor
remain separate work.

The TTY keeps its own framebuffer and continues accepting output while graphics
is selected. Its cursor is not composited over graphics. Releasing graphics or
exiting restores the TTY on the next presentation. Graphics ownership is
independent of [keyboard capture](../devices/keyboard.md): Mandelbrot acquires both
sessions. A presented session's terminal layer overrides capture routing, without
releasing keyboard capture. Releasing graphics retains an independently owned
keyboard session and ends the pointer subscription and its cursor preference.

## Ordinary pointer input

The [pointer protocol](pointer.md#userspace-pointer-sessions) requires
both pointer INPUT authority and ownership of this space's graphics session.
Its 56-byte events report signed surface-local positions below navigation,
buttons, wheel counts, destination generation and mapping identity. Hit-testing
uses the shown mapping/destination intersection; exposed margins use the kernel
default cursor and receive no application input. A fresh press anchors motion
and releases to that surface until its accepted buttons are released. Wheel
input follows the currently hovered surface. Tab clicks select spaces.

The owner can supply a copied BGRA8 straight-alpha image, each dimension 1 through
64, with an in-image hotspot, select the default image, or hide and show it without
changing routing. Eligible shown graphics can warp within its intersection
using both current identities. Physical resize changes destination generation;
REPLACE changes mapping identity, including at the same size. Both discard old
ordinary spatial input, cancel drags and notify the client to re-query geometry.
The same locked session instead retains relative input and accepted buttons.

The owned subscription can request LOCK for relative input. Only one global
pointer lock exists; it requires shown, focused graphics and permits refusal.
Locked INPUT has `POINTER_EVENT_LOCKED` and device-count `dx`/`dy`, while the
ordinary position stays parked. Lock forces cursor hiding without altering the
surface's saved preference. UNLOCK restores ordinary routing; STATE reports
current focus/lock flags. Acquisition and PRESENT do not themselves lock input.

Super+Esc with either Super key and any additional modifiers revokes lock and
consumes Escape through its release before keyboard capture. Space/layer loss,
device loss and teardown also revoke it. Such revocation leaves a per-space
fresh-click requirement that survives new sessions and processes. A consumed
fresh left press on shown graphics reports `POINTER_ACTIVATED`; tab clicks,
warp and polling cannot grant relock permission. See the
[lock contract](pointer.md#relative-lock-and-user-escape).

Pointer and display backing have separate presenter leases. A snapshotted image
can finish after image replacement, display release or owner exit without
accessing retired owner state. The kernel composes it after the chosen surface
and includes it in [screen capture](screen-capture.md); application pixels remain
cursor-free. Quake acquires pointer input after graphics and requests lock after
its first PRESENT. It reads relative counts only while locked, observes STATE
to detect revocation and requests relock on a fresh surface activation. Locked
same-session geometry changes retain held controls. Cleanup releases pointer
input before graphics; keyboard-only play remains available without a mouse.
The [SDL backend](../development/sdl2.md#the-backend) exposes the same native
position, cursor, warp and lock rules. Runtime evidence and source-inspected
limits are recorded in [system pointer qualification](../development/system-pointer-qualification.md).

## Live destination geometry

VirtIO follows enabled size changes on its selected output. Boot and Bochs
geometry stays fixed. At a frame boundary the BSP prepares replacement screen,
navigation, cursor and TTY buffers for every local space, including inactive
ones. It verifies the space registry before switching scanout and again after
the device wait; a newly created space causes rollback and fresh preparation.
Refused dimensions or failed allocation leave the last valid geometry usable.

After confirmed scanout success, the output lock protects copying the latest
TTY raster and committing every local geometry and generation together. TTY
objects retain their identity. Whole cells survive without reflow: shrinking
height drops enough top rows to keep the cursor visible; growing keeps existing
positions. Only whole columns that fit are copied, and new cells and margins
use the TTY background. The cursor is translated/clamped and pending wrap is
cleared; colors, parser state and tab width survive. No allocation or device wait
occurs under the output lock.

SIZE observes the new destination, while an existing application mapping keeps
its original layout. Presentation copies the top-left intersection using each
source row's pitch and fills exposed destination margins with the TTY background.
Generation starts at one and advances on each committed local resize. Physical
resizing preserves the chosen layer and does not change space focus or
keyboard capture eligibility. Pointer position is clamped to the new physical
screen and spatial input receives a geometry notification.

Old TTY pixel mappings remain intact until writers and the presenter have
relinquished them and the [shared-range retirement protocol](../kernel/smp.md#memory-and-output-boundaries)
has flushed every online CPU. A missing acknowledgement retains one old batch
until reboot and disables further resizing; the committed display continues
working. Device-owned backing separately requires confirmed fenced cleanup;
uncertain GPU ownership causes terminal driver failure and retention until reboot.

`WAIT_RESIZED` on a DRAW handle compares a caller's observed generation with
the current destination generation. It needs no acquired session and reserves
nothing. Changes coalesce; query SIZE after waking. A backend failure reports
`WAIT_ERROR`. Geometry notification cannot be lost between query and sleeping:
the readiness worker remembers notifications across its scan and park.

Mandelbrot waits for resize together with captured keyboard readability and
replaces at a render checkpoint, retaining its complex-plane centre and zoom.
Doom queries at frame boundaries and keeps its 320x200 game frame, recomputing
integer scale and letterboxing after replacement. Below scale one it keeps
rendering to the old mapping with clipping until the destination grows. Both
keep the old mapping usable after replacement failure and wait for a fresh
geometry generation before automatically retrying. Other graphics consumers
keep their acquired layout until they explicitly adapt.

## Mapping and teardown invariants

All display state and backing allocation are BSP-owned, with interrupts disabled
while mutating them. All five operations, including PRESENT and SIZE, use typed
requests on the common BSP FIFO. The service catalog requires the scheduler to park the
requesting task outside its private root and stack, with entry/current-task state
cleared, before publication. The BSP executor performs the operation and clears
its process/display loans before completion and wakeup; resumption reloads CR3.
The display capability keeps the object alive during the request. Failed
acquisition unwinds partial mappings and backing without claiming the display.

VM owns the kernel allocation; user mappings borrow its physical frames. The
session holds one buffer reference. Presentation acquires another with interrupts
disabled, then copies with interrupts enabled. If release or process cleanup runs
while that copy is preempted, it detaches the old buffer and removes its user aliases
but retains backing until presentation drops the last reference. A presenter
borrows no process pointer. It may finish one old frame before the next tick
shows the restored TTY or newly selected graphics. REPLACE applies the same
retirement rule while retaining the session. Deferred backing reclamation is
attributed to the owner's execution group, so group completion includes it.

Normal exit and fatal user-fault cleanup both release an owned session before
VM destruction, including if the process closed every display handle. Child
completion is published after process cleanup; a final preempted presentation
may temporarily retain only pixel backing. No application mapping survives exit.

## Current boundaries

One buffer, one owner and one user mapping per space; fixed acquired layouts and
native 32-bit pixels. Cross-space presentation, shared application mappings,
dirty rectangles, frame completion and graphics-specific
resource quotas remain separate work. The single-CPU development fallback can use the same
display protocol, while its TTY still shares kernel logs.
