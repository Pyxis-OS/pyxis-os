# SDL2

Pyxis carries upstream SDL 2.32.10 with a native backend, as a development
library for graphical ports. Its first consumer is
[DevilutionX](../userland/devilutionx.md), an opt-in build. Its
[native ThinkPad qualification](https://git.internal/PyxisOS/pyxis-os/issues/1399)
is recorded separately from the QEMU measurements below.

The [recipe README](../../ports/sdl2/README.md) documents the backend file by
file; this page is the overview.

## Using the library

`make image` stages the library in `build/ports-dev/sdl2`, outside the base and
guest SDK. That directory holds:

- `lib/libSDL2.a`;
- `include/SDL2/`, with the port's `SDL_config.h` in place of upstream's
  platform dispatcher, so the library and its consumers see one configuration;
- `lib/cmake/SDL2/SDL2Config.cmake`, a relocatable package with upstream's
  static target `SDL2::SDL2-static`.

A Make consumer compiles with `-I.../include/SDL2` and links `libSDL2.a`
before the SDK libraries. A CMake consumer uses the SDK's
[toolchain file](sdk.md#cmake), adds the prefix to `CMAKE_FIND_ROOT_PATH` and
calls `find_package(SDL2)`.

A program needs the `display`, `keyboard` and `clock` grants. `pointer` is
optional: without it, or without a mouse, the program runs from the keyboard.
Threads and callback timers additionally need a [bundle](../userland/program-bundles.md)
declaring `threads/manage`, and `pipe/create` for blocking event waits; see
[threads and timers](#threads-and-timers). Audio output needs the `audio` grant
and `threads/manage` for SDL's callback thread; see [audio](#audio).

## OpenGL through OSMesa

Install `sdl2-osmesa-dev` for the opt-in `libSDL2-osmesa.a`, selected **instead
of** `libSDL2.a`. It depends by name on `sdl2-dev` (shared headers and covered
source), `mesa-dev` and `libcxx-dev`. Headers/libraries join `sdk://`; no package
adds runtime authority. Ordinary SDL/raylib links remain Mesa-free and use the
C SDK closure. GL also needs declared `threads/manage` for synchronisation
and [Mesa](mesa.md).

Make consumers use `pkg-config --static --cflags --libs sdl2-osmesa`. CMake
consumers use `find_package(SDL2OSMesa CONFIG REQUIRED)` and the static target
`SDL2::SDL2-osmesa-static`, carrying Mesa's real C++ closure. Building the SDL
recipe requires the Mesa development prefix; `make image` builds both profiles
but puts no GL application in the archive. Host dependencies include Mesa's
Meson/Ninja, Mako/PyYAML/packaging, flex and bison. The compiler container supplies
Mesa's matching target LLVM libraries; ordinary SDL/image builds do not build LLVM.

SDL's ordinary double-buffer default creates independent GL front/back
attachments. `glDrawBuffer(GL_BACK)` works; `SDL_GL_SwapWindow` finishes rendering,
exchanges them, reads the new front into a retained top-down BGRA canvas, and
copies/scales it into the held native display slot. Back contents after swap
are undefined. Front-only requests also work. Attributes report the current GL
context and actual buffers. Static library loading accepts only NULL/empty paths;
procedure lookup uses `OSMesaGetProcAddress`. Multisample, stereo, ES, unsupported
context flags/precision and nonzero swap intervals fail explicitly. There is no
vblank feedback or GPU. Mesa selects llvmpipe when optional JIT authority is
available and softpipe otherwise; neither profile is formally GL-conformance
qualified.

One native window covers the space. GL window dimensions are the app-selected
logical canvas, including fullscreen requests; output resize preserves that
canvas and aspect-fits the next frame into new slots. Absolute pointer/warp
coordinates follow the fit; native relative motion stays unscaled. Explicit
`SDL_SetWindowSize` changes the canvas. Detach a context from its rendering thread
and synchronise before rebinding/resizing it elsewhere. Context storage survives
window destruction until explicit deletion; native mapping replacement and swap
are serialised. Focus loss releases held input while rendering continues.

The asset-free [gears example](../../ports/sdl2/manual/gears/README.md) uses the
ordinary double-buffer default and draws its renderer name. Animation, input,
focus, logical/output resize and thread handoff are QEMU-qualified in the
[task 3 record](experiments/sdl2-osmesa.md). The later
[OpenArena record](experiments/ioquake3-softpipe.md#vela-native-hwp-comparison-2026-10-25)
covers a Vela native timedemo; it does not qualify every context/input feature
exercised by the QEMU gears example.

## The backend

- **Software video.** One window, always the size of the display content area
  and marked fullscreen; a second window is refused. The GL profile above uses
  a logical canvas over the same native output.
  - **Sessions.** Creating the window acquires the keyboard. Creating its
    framebuffer acquires graphics, then the optional pointer subscription.
    Destroying the window releases pointer and keyboard input before graphics.
  - **Drawing.** SDL draws into its own surface. `SDL_UpdateWindowSurface` and
    `SDL_RenderPresent` copy the whole surface into a held display slot and
    [submit](../interfaces/graphics.md#slots-and-frame-handoff) it. Slots
    rotate and each keeps an older frame, so dirty rectangles alone would leave
    stale areas. The presenter shows only complete frames. The first
    submission shows graphics.
  - **Resize.** A display geometry change replaces the slots, resubmits the
    last frame at the new geometry, updates the display mode and sends SDL's
    resized event. A failed replacement keeps the old slots until the next
    change.
- **Event pump.** One `wait_many` poll covers display geometry, keyboard and the
  acquired pointer subscription. `SDL_WaitEvent` and positive-timeout
  `SDL_WaitEventTimeout` block on these interests; the next pump consumes that
  readiness observation once, avoiding a duplicate BSP handoff. Negative
  timeouts mean infinite, zero remains nonblocking, and finite deadlines survive
  re-arming at the native 30-second wait bound. No input session is acquired by
  waiting. The optional pointer is omitted until acquired.
- **Wakeup.** Events pushed from other threads end a blocking wait through a
  private pipe; see [threads and timers](#threads-and-timers).
- **Wait fallback.** Unsupported/failing waits, native ownership/backend errors,
  missing window/input sessions, or threads authority without a wakeup pipe
  retain upstream polling with a 1 ms delay. Initializing joysticks retains
  upstream's enumeration polling interval, even with zero devices.
- **Keyboard.** Pyxis key positions map to SDL scancodes. Text input comes from
  the US layout shared with the kernel's terminal (`pxe/key_layout.h`). Control,
  Alt and Super suppress text.
- **Clipboard.** Get/Set/Has use the separate local/shared grants and
  [physical graphics actions](../interfaces/clipboard.md#graphics-and-sdl2).
  Unarmed reads/publications refuse; shared Super+Shift+C/V becomes an
  identity-bound Ctrl command. Has does not consume Paste activation. UTF-8
  text is bounded to 64 KiB, with no upstream cached-success fallback. The
  [manual exercise](../../ports/sdl2/manual/README.md) is opt-in, outside ordinary images.
- **Pointer.** The [native protocol](../interfaces/pointer.md#userspace-pointer-sessions)
  provides 88-byte events with surface-local positions and geometry/mapping
  identities. Ordinary SDL motion follows those positions; native locked input
  supplies relative `dx`/`dy`. Each pump lazily queries geometry once for matching
  events and refreshes on state/geometry boundaries, preserving per-event identity
  and extent checks without a blocking BSP geometry query per motion report.
  Event order and motion/button/wheel samples are preserved.
  All 32 buttons and both precise 120-unit scroll axes
  reach SDL; positive native vertical scroll becomes SDL negative (down). Signed
  fractions cancel, and input/view boundaries clear wheel accumulation. `SDL_WarpMouseInWindow` requests bounded owner
  warp using current identities. Refusal sets an SDL error; success arrives
  through native input, without a synthetic SDL position update.
- **Relative mode.** `SDL_SetRelativeMouseMode` requires pointer ownership and
  a first SUBMIT before requesting native LOCK. A refused lock reports an
  error and leaves relative mode disabled. Super+Esc and focus/device loss can
  revoke lock; the event pump clears SDL relative mode, pending motion and held
  buttons without warping or automatically relocking. Fresh surface activation
  permits a new explicit request. Locked same-session resize/REPLACE preserves
  accepted buttons; ordinary geometry changes reset them.
- **Cursor.** SDL bitmap and color cursors become copied native surface images,
  1 through 64 pixels per dimension, with straight-alpha BGRA and an in-image
  hotspot. The default is the kernel arrow; `SDL_ShowCursor` changes native saved
  visibility, allowing a program to draw its own cursor. Lock forces hiding and
  restores the saved preference on unlock. Boot and Bochs compose images in
  software; VirtIO uses its hardware cursor and blends into active captures
  separately. See [display presentation](../kernel/display.md#software-pointer).
- **Focus.** Focus changes and input resets release every held key and button.
- **Timer.** Ticks and the performance counter come from `clock_now` in
  nanoseconds, and `SDL_Delay` from `clock_sleep_for`.
- **Paths.** `SDL_GetPrefPath(org, app)` creates and returns `home://APP/`; the
  organisation is not used. `SDL_GetBasePath` is unsupported, because programs
  cannot find their [own location](https://git.internal/PyxisOS/pyxis-os/issues/1378).

Facilities Pyxis lacks report themselves as unsupported, as upstream does:

- Audio capture and output device selection; there is one default output.
- Joysticks initialize with zero devices.
- Haptics, sensors, HIDAPI, shared objects, power and Vulkan are not built.
  OpenGL is available only through the separate OSMesa archive above.

The ordered upstream patches disable the dynamic API, register the video and audio drivers, skip
the Steam virtual gamepad file because Pyxis `stat` has no modification time,
respect native pointer position/lock authority, start the timer thread at the
first timer and stop it in `SDL_Quit`, carry private clipboard command identity, and preserve
signed wheel fractions and unsigned button masks. The pointer
patch prevents synthetic warp updates and relative-mode fallback after native
refusal; it retains other video drivers' behavior.

## Threads and timers

SDL threads use libc's joinable [thread profile](../userland/threads.md); the
contract was accepted by the owner on 2026-10-13 ([threads task 9](../interfaces/threads.md#delivery)).

- **Authority.** Creation needs the program's declared `threads/manage` grant
  (plus `memory/manage`; timed waits use the `clock` grant). Without it libc
  refuses and `SDL_CreateThread` returns NULL with libc's error. SDL adds no
  wrapper around creation, and plain programs, which receive no threads grant,
  are unchanged.
- **Synchronization.** Mutexes are libc recursive mutexes, conditions wait on
  the monotonic clock, semaphores are SDL's generic ones over those conditions,
  and SDL TLS uses libc keys. All of them really block.
- **Stacks, names and priorities.** SDL's default is libc's 1 MiB stack.
  Requested sizes are minimums: smaller ones receive 1 MiB, others round up to a
  page, and libc refuses more than 8 MiB. Names stay inside SDL
  (`SDL_GetThreadName`); Pyxis threads have no native name. Only
  `SDL_THREAD_PRIORITY_NORMAL` succeeds; other priorities report unsupported.
- **Detach.** The port does not use libc's detached threads yet. `SDL_DetachThread` sets an SDL error
  and the thread keeps running; libc's record, stack and TLS stay reserved until
  exit, and that uncollected record makes exit skip orderly finalizers.
- **Timers.** `SDL_INIT_TIMER` starts no thread, because ticks, delays and the
  performance counter need none. The first `SDL_AddTimer` starts SDL's timer
  thread, failing honestly without threads authority. `SDL_Quit` always stops
  and joins it, including a thread started without `SDL_INIT_TIMER`, which
  upstream leaves running.
- **Wakeup.** With threads authority, video initialization creates one private
  pipe from the declared `pipe/create` grant and keeps it until the process
  exits, so a racing sender can never write to a reused handle number. SDL calls
  the wakeup sender once per wait after queuing an event; it writes one byte
  without blocking, and a full pipe already means a pending wakeup. Blocking
  waits watch the read end, and the pump drains it. Before blocking, the wait
  checks SDL's queue, so an event queued before SDL published the waiting window
  is not missed. Without a `pipe` grant, threaded programs use upstream's 1 ms
  polling fallback. Without threads authority no other thread exists and waits
  block without a pipe.
- **Exit.** Join threads and call `SDL_Quit` before returning from `main`.
  A process exiting with live threads skips `atexit` handlers and stdio
  flushing; see [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1302).

## Audio

SDL audio callbacks use the space's native [PCM session](../interfaces/audio.md).
The contract was accepted by the owner on 2026-10-13 ([threads task 9](../interfaces/threads.md#delivery)).
SDL gains callbacks, not real-time guarantees.

- **Ownership.** One SDL output device holds the space's one native session
  from open to close. `SDL_INIT_AUDIO` acquires nothing. SDL refuses a second
  open in the same process; another owner in the space makes the open fail.
  `SDL_CloseAudioDevice` and `SDL_Quit` join SDL's callback thread, then release
  the session, which discards PCM still queued.
- **Authority.** `SDL_INIT_AUDIO` needs the `audio` grant (and `clock`).
  Opening also needs `threads/manage` for SDL's callback thread; without it the
  open fails ("Couldn't create audio thread") and the session is released.
  Plain programs receive the space's `audio` grant but no threads grant, so
  they stay silent with that error; [Chocolate Quake](../userland/chocolate-quake.md),
  [EDuke32](../userland/eduke32.md) and [DevilutionX](../userland/devilutionx.md)
  ship as bundles declaring both. No grant wrapper is added.
- **Format and buffers.** The device runs at the native 48 kHz S16LE stereo,
  with 1,024-frame (21.3 ms) buffers, one maximum native write. SDL's own
  audio stream converts other rates, formats, channel counts and callback sizes.
- **Queue and latency.** The callback thread waits until the session is
  writable and writes one buffer, so the native 80 ms queue stays near full.
  Calculated worst-case output latency, from the interface sizes and not
  measured, is about 140–160 ms including the hardware ring; tuning is
  [deferred](https://git.internal/PyxisOS/pyxis-os/issues/1397).
- **Pause.** As upstream: the callback stops and SDL writes silence, so the
  session stays held and the thread still wakes every buffer.
- **Underruns and failure.** A late or slow callback becomes native silence:
  an audible gap, counted by the session's starvation counter, with no
  stretching or repetition. SDL has no underrun report and the backend logs
  nothing. Engine failure disconnects the SDL device (`SDL_AUDIODEVICEREMOVED`);
  the callback keeps its nominal rate into a discarded buffer, as upstream.
  SDL's `TIME_CRITICAL` priority request is refused and ignored.

## Related development interfaces

- **libc:** `fileno` (the stream's existing descriptor, or -1 with `EBADF`),
  `fseeko`/`ftello`, a "C"-only `setlocale`, `roundf`, `sqrtf` and `wcslen`.
  SDL's environment calls use libc's `getenv` and `setenv`, so a value set with
  `SDL_setenv` is visible to `SDL_getenv` and libc; before the port declared
  `setenv`, SDL kept such values in a private table that lookups never read.
  `SDL_strtokr` and `SDL_trunc` also use libc; `SDL_iconv` stays SDL's own. See
  the [libc reference](../kernel/userspace.md#foundational-libc) and
  [stdio](../userland/stdio.md).
- **One US key layout** in `lib/key_layout.c`, compiled by the kernel and
  exported to libpyxis as `share/pyxis/key_layout.c`.
- **The SDK's CMake toolchain file**, [`share/pyxis.cmake`](sdk.md#cmake). The
  fastfetch, fmt and mbedtls recipes use it.
- **The ports runner's extra sources:** pinned dependencies fetched beside the
  main source, documented in the [ports README](../../ports/README.md).
- **CMake packages** for fmt (`fmt::fmt`) and SDL2.

## Integration choices

1. **First consumer:** DevilutionX, with the game data as a local build
   input.
2. **Presentation:** an SDL-owned surface, copied into the display mapping on
   present; whole frames are submitted since the frame handoff (2026-10-09).
3. **Paths:** `home://APP/` for preferences; no base path.
4. **DevilutionX packaging:** opt-in only, because of its non-commercial
   licence.
5. **`fileno`:** the agreed libc extension.
6. **The CMake system name** is `Pyxis`, with an SDK platform file.
7. **CMake language modes:** C is freestanding and C++ hosted, as in
   `pyxis.mk`.
8. **Existing CMake recipes** move to the SDK file.
9. **`DIABLO_DATA`** stages shareware data only; retail data is played with
   `--data-dir`.
10. **Personal use only:** images or bundles containing DevilutionX are not
    shared, because its licence and libmpq's GPL cannot both be met.
11. **Extra sources:** the ports runner fetches extra pinned sources, with
    FetchContent fully disconnected.

Games are packages that carry their programs and data; DevilutionX exists only
in the owner's personal `diablo` package. See
[package preparation](../../packages/README.md).

## Measurements

The original port measurements are from QEMU 10.2.2 with KVM in Claude's
Fedora VM (nested), 4 CPUs, on 2026-10-08. None are native.

A test program drew a 640x480 streaming texture at logical size every frame,
with `SDL_Delay(1)` between frames:

| Display | Window | Mean render and present |
| --- | --- | --- |
| Standard VGA, 1280x800 | 1280x768 | 959, 984, 1485, 1065 and 982 µs (five runs) |
| Bochs, `DISPLAY_SIZE=800x600` | 800x568 | 593 µs |
| VirtIO GPU, resized while running | 1280x768 → 1024x608 → 1440x868 → 800x468 | 741 µs over the run |
| VirtIO GPU at 800x468 | 800x468 | 384 µs |

The original `SDL_Delay(16)` probe ran at 24.8 ms per frame with tick-bound
sleep wakeups. Later [matched kernel qualification](experiments/sleep-wake-granularity/timer.md)
measured median mean frame times of 25.565 ms before, 24.243 ms with expiry
IPIs alone and 17.415 ms with per-CPU one-shot deadlines. Quake's ordinary
72 Hz cap improved from 49.223 to 59.595 to 70.291 FPS. These nested-KVM runs
changed only the kernel; HPET remains authoritative and nominal preemption
stays at 120 Hz. Native qualification remains in
[sleep wake granularity](https://git.internal/PyxisOS/pyxis-os/issues/1379).

DevilutionX's frame rates in town, at 1280x768 on standard VGA, are in its
[reference](../userland/devilutionx.md#measurements): 56.4–60.8 FPS with the
Pyxis default "Limit FPS", and 419–427 FPS uncapped.

The separate [pointer qualification](system-pointer-qualification.md#task-2-and-joint-integration)
records native ordinary motion, warp, lock refusal/revocation, game relock and
cursor choices. DevilutionX's supplied 33x28 cursor included 86 intermediate
alpha levels; its own software-cursor option hid the system cursor while
ordinary motion and clicks continued. Inventory navigation issued a successful
native bounded warp. These are separate interactive checks from the original
SDL milestone and frame-time samples above.

The [matched event-wait qualification](sdl2-event-wait-qualification.md) records
idle CPU usage and keyboard/pointer delivery with the existing upstream
`checkkeysthreads` consumer. Its optional thread is unsupported; its main event
loop still runs. DevilutionX menus and Quake's SDL path poll events and do not
exercise blocking waits.

The [threads and timers qualification](experiments/sdl-threads-timers/README.md)
records matched one/four-CPU frame timing, idle wait CPU, timer periods and
cross-thread wake latency before and after threads, all nested QEMU. The
[audio qualification](experiments/sdl-audio/README.md) records callback timing,
captured output, Chocolate Quake with and without sound, and the nested-QEMU
HDA guard trips seen during those runs.

## Limits

The [SDL2 port limits](https://git.internal/PyxisOS/pyxis-os/issues/1396) and
[DevilutionX port limits](https://git.internal/PyxisOS/pyxis-os/issues/1398) entries
record what is missing and when to revisit it.
