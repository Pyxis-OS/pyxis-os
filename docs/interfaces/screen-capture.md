# Screen capture

The native screen-capture object observes the whole currently shown local screen.
It is independent of the per-space [DRAW capability](graphics.md): CAPTURE permits
no drawing, graphics acquisition, input access, mode setting or device access.
The result includes navigation, the selected space's shown layer, clipping and
background margins, visible terminal selection highlighting, the visible
system pointer and the TTY block caret
when the terminal is shown. Hidden surfaces are omitted. A hidden system pointer
is omitted without changing ordinary input routing; relative lock also excludes
the system pointer while retaining its saved visibility preference. TTY caret
visibility remains independent.
Layer selection remains the presenter's ordinary `display_snapshot()` selection.
While the screen is [idle-blanked](../kernel/display.md#idle-blanking) the capture
still holds the composed frame, not black.

Boot init receives the named `screen_capture` resource with CAPTURE authority.
It delegates it only to spaces with the optional
[`screenshot = true` boot setting](../userland/init.md#boot-configuration).
Live Development and Remote and installed `pyxis` opt in; other packaged spaces
and the built-in rescue space do not. Init, session, remote-daemon and shell
handoffs preserve the optional grant through explicit resource lists. Ordinary
commands launched by a granted shell inherit it, including background commands
and every pipeline stage. Remote access, DRAW and path-write rights imply no
capture authority. Every program holding this grant can observe whatever any
space currently shows, independently of that space's grants. The packaged Remote
grant also exposes the whole shown local screen to anyone reaching the
unauthenticated terminal on the development LAN. This authority breadth is
accepted for bring-up; its revisit point is recorded under
[LAN visibility](https://git.internal/PyxisOS/pyxis-os/issues/1358).
The packaged [screenshot command](../userland/screenshot.md) saves a PNG and
uses the existing explicit file-download workflow. See the
[qualification report](../development/screenshot-qualification.md) for measured
QEMU coverage and cost, and its
[native ThinkPad check](../development/screenshot-qualification.md#native-thinkpad-check).

## Print Screen delivery

The system consumes local Print Screen press/repeat/release events before
application input routing, including keyboard capture and relative pointer lock.
The power overlay and space picker consume the key without starting a save.
The existing per-space `screenshot` switch selects `SPACE_CREATE_SCREENSHOT_SAVER`
in trusted space creation. It installs one private `screenshot_saver` capability
in the first process startup. The trusted script-shell SESSION handoff carries it
once; the session consumes it and starts the saver with CAPTURE, home-write,
clock and memory grants, without a launcher or console streams. Ordinary children
never receive this private channel. It grants neither CAPTURE nor filesystem
access. The [screenshot guide](../userland/screenshot.md#print-screen) covers use.

[`screenshot_saver.h`](../../include/abi/screenshot_saver.h) defines the private
`PROTOCOL_SCREENSHOT_SAVER` operations, all requiring SERVE and the caller's own
space:

- `NEXT` returns one request identity and the fixed space name, or WOULD_BLOCK.
  `WAIT_MANY` READABLE observes an undelivered request without consuming it.
  Availability starts only after NEXT or readiness participation; a
  startup process merely holding SERVE does not establish a participating saver.
  A failed NEXT copyout cancels the delivered request.
- `CAPTURE` takes that identity and a separately held CAPTURE handle. It resolves
  that authority, preserving forwarder lineage, and uses the ordinary immutable
  FILE result and presenter slot. At the frame boundary, a changed shown-space
  generation returns DENIED, even after switching away and back. The recheck and
  actual `active_space`/surface selection share one interrupts-off section. A
  modal overlay returns DENIED for saver requests; ordinary CAPTURE still
  observes the overlay. An image selected before switching finishes in the
  initiating space. One attempt is admitted per request; no automatic retry or
  input/focus change occurs.
- `VALIDATE` checks the captured request immediately before each publication
  attempt; stale identities return DENIED.
- `COMPLETE` takes the matching identity and a status, plus a printable basename
  only on success. It clears that request. Kernel-formatted bar feedback expires
  after two seconds. A rejected busy/unavailable press does not invalidate the
  in-flight completion. A newer admitted request does. Missing wall time has a
  distinct no-clock failure reason.

One keyboard save is admitted globally through PNG publication. A second press
in any opted-in space while it is outstanding reports busy rather than queuing.
Missing permissions, missing startup resources or a dead saver report
unavailable, without using another space's grants. A 30-second policy budget from
key admission bounds queued and delivered requests. BSP upkeep reports expiry or
final SERVE close as failure, disarms the saver and releases global admission.
NEXT or a new readiness wait re-arms it; late completion is refused. A blocked
filesystem operation already admitted elsewhere can still finish: after a
successful capture, timeout/death feedback says to check screenshots and makes
no cancellation claim. Before capture it reports only the failure, because no
saver storage work could have started.

SERVE grants retain logical registration; zero-right copies, operation and
readiness references retain only storage. Closing the final SERVE grant ends
availability immediately, and final storage retirement clears the weak space
registration on the BSP. All request state changes and presenter checks are
BSP-owned; grant counts and readiness notifications are safe on any CPU. The
saver keeps the admitted object alive through blocking capture and output
copying. No file or PNG work occurs in the input handler or navigation
drawing.

Print Screen merged in #954 and is qualified in QEMU only (nested KVM/Q35, one
and four CPUs): a 1280×800 PNG decoded on the host, same-second suffixes,
unavailable Read-only spaces, kept keyboard ownership and relative lock, a
preserved non-directory `home://screenshots`, completion in the initiating space
after a switch, busy rejection and the overlay refusal. Pre-selection cancellation,
saver stalls or death, failed NEXT copyout, a missing clock, storage failures and
collision exhaustion are source-reviewed only. The
[native check](https://git.internal/PyxisOS/pyxis-os/issues/1353) is open;
raw evidence is in `/shared/present/print-screen/`.

## Request and owned result

[screen_capture.h](../../include/abi/screen_capture.h) defines
`SCREEN_CAPTURE_RIGHT_CAPTURE` and the header-only `SCREEN_CAPTURE_FRAME`
operation on `PROTOCOL_SCREEN_CAPTURE`. A successful CALL returns the 64-byte
`screen_capture_reply` and installs an owned native FILE handle atomically with
that result. Its fields describe:

- `file`: a FILE with READ rights and no transport rights;
- `width`, `height`, `pitch`, `size`: native 32-bit pixels in tightly packed rows,
  with `pitch = 4 * width` and `size = pitch * height`;
- `red_shift`, `green_shift`, `blue_shift`: channel locations within each pixel;
- `generation`: the geometry generation selected at the captured frame boundary.

Device row padding and allocation padding are excluded. Read the bytes through
ordinary offset-based FILE READ and SIZE operations. WRITE and RESIZE are denied.
The bytes remain immutable across later drawing, layer/space switches, resizing
and display failure. CLOSE releases the handle; COPY retains the same snapshot,
and the last reference releases its pixel backing. Normal process cleanup closes
remaining handles. No user mapping of the backing is created.

The userland `screen_capture_frame()` helper borrows the capture handle and
returns the owned result, clearing its output on failure. Callers must close a
successful FILE when finished.

## Frame boundary and lifetime

One global pending or in-flight capture is admitted. Another admission returns
BUSY rather than queuing. The request captures the next presenter composition;
arrival during a composition waits for the following frame. Geometry is selected
at that frame boundary, after any resize transaction.

After successful frame begin, the presenter retains one pointer image, hotspot,
position and visibility snapshot alongside the chosen surface. Boot and Bochs
blend that image into intersecting spans after navigation, surface and TTY caret
composition, then pass final pixels into the capture tee. VirtIO tees underlying
spans once into capture and cursor-free scanout. It then reads the already-teed
background bytes, blends the same pointer snapshot and writes only to capture
storage. Without an active capture it performs no software pointer blend.
Image replacement, hiding, warp, lock changes or owner exit during composition
affects a later frame. The lease remains through normal display, hardware cursor
and capture completion. See
[software pointer presentation](../kernel/display.md#software-pointer) and
[system pointer qualification](../development/system-pointer-qualification.md).

The presenter allocates compact backing only for an admitted request. For each
visible source span it first copies into that backing, then sends those same
staged bytes to the display driver. Copies split at physical row boundaries so
device offsets cannot index compact storage. Success requires the driver's
normal frame submission to succeed; VirtIO also requires cursor state matching
the leased snapshot. Changed state requires successful fenced image preparation
when needed and the matching bounded cursor used completion; unchanged state
reuses its last confirmed completion, or drains a still-posted ordinary command
for that state. Ordinary frames can release the image lease after copying and
posting because command and uploaded image buffers belong to the driver.
Incomplete or failed backing is never
published. The public reply and FILE lifetime remain the same for all backends.
See [display ownership](../kernel/display.md#screen-capture).

Capture backing is initially uninitialized. The current full repaint fills every
visible pixel before publication. Future damage tracking must force a full
composition for a pending capture; copying damaged regions alone would expose
unwritten or stale bytes from the snapshot allocation.

The typed BSP request uses DEFERRED publication after the scheduler parks the
caller, leaves its stack, activates the kernel root and clears entry/current-task
state. The caller reserves its result slot before submission; the shared request
owns that claim through completion, including for BSP userspace. The FIFO executor forwards the request to the sole
presenter and continues servicing other work. The presenter owns pending/active
state; admission, allocation, FILE installation, cleanup and completion run on
BSP with IF=0, outside the output lock. Pixel copying and device waits retain the
existing IF=1 frame lease.

Stop is checked before admission, before backing allocation and before FILE
publication. Refusal, stop and backend failure release unpublished resources.
Completion clears presenter references and any unconsumed slot claim before waking the
caller, and never accesses request storage afterward. The caller consumes the
result and releases its request reservation before another operation can reuse
it. See [BSP requests](../kernel/bsp-service-requests.md#publication-and-completion).

Missing CAPTURE returns DENIED. Invalid operation, request extent or reply buffer
uses the ordinary BAD_OPERATION, BAD_REQUEST or BAD_BUFFER status. Unsupported
or overflowing geometry and exhausted handle capacity return LIMIT; allocation
refusal returns NO_MEMORY; an unavailable or failed backend returns UNAVAILABLE.
A pending caller stop returns ENDPOINT_CLOSED. Failures return no FILE or reply
payload, and capture does not retry them automatically.

## Limits and qualification

This freezes one successful presenter composition. Application graphics appear
as whole submitted frames; concurrent TTY writes can still tear the composition.
It does not promise vblank or exact physical scanout timing. It cannot capture a
panic or make progress with a stuck presenter/scheduler. Completed snapshots have
no separate retained-image quota, so callers can retain multiple files and exhaust
memory despite the single in-flight slot. Backing costs `4 * width * height`
bytes, in addition to existing display buffers. See
[capture limits](https://git.internal/PyxisOS/pyxis-os/issues/1354).

QEMU 10.2.2's zero-length cursor used completion confirms buffer consumption,
without independently acknowledging cursor application. Capture includes the
guest's leased pointer image even when a host frontend cannot place or render it
correctly; [frontend qualification](../development/qemu.md#hardware-pointer-frontend)
is separate from capture contents. The
[task 4 hardware qualification](../development/system-pointer-qualification.md#task-4-hardware-qualification)
records cursor-inclusive capture checks, including fractional alpha, against
cursor-free scanout and one RGBA cursor resource.

Interactive QEMU/debugger inspection established READ-only FILE layout and EOF,
DENIED and BUSY refusal, copied-handle lifetime and final FILE destruction.
Ordinary init/session/shell handoffs were checked in Development and Remote,
including pipeline and background children; Read-only retained DRAW without
CAPTURE. Installed/rescue policy and strict boot-setting parsing were
source-reviewed, without an installed boot or injected errors.

The [qualification report](../development/screenshot-qualification.md) records
PNG/monitor comparisons across boot, Bochs and VirtIO displays, shown-layer and
resize coverage, snapshot retention, and matched presenter/encoder measurements.
Allocation/backend failure and stop cleanup remain source-reviewed without
injected failures. The owner accepted milestone closure before the
[native ThinkPad check](../development/screenshot-qualification.md#native-thinkpad-check),
which passed later that day.
