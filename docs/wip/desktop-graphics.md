# Desktop and graphics direction

Working ideas, not an implementation assignment or settled window protocol.

The initial desktop could combine Windows 9x-inspired controls and window
decoration with a more modern treatment, a global application menu bar, a dock
and desktop widgets. GrafX2 is a future port candidate: creating graphical
assets inside Pyxis would complement the existing edit/build/run workflow.

A desktop/compositor belongs within a space. Multiple desktop instances can
coexist; the persistent space-navigation interface remains independent of them.
The desktop's application menu bar is distinct from global space navigation.

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
changed-region redraw. Virtio-gpu is a later graphics path to investigate;
device resource/presentation support and accelerated drawing are separate
decisions, not an assumed single feature.

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
