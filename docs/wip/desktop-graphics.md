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

## Where to start on a compositor

The owner intends to write the compositor personally; this is a suggested
learning path, not an assignment. Drawing widgets is the easy part. A compositor
has three jobs: combining each window's buffer onto the screen in stacking order,
routing keyboard focus and pointer input, and a protocol with client programs.
Widgets and decorations are a separate fourth job.

1. **One process, no protocol.** A program that acquires the display, as
   Mandelbrot does, keeps a list of windows with their own pixel buffers in its
   own memory, draws them back to front with a pointer, and lets the user click
   to raise and drag to move. This teaches stacking, hit-testing and double
   buffering with today's display, pointer and keyboard grants.
2. **Damage.** Recomposite only the rectangles that changed.
3. **Separate client processes.** Move window contents into their own programs.
   Wayland's core protocol is a clear model: a surface, an attached buffer,
   damage and commit, answered by configure (size) and frame-done events.
   [The Wayland Book](https://wayland-book.com/) explains it, and wlroots'
   [tinywl](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/master/tinywl)
   is a small complete compositor. Pyxis endpoints can carry the messages, but
   **Pyxis has no shared memory between processes**: window buffers need a
   kernel facility for it first, a bounded task an agent can take.

A compositor runs as the graphical program on a space's `+`
[layer](../userland/space-layers.md).

## Rendering and ports

Software rendering is a candidate for the first implementation; measure it
before requiring acceleration. Start with opaque windows, bitmap text and
changed-region redraw. The first VirtIO GPU work presents the software-rendered
screen ([display drivers](../kernel/display.md)); accelerated drawing remains separate.

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

## Display drivers and resizing

VirtIO GPU 2D presentation and live display resizing are implemented in
[display drivers](../kernel/display.md), together with Bochs and a small
driver interface. Accelerated rendering remains separate.

The next physical-driver investigation can use an Intel integrated GPU passed
through from horse, or native ThinkPad PXE bring-up with
[remote debugging](../development/remote-debugging.md). The owner prepares
hardware and permits vendor firmware blobs (agreed 2026-10-07). ThinkPad GPU
VFIO is ruled out because its IOMMU group also contains the PSP, both USB
controllers and audio devices. This direction does not authorize a driver task.
