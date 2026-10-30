# SDL3

Pyxis carries upstream SDL 3.2.28 with a native backend adapted from the
[SDL2 port](sdl2.md), as a development library for new graphical ports. This
first part covers the core, video, OpenGL through OSMesa, keyboard, pointer,
timers and threads. Audio follows separately; clipboard and joysticks come
later. Building SDL2 programs on SDL3 through sdl2-compat is a possible later
milestone, not part of this port.

The [recipe README](../../ports/sdl3/README.md) documents the backend file by
file; this page is the overview.

## Using the library

`sdl3-dev` provides `lib/libSDL3.a`, upstream's public headers in
`include/SDL3/`, `lib/pkgconfig/sdl3.pc` and the relocatable CMake package
`lib/cmake/SDL3/SDL3Config.cmake` with `SDL3::SDL3-static` and `SDL3::SDL3`.
The library's build configuration is internal, so consumers compile against
upstream's headers unchanged.

`sdl3-osmesa-dev` provides `libSDL3-osmesa.a`, selected **instead of**
`libSDL3.a` for desktop OpenGL, with `sdl3-osmesa.pc` and
`find_package(SDL3OSMesa)` / `SDL3::SDL3-osmesa-static`. It depends by name
on `sdl3-dev`, `mesa-dev` and `libcxx-dev`. Ordinary SDL3 links stay free of
Mesa and C++.

A program needs the `display`, `keyboard` and `clock` grants; `pointer` is
optional. Threads and callback timers need a [bundle](../userland/program-bundles.md)
declaring `threads/manage`, and a threaded program that blocks in
`SDL_WaitEvent` also needs `pipe/create`.

## Behavior

The window, input and OpenGL models are the SDL2 port's:

- One window covers the output. A software window takes the output size and
  receives `SDL_EVENT_WINDOW_RESIZED` when it changes. An OpenGL window keeps
  its own canvas size, scaled into the largest centred aspect-preserving
  viewport; `SDL_SetWindowSize` changes the canvas. Fullscreen requests
  succeed without changing anything.
- Native pointer positions, focus and relative lock are authoritative. A
  refused lock fails `SDL_SetWindowRelativeMouseMode` instead of falling back
  to warping.
- Condition timeouts use the monotonic clock, and SDL's environment is read
  from the native environment table.
- `SDL_GetPrefPath` creates `home://APP/`; `SDL_GetBasePath` is unsupported.

## Validation

QEMU 10.2.2 under KVM, 4 CPUs, 4 GiB, standard VGA at 1280×800, OVMF;
pyxis-os `2ad79937` with ports `8d6ba57` plus this port. Programs ran from a
virtio-fs export in the Development space, launched from the shell without
a bundle.

- A software-renderer program (`SDL_CreateWindowAndRenderer`) took the
  1280×768 content area, received keyboard focus, key, text, motion, button and
  wheel events with correct values and positions, and exited cleanly on
  Escape.
- An OpenGL 1.1 program on the OSMesa archive got Mesa 26.2.5 softpipe with a
  compatibility profile 3.3 context. A 1024×768 canvas drew 158–216 frames
  per second, and 262–308 after `SDL_SetWindowSize(640, 480)`; the pointer
  mapped to canvas coordinates in both.
- In a bundle granting `threads/manage` and `pipe/create`, a 50 ms condition
  timeout took 50.04 ms; a thread pushing an event after 300 ms ended a
  blocked `SDL_WaitEventTimeout(5000)` after 300.3 ms; a 100 ms
  `SDL_AddTimer` event arrived after 100.5 ms; an idle 250 ms wait took
  250.1 ms.
- Without `threads/manage`, `SDL_CreateThread` failed with "Operation not
  permitted", and condition waits returned at once as spurious wakeups,
  because libc cannot park a thread without the threads service.
  `SDL_Delay(20)` still slept 20 ms.

These are nested-VM numbers. A native run has not been made.

## Limits

- No audio, clipboard, joysticks, haptics, sensors, camera, GPU API or
  Vulkan yet.
- OpenGL supports only swap interval 0.
- Thread priorities other than normal are refused.
- Named system cursors and message boxes are unsupported.
