# SDL2

Pyxis carries upstream SDL 2.32.10 with a native backend, as a development
library for graphical ports. Its first consumer is
[DevilutionX](../userland/devilutionx.md), an opt-in build. The milestone
completed on 2026-10-08. Its
[native ThinkPad check is deferred](../technical-debt.md#sdl2-and-devilutionx-native-qualification).

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
  - **Sessions.** Creating the window acquires the keyboard and pointer;
    destroying it releases them and the display.
  - **Drawing.** SDL draws into its own surface. `SDL_UpdateWindowSurface` and
    `SDL_RenderPresent` copy the updated rectangles into the display mapping,
    so the presenter never shows a cleared or half-drawn frame, though rows can
    tear. The first presentation shows graphics.
  - **Resize.** A display geometry change replaces the mapping, updates the
    display mode and sends SDL's resized event. A failed replacement keeps the
    old mapping until the next change.
- **Event pump.** One `wait_many` poll covers display geometry and keyboard
  readiness; the pointer is polled.
- **Keyboard.** Pyxis key positions map to SDL scancodes. Text input comes from
  the US layout shared with the kernel's terminal (`pxe/key_layout.h`). Control,
  Alt and Super suppress text.
- **Pointer.** Relative counts become SDL motion in one function, and SDL keeps
  the position, clamped to the window. Warping moves SDL's copy, and relative
  mode works from the same counts. The [system pointer](../wip/pointer.md)
  replaces that function after this milestone.
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

Upstream needs three patches: the dynamic API off, the driver registered, and
the Steam virtual gamepad file skipped because Pyxis `stat` has no
modification time.

## What the milestone added elsewhere

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

## Accepted decisions

The owner accepted these on 2026-10-08:

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

## Limits

The [SDL2 port limits](../technical-debt.md#sdl2-port-limits) and
[DevilutionX port limits](../technical-debt.md#devilutionx-port-limits) entries
record what is missing and when to revisit it.
