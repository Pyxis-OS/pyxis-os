# Desktop and graphics direction

Working ideas, not an implementation assignment or settled window protocol.

The initial desktop could combine Windows 9x-inspired controls and window
decoration with a more modern treatment, a global application menu bar, a dock
and desktop widgets. GrafX2 is a future port candidate: creating graphical
assets inside Pyxis would complement the existing edit/build/run workflow.

A desktop/compositor belongs within a space. Multiple desktop instances can
coexist; the persistent space-navigation interface remains independent of them.
The desktop's application menu bar is distinct from global space navigation.

The [implemented userspace-service contract](../interfaces/userspace-services.md) defines discovery,
exported-object authority/lifetime and call/send/receive transport for future
services, including this one. It does not define window operations, shared pixel
memory, frame ownership or compositor scheduling. Those remain graphics work.

## First useful graphics slice

Two independent client windows can overlap, move, receive input and disappear
cleanly when their processes exit. This exercises the contracts before a full
desktop is built around them:

- Mouse input, focus, pointer routing and keeping a drag with its original client.
- Shareable pixel buffers with explicit mapping rights, ownership and lifetime.
- Frame submission and buffer-reuse rules that prevent concurrent producer and
  compositor access from silently corrupting a frame.
- Window creation, presentation, resize, close and focus messages.
- Client exit/failure cleanup for windows, buffers and outstanding interactions.

The global menu will eventually need application-supplied menu descriptions and
action delivery. Its shape and the wider UI toolkit remain undecided. Do not add
placeholder APIs, generic rendering layers or kernel desktop policy now.

## Rendering and ports

Software rendering is a candidate for the first implementation; measure it
before requiring acceleration. Start with opaque windows, bitmap text and
changed-region redraw. The agreed first VirtIO GPU direction is presentation of
our software framebuffer, as described below; accelerated drawing remains separate.

[GrafX2](https://gitlab.com/GrafX2/grafX2) would exercise pointer input, image
editing and file access. Its [compilation instructions](https://sources.debian.org/src/grafx2/2.9%2Bds-2/doc/COMPILING.txt)
describe SDL/SDL_image and image libraries, with optional font/Lua support.
SDL2 is the intended reusable library target: investigate a native Pyxis backend
with software rendering, presentation, keyboard/mouse input and timing first.
Choose the supported subsystems through a pinned GrafX2/dependency probe; audio,
GPU support and a full desktop are not prerequisites for that initial scope.
Keep upstream SDL2 with a platform backend rather than implementing a substitute
subset of its API. The [port candidates](application-ports.md) also connect this
work to DevilutionX and the future C AbyssEngine investigation. No port schedule
or complete SDL subsystem contract is selected yet.

## VirtIO GPU presentation and display resizing

Agreed future milestone scope, not an implementation task. Finish the writable
filesystem core, then runtime SMP and independent spaces; this adds no prerequisite
to either milestone or to the first terminal multiplexer.

Software framebuffer presentation and dynamic display resizing belong in the
same milestone, along with propagating changed terminal dimensions. The boundary
is 2D presentation without 3D acceleration, not fixed-resolution output.

Start with a basic VirtIO GPU 2D driver that presents a software-rendered framebuffer:
create a display resource, attach guest backing memory, transfer rendered pixels
and select/update the scanout. No 3D acceleration, shader stack or desktop server
is required for this first step. Keep the Limine framebuffer for boot-time output;
define the ownership handoff before implementing driver takeover.

Include live display resizing in that milestone. QEMU's GTK frontend can pass window/fullscreen
dimensions to VirtIO GPU, which raises `VIRTIO_GPU_EVENT_DISPLAY`. The guest can
query `GET_DISPLAY_INFO`, prepare appropriately sized backing/resources and update
the scanout. This direct route does not require a SPICE guest agent. Host scaling
alone only enlarges existing pixels; it does not give Pyxis more terminal columns.
See the [QEMU GPU implementation](https://github.com/qemu/qemu/blob/master/hw/display/virtio-gpu-base.c)
and [2D backend documentation](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html).

Keep display pixel dimensions separate from terminal rows/columns and pane sizes.
A display resize should eventually update space presentation, terminal geometry
and multiplexer layout, with resize notifications to affected applications.
Splitting a pane also changes terminal geometry without changing the display mode,
so terminal resizing is useful independently of the GPU driver. Define buffer
replacement and mapping lifetimes, resize failure behavior and terminal-content
preservation when those implementation tasks are selected; do not silently
invalidate application mappings or assume every resize allocation succeeds.
