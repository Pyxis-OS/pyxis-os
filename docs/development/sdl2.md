# SDL2

Pyxis carries upstream SDL 2.32.10 with a native backend, as a development
library for graphical ports. Its first consumer is
[DevilutionX](../userland/devilutionx.md), an opt-in build. Its
[native ThinkPad qualification](../technical-debt.md#sdl2-and-devilutionx-native-qualification)
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

## The backend

- **Video.** One window, always the size of the display content area and
  marked fullscreen; a second window is refused.
  - **Sessions.** Creating the window acquires the keyboard. Creating its
    framebuffer acquires graphics, then the optional pointer subscription.
    Destroying the window releases pointer and keyboard input before graphics.
  - **Drawing.** SDL draws into its own surface. `SDL_UpdateWindowSurface` and
    `SDL_RenderPresent` copy the updated rectangles into the display mapping,
    so the presenter never shows a cleared or half-drawn frame, though rows can
    tear. The first presentation shows graphics.
  - **Resize.** A display geometry change replaces the mapping, updates the
    display mode and sends SDL's resized event. A failed replacement keeps the
    old mapping until the next change.
- **Event pump.** One `wait_many` poll covers display geometry, keyboard and the
  acquired pointer subscription. `SDL_WaitEvent` and positive-timeout
  `SDL_WaitEventTimeout` block on these interests; the next pump consumes that
  readiness observation once, avoiding a duplicate BSP handoff. Negative
  timeouts mean infinite, zero remains nonblocking, and finite deadlines survive
  re-arming at the native 30-second wait bound. No input session is acquired by
  waiting. The optional pointer is omitted until acquired.
- **Wait fallback.** Unsupported/failing waits, native ownership/backend errors
  or missing window/input sessions retain upstream polling with a 1 ms delay.
  Initializing joysticks retains upstream's enumeration polling interval, even
  with zero devices. The threadless build has no asynchronous SDL event producers
  and needs no `SendWakeupEvent`; threads will require a real wakeup sender and
  a review of the immediate-next-pump readiness cache.
- **Keyboard.** Pyxis key positions map to SDL scancodes. Text input comes from
  the US layout shared with the kernel's terminal (`pxe/key_layout.h`). Control,
  Alt and Super suppress text.
- **Pointer.** The [native protocol](../interfaces/pointer.md#userspace-pointer-sessions)
  provides 56-byte events with surface-local positions and geometry/mapping
  identities. Ordinary SDL motion follows those positions; native locked input
  supplies relative `dx`/`dy`. `SDL_WarpMouseInWindow` requests bounded owner
  warp using current identities. Refusal sets an SDL error; success arrives
  through native input, without a synthetic SDL position update.
- **Relative mode.** `SDL_SetRelativeMouseMode` requires pointer ownership and
  a first PRESENT before requesting native LOCK. A refused lock reports an
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
  cannot find their [own location](../technical-debt.md#program-location).

Facilities Pyxis lacks report themselves as unsupported, as upstream does:

- `SDL_CreateThread` fails, and so does `SDL_INIT_TIMER`, whose callback timers
  need a thread. Mutexes and semaphores succeed and do nothing, which is
  correct with one thread.
- `SDL_INIT_AUDIO` fails; conversion and WAV loading work.
- Joysticks initialize with zero devices.
- Haptics, sensors, HIDAPI, shared objects, power, OpenGL and Vulkan are not
  built.

Upstream needs five patches: the dynamic API off, the driver registered, the
Steam virtual gamepad file skipped because Pyxis `stat` has no modification
time, native pointer position/lock authority in SDL mouse core, and permission
for the threadless Pyxis wait hook to run without a wakeup sender. The pointer
patch prevents synthetic warp updates and relative-mode fallback after native
refusal; it retains other video drivers' behavior.

## Related development interfaces

- **libc:** `fileno` (the stream's existing descriptor, or -1 with `EBADF`),
  `fseeko`/`ftello`, a "C"-only `setlocale`, `roundf`, `sqrtf` and `wcslen`. See
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
   present.
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
[sleep wake granularity](../technical-debt.md#sleep-wake-granularity).

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

## Limits

The [SDL2 port limits](../technical-debt.md#sdl2-port-limits) and
[DevilutionX port limits](../technical-debt.md#devilutionx-port-limits) entries
record what is missing and when to revisit it.
