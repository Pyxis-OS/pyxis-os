# Screen capture

The native screen-capture object observes the whole currently shown local screen.
It is independent of the per-space [DRAW capability](graphics.md): CAPTURE permits
no drawing, graphics acquisition, input access, mode setting or device access.
The result includes navigation, the selected space's shown layer, clipping and
background margins, and the visible software cursor. Hidden surfaces are omitted.
Layer selection remains the presenter's ordinary `display_snapshot()` selection.

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
[LAN visibility](../technical-debt.md#kernel-log-retention-and-lan-visibility).
The packaged [screenshot command](../userland/screenshot.md) saves a PNG and
uses the existing explicit file-download workflow. See the
[screenshots milestone](../wip/screenshots.md) for remaining qualification.

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

The presenter allocates compact backing only for an admitted request. For each
visible source span it first copies into that backing, then sends those same
staged bytes to the display driver. Copies split at physical row boundaries so
device offsets cannot index compact storage. Success requires the driver's
normal frame submission to succeed; incomplete or unpresented backing is never
published. See [display ownership](../kernel/display.md#screen-capture).

Capture backing is initially uninitialized. The current full repaint fills every
visible pixel before publication. Future damage tracking must force a full
composition for a pending capture; copying damaged regions alone would expose
unwritten or stale bytes from the snapshot allocation.

The typed BSP request uses DEFERRED publication after the scheduler parks the
caller, leaves its stack, activates the kernel root and clears entry/current-task
state. This lends the caller's capability table exclusively through completion,
including for BSP userspace. The FIFO executor forwards the request to the sole
presenter and continues servicing other work. The presenter owns pending/active
state; admission, allocation, FILE installation, cleanup and completion run on
BSP with IF=0, outside the output lock. Pixel copying and device waits retain the
existing IF=1 frame lease.

Stop is checked before admission, before backing allocation and before FILE
publication. Refusal, stop and backend failure release unpublished resources.
Completion clears presenter references and the table loan before waking the
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

This freezes one successful presenter composition. Concurrent application or TTY
writes can already tear that composition; it does not promise an atomic
application frame, vblank or exact physical scanout timing. It cannot capture a
panic or make progress with a stuck presenter/scheduler. Completed snapshots have
no separate retained-image quota, so callers can retain multiple files and exhaust
memory despite the single in-flight slot. Backing costs `4 * width * height`
bytes, in addition to existing display buffers. See
[capture limits](../technical-debt.md#screen-capture-memory-and-consistency-limits).

Interactive QEMU/debugger inspection on 2026-10-08 used q35, the boot framebuffer
at 1280x800 with physical pitch 5120, four CPUs, 2 GiB and nested KVM. A real CALL
returned 64 bytes and a native READ-only FILE of 4,096,000 bytes, RGB shifts
16/8/0 and generation one. FILE READ bytes and EOF, RESIZE refusal, a zero-right
capture refusal, and a copied FILE surviving the original handle's CLOSE were
observed. Full snapshot dumps remained identical as boot progressed; the final
CLOSE reached the FILE destructor.

A second q35 boot selected VirtIO GPU with the same geometry. Capture succeeded;
with one real caller pending, another returned BUSY with zero reply bytes and its
table loan cleared. The admitted caller subsequently received its FILE, leaving
pending/active state and private backing clear. The child received its diagnostic
CAPTURE grant through an ordinary kernel grant at its unsubmitted-process
checkpoint; these task-4 checks did not establish the subsequent boot policy.

Allocation/backend failure and stop cleanup are source-reviewed, without injected
failures. Bochs, native hardware, PNG encoding and host download remain unqualified.
The separately versioned helper is included in the image's SDK and is consumed
by the [PNG command](../userland/screenshot.md).

Task-5 manual inspection used the ordinary image built with the current LLVM
23.1.3 / 49e2c1a builder, q35 standard VGA, 1280x800 boot framebuffer, four CPUs,
2 GiB and nested KVM. Development's final shell retained CAPTURE through its
init/session chain; Read-only's shell had DRAW and no capture resource. A
connected Remote shell had CAPTURE without DRAW. Its foreground `echo` child
held CAPTURE alone with transport zero; a real debugger-substituted CALL returned
the successful 64-byte reply and a 4,096,000-byte FILE, which was closed before
replaying the original syscall. Both `echo pipe | cat` stages and
`echo background &` inherited CAPTURE. Doom launched and visibly rendered in
Read-only with DRAW and no CAPTURE. Installed/rescue configuration, strict
boolean parsing and missing-seed refusal are source-reviewed, without injected
errors or an installed boot in this task. The PNG consumer and native capture
qualification remain pending.
