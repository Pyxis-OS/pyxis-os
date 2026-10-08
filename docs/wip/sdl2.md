# SDL2 with a native backend

Status: **task 3 delivered for review, 2026-10-08; assigned to Claude.** Tasks
1 and 2 are merged ([#522](https://git.internal/PyxisOS/pyxis-os/pulls/522),
[#525](https://git.internal/PyxisOS/pyxis-os/pulls/525)). The owner accepted
five [decisions](#accepted-decisions). Later tasks start one at a time.

## Goal and completion

Port upstream SDL2 with a Pyxis platform backend, then use it for one real
application. The backend covers video, keyboard, pointer, timing and paths over
the existing display, keyboard, pointer and clock sessions. Audio waits for an
audio driver.

The milestone finishes when:

- SDL2 is a reusable development library from the ports repository;
- the first consumer is in the ordinary image and plays in QEMU;
- the owner has checked it natively on the ThinkPad.

Implemented behaviour then replaces this document in the userland and
development references.

## What the probe ran

The probe compiled SDL2 and DevilutionX against the SDK and ran both in QEMU.
Its scaffolding is not committed; task 3 rewrites the backend properly.

| Input | Revision |
| --- | --- |
| Pyxis | `7dd2f3c` (main), userland `e8ad039`, ports `e85d307` |
| Toolchain | `pyxis-llvm` `49e2c1a` (the current pin) |
| SDL2 | `release-2.32.10` (`5d24957`), the last SDL2 release |
| DevilutionX | `1.5.5` (`7223eea`), the latest release |
| Shareware data | `spawn.mpq` from DevilutionX's asset release, SHA-256 `64427cd7…59356e38`; local only |

Setup: QEMU 10.2.2 with the AHCI fix, KVM in Claude's Fedora VM (nested),
4 CPUs, 8 GiB, standard VGA at 1280x800, so the content area is 1280x768. The
programs ran from `host://` in the Development space. Input was injected
through QMP and the screen read with `screendump`. Nothing ran on the ThinkPad.

### SDL2 against the SDK

The core library compiles with a hand-written `SDL_config.h`:

- `HAVE_LIBC` with the functions libc has, so SDL uses libc's allocator,
  strings and stdio; SDL's own `libm` copies cover the missing math;
- threads, audio, haptics, sensors, HIDAPI, shared-object loading and power
  disabled; the dummy joystick driver (zero devices) on;
- the probe backend for video, input and timing.

Built with the SDK's usual `-ffreestanding`, the whole archive links with
one missing libc function, `fileno`. Without `-ffreestanding`, Clang turns
SDL's fallback loops into calls to `wcslen`, `floorf`, `roundf` and `sqrtf`.

Upstream needs three small changes:

1. **Dynamic API off.** `SDL_dynapi.h` has no case for a platform without
   `dlopen`; without it every function is renamed `*_REAL`.
2. **Driver registration.** The backend has to be added to the bootstrap list
   in `SDL_video.c` and `SDL_sysvideo.h`.
3. **No `st_mtime`.** `SDL_steam_virtual_gamepad.c` reads `st_mtime`, which
   Pyxis `stat` does not have. It is compiled even with the dummy joystick
   driver.

Two upstream behaviours matter without threads:

- **`SDL_INIT_TIMER` fails** with "SDL not built with thread support",
  because callback timers need a thread. Ticks, the performance counter and
  `SDL_Delay` still work without it.
- **`SDL_INIT_JOYSTICK` fails** when joysticks are compiled out. DevilutionX
  requests it, so the probe used the dummy driver, which reports zero devices.

### A test program

A 640x480 streaming texture, drawn through the software renderer with a
logical size, filled the 1280x768 content area at 1024x768 with black side
borders. Key, button and motion events arrived with the expected names and
logical coordinates. Its loop rendered, presented and called `SDL_Delay(16)`:

- **Speed:** 482 frames took 11,958 ms; mean render-and-present was 726 µs
  (one run).
- **Pacing:** 24.8 ms per frame instead of about 17. Sleep wakes on the
  120 Hz preemption tick (8.33 ms), as [timekeeping](../kernel/timekeeping.md)
  describes, so a 16 ms sleep after a short frame lands on the third tick.

### DevilutionX

DevilutionX built with CMake, using a Pyxis toolchain file. The upstream
options that applied were `NONET`, `NOSOUND`, `DISABLE_LTO` and
`BUILD_TESTING=OFF`, plus its stub `Threads` module. It also needed:

- SDL2 from the probe;
- zlib and libpng from the existing ports;
- fmt 12.2 from the [fmt port](../development/ports.md#fmt-development-library),
  instead of DevilutionX's bundled fmt 10, which needs locales and `wstring`;
- SDL_image 2.0.5 (PNG only), bzip2, libmpq, libsmackerdec and SimpleIni,
  fetched from upstream at configure time.

With single-player, no network and no sound, it starts no threads.

To compile and link, it needed these libc functions, supplied by
probe-only stand-ins:

- `fileno`, `fseeko`/`ftello` and `wcslen`;
- `sqrtf` and `roundf`;
- `setlocale` with `<locale.h>`.

It also needed these source changes:

- `#include <new>` for `std::launder`, which libc++ does not include through
  `<memory>`;
- `<fmt/format.h>` where fmt 12 no longer includes it through `<fmt/core.h>`;
- `<cstdlib>` in the locale code;
- one `thread_local` made an ordinary global;
- `DVL_NO_FILESYSTEM`, because libc++ is built without `<filesystem>`;
- a Pyxis branch in `file_util.cpp` using `stat`, `fopen` and `ftruncate`.
  Without that branch, the directory check has no return path. Clang compiled
  it to a trap, and the first run faulted in `RecursivelyCreateDir`.

The executable is 3.45 MB; its loose assets are another 5.7 MB.

With `--data-dir host:// --save-dir home://diablo/`, the following worked:

- the Blizzard North intro (Smacker video);
- the shareware title and menus;
- hero creation, including name entry;
- entering Tristram and walking with mouse clicks;
- saving: the hero persisted in `home://diablo` and loaded on a second start.

Found while running:

- **Text input.** Name entry needs `SDL_TEXTINPUT`. The probe added a US-layout
  key-to-text table, matching the terminal's layout.
- **Cursor.** The default hardware-cursor option leaves the screen with no
  cursor, because the backend has no cursor support. With it off, DevilutionX
  draws its own.
- **Paths.** With SDL's dummy filesystem, the save path is empty. Starting a
  game then fails to create a directory and quits.
- **Frame rate** in town at 1280x768, five samples two seconds apart:
  - uncapped: 477–479 FPS;
  - its "Limit FPS" setting: 57.8–62.3 FPS. That limiter tracks deadlines
    itself, so the 8.33 ms wake granularity does not show.

## Planned scope

### SDL2 port

Upstream SDL2 2.32.10 as a development library in the ports repository,
like fmt, under `build/ports-dev/sdl2`:

- a Makefile with an explicit source list;
- a committed `SDL_config.h`;
- the three upstream patches above;
- the backend as recipe-local source files.

No fork repository. The backend's parts:

- **Video.** One window, always the size of the display content area and
  marked fullscreen. A software framebuffer, so the software renderer and
  `SDL_GetWindowSurface` both work. The display is acquired with the first
  framebuffer and presented once; resize follows the display generation
  through REPLACE and sends SDL's size-changed event. No OpenGL, Vulkan,
  hardware cursor, message boxes, clipboard or second window.
- **Keyboard.** Pyxis key positions map to SDL scancodes, keycodes follow
  SDL's US defaults, and text input uses the terminal's US layout.
  Focus gain, focus loss and reset release every held key, as Quake and Doom
  do.
  - The layout lives in one place. Today it is a table in
    `kernel/keyboard_text.c`. Task 3 moves it into a Pyxis-owned source that
    the SDK exports and libpyxis compiles, as the shebang parser already is
    (`share/pyxis/shebang.c`). The kernel and the backend then share one copy.
- **Pointer.** Relative counts become SDL motion. SDL keeps the absolute
  position, clamped to the window, because Pyxis reports none. There is no
  acceleration; `SDL_HINT_MOUSE_NORMAL_SPEED_SCALE` is the user's scale. Focus
  changes release buttons. Relative mouse mode works by SDL's own rules.
  - The translation from relative counts to a position stays in one function,
    so the planned [system pointer](pointer.md), which reports positions,
    replaces it in a single change.
- **Timer.** Ticks and the performance counter from `clock_now`, and
  `SDL_Delay` from `clock_sleep_for`. Without the clock grant, initialization
  fails with a clear error.
- **Paths:** see [decision 3](#3-sdl-paths).
- **Facilities without threads**, as upstream reports them:
  - `SDL_CreateThread` and `SDL_INIT_TIMER` fail;
  - mutexes and semaphores succeed as no-ops, which is correct for one thread;
  - `SDL_INIT_JOYSTICK` succeeds with zero joysticks;
  - audio is compiled out, so `SDL_INIT_AUDIO` fails.

  Making `SDL_INIT_TIMER` succeed is left until a consumer needs it.
- **Waiting.** `SDL_WaitEvent` keeps upstream's polling loop with a 1 ms
  delay, which becomes the 8.33 ms tick. A blocking wait on the input and
  display handles is later work; DevilutionX does not wait for events.

### First consumer

DevilutionX 1.5.5, single player, no network, no sound. Built with CMake
through a toolchain file the SDK exports as `share/pyxis.cmake`, beside
`pyxis.mk`. The probe's toolchain file is that file's starting point.

Its smaller dependencies (bzip2, libmpq, SDL_image's PNG loader,
libsmackerdec and SimpleIni) are pinned archives built inside the recipe.
They are not separate ports. Patches as listed above, plus:

- a Pyxis CMake platform file;
- hardware cursor off and its option hidden;
- default paths, so a bare `devilutionx` starts without arguments.

Its packaging changed after task 1; see [decision 4](#4-devilutionx-packaging).
Before task 5, record the licence of DevilutionX, of its assets, and of each
small dependency (bzip2, libmpq, libsmackerdec, SimpleIni and SDL_image) in
the recipe's `PORT-NOTICE`. Stage each licence wherever the program is staged,
as the other ports do.

### libc

Delivered in task 2: the standard functions the probe stood in for.

- `fileno`, under [decision 5](#5-fileno).
- `fseeko`/`ftello`, which forward to `fseek`/`ftell` because `off_t` is `long`.
- `setlocale` with `<locale.h>`, "C" only: it returns `"C"` for queries, `""`
  and `"C"`, and refuses every other name. There is no `localeconv`.
- `roundf`, `sqrtf` and `wcslen`, unmodified from the vendored musl.

The [libc reference](../kernel/userspace.md#foundational-libc) and
[stdio](../userland/stdio.md) describe them.

## Task 3: the SDL2 port

The [recipe README](../../ports/sdl2/README.md) describes the port as built.
Choices made while implementing, beyond the planned scope:

- **Sessions.** The window owns the input sessions. Creating it acquires the
  keyboard and pointer; destroying it releases them and the display. The
  display grant, keyboard grant and clock grant are all required.
- **Event pump.** One `wait_many` poll covers display geometry and keyboard
  readiness; the pointer, which cannot be waited on, is polled.
- **Headers.** The staged include tree replaces upstream's platform-dispatching
  `SDL_config.h` with the port's, so consumers and the library agree.
- **Build flags.** Upstream sources build with upstream's own `-Wall
  -fno-strict-aliasing` and the compiler's default C standard; one upstream
  warning remains (an unused XInput mapping). The backend uses the SDK's C23
  flags and builds without warnings. SDL's dummy video driver is not built.
- **One key layout.** The US table moved from `kernel/keyboard_text.c` to
  `lib/key_layout.c`. The kernel compiles it, and the SDK exports it as
  `share/pyxis/key_layout.c` for libpyxis, like the shebang parser.

Validation in QEMU 10.2.2 with the AHCI fix, KVM in Claude's Fedora VM (nested),
4 CPUs. A throwaway check program, not committed, drew a 640x480 streaming
texture at logical size every frame, with `SDL_Delay(1)` between frames:

| Display | Window | Mean render and present |
| --- | --- | --- |
| Standard VGA, 1280x800 | 1280x768 | 959, 984, 1485, 1065 and 982 µs (five runs) |
| Bochs, `DISPLAY_SIZE=800x600` | 800x568 | 593 µs |
| VirtIO GPU, resized while running | 1280x768 → 1024x608 → 1440x868 → 800x468 | 741 µs over the run |
| VirtIO GPU at 800x468 | 800x468 | 384 µs |

The VirtIO resizes came from QEMU's D-Bus display (`SetUIInfo` on a private
session bus), since this host has no GTK display.

Checked as working:

- **Initialization:**
  - `SDL_INIT_TIMER` fails with "SDL not built with thread support";
  - video with joysticks and game controllers starts, with zero joysticks;
  - `SDL_GetBasePath` is unsupported;
  - `SDL_GetPrefPath` creates `home://sdlcheck/`, and a file written there
    reads back.
- **Windows and resizing:**
  - a second window is refused;
  - each resize reaches the program as a size change, and the software
    renderer letterboxes again.
- **Input:**
  - keys and text, including Shift, Caps Lock and Shift+Caps;
  - Ctrl suppresses text;
  - the wheel follows SDL's sign;
  - clicks land at the expected logical coordinates;
  - relative mode toggles;
  - Super+Down and Super+Up give focus lost and focus gained.
- **The real consumer.** The probe's DevilutionX build, relinked against this
  library, played at 398 FPS at 1280x768. After a resize to 1024x640 it re-laid
  out for its 1024x608 window and ran at 538 FPS.

Not covered: key repeat, because QEMU's injected PS/2 input has no typematic
repeat, and native hardware.

## Accepted decisions

Accepted by the owner on 2026-10-08, as proposed in task 1.

### 1. First consumer and its data

**DevilutionX, with the game data as a local build input.**

- The probe found no stated redistribution terms for the shareware
  `spawn.mpq`, unlike Quake's shareware licence. So the data is not committed,
  pinned or put in CI images.
- `make image DIABLO_DATA=DIR` stages `spawn.mpq` or `DIABDAT.MPQ`, as
  `QUAKE_DATA` does for retail Quake.
- Packaging was revised after task 1; see [decision 4](#4-devilutionx-packaging).

### 2. Frame presentation

**SDL draws into its own surface.** The backend copies the updated
rectangles into the display mapping at `SDL_UpdateWindowSurface` and
`SDL_RenderPresent`.

- **Why.** The renderer clears and redraws every frame. Drawing directly into
  the shown mapping would let the presenter show cleared or half-drawn frames,
  not only the row tearing that the [single-buffer contract](../interfaces/graphics.md#mapping-and-presentation)
  already permits.
- **Cost.** One copy of the updated area per frame, plus an SDL-owned surface
  the size of the window: 3.75 MiB at 1280x768. The probe used this, and its
  726 µs frame included the copy.

Direct rendering into the mapping, with no copy, was not chosen. A later
compositor or double-buffering contract would replace this.

### 3. SDL paths

- `SDL_GetPrefPath(org, app)` returns `home://APP/` and creates it. The
  organisation name is ignored. Quake already uses `home://quake`;
  DevilutionX would get `home://devilution/`.
- `SDL_GetBasePath` reports unsupported, because Pyxis has no reliable
  executable path: `argv[0]` is whatever the launcher passes. A port's recipe
  sets its read-only paths instead; for DevilutionX that is
  `boot://share/devilutionx/`.

### 4. DevilutionX packaging

Accepted by the owner on 2026-10-08, after task 1's review.

- DevilutionX 1.5.5 is under the Sustainable Use License: free, non-commercial
  distribution only. It is not open source.
- So it is an **opt-in build**: the recipe builds and stages DevilutionX only
  when `DIABLO_DATA` is supplied. Ordinary and CI images never contain it,
  which keeps their terms independent of it.
- Whenever it is staged, its licence and notices go with it.

### 5. `fileno`

Accepted by the owner on 2026-10-08, as the separately agreed extension that
[technical debt](../technical-debt.md#libc-compatibility-gaps) requires.

- `fileno` returns the stream's current descriptor, or -1 with `EBADF` once the
  stream or its descriptor is closed.
- It adds no `fdopen`, `O_RDWR` or duplication, so no new descriptor aliases.

## Tasks

1. [x] Probe and proposal: this document. Documentation only.
2. [x] **libc additions**: [userland #161](https://git.internal/PyxisOS/pyxis-userland/pulls/161)
   and its Pyxis gitlink PR.
3. [x] **SDL2 port**: [ports #60](https://git.internal/PyxisOS/pyxis-ports/pulls/60),
   [userland #163](https://git.internal/PyxisOS/pyxis-userland/pulls/163) and their
   Pyxis PR, with the integration that builds it into
   `build/ports-dev/sdl2`:
   - the backend, patches and configuration;
   - the shared US key layout (Pyxis and userland);
   - checked in QEMU with a test program that is not committed, on standard
     VGA, Bochs, and VirtIO with a resize.
4. [ ] **SDK CMake toolchain file** (Pyxis): `share/pyxis.cmake`, documented in
   the SDK reference.
5. [ ] **DevilutionX** (ports and Pyxis):
   - the recipe, dependencies and patches;
   - licence notices for it and each dependency;
   - the opt-in `DIABLO_DATA` build and image staging;
   - a userland reference;
   - QEMU play with shareware data.
6. [ ] **Native check by the owner**, then close: the reference documents,
   technical debt and index.

## Owner actions

- Mirrors: done on 2026-10-08. `git.internal/mirrors/` has SDL, DevilutionX,
  bzip2, libmpq, libsmackerdec and simpleini, and every pinned commit fetches
  by hash. SDL_image's release archive comes through `raw-github`.
- No compiler container rebuild: nothing changes in the toolchain.

## Limits

These are stated now, so the implementation does not hide them:

- No audio, threads, game controllers or networking.
- One window, no hardware cursor, US text layout only.
- Polling `SDL_WaitEvent`.
- Sleep at 8.33 ms granularity, a kernel limit recorded in
  [technical debt](../technical-debt.md#sleep-wake-granularity).
- Programs keep running and rendering while hidden, as Quake and Doom do.
- All measurements are from nested KVM; none are native.
