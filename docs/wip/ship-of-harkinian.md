# Ship of Harkinian on Pyxis

Investigation and task plan accepted; implementation authorised in order
(chronium, 2026-10-24). Tasks 1–5 are qualified; later tasks continue automatically,
with a stop for new material behaviour, authority or lifetime decisions.
Goal: Ocarina of Time through SoH, first on Mesa softpipe with keyboard input.
A slow first frame is acceptable; neither llvmpipe/JIT nor gamepad drivers are
prerequisites. Private host extraction and the public native LUS slice are qualified; the
private game reaches its title/menu; gameplay, audio and persistence remain unqualified.

## Sources and distribution boundary

Current stable is [9.3.1, Dewey Bravo](https://github.com/HarbourMasters/Shipwright/releases/tag/9.3.1),
SoH `f0599f96f8d442f26d0fa1c5cedafddacd56633f`. Its
[gitlinks](https://github.com/HarbourMasters/Shipwright/tree/f0599f96f8d442f26d0fa1c5cedafddacd56633f)
select libultraship (LUS) `da6daf1a50a5e46d6641408d52227e31e85e46df`
and Torch `2ab12fe9660aec04e02ee89fe81baed304a1a1d6`.
LUS and Torch have no further gitlinks; their build files fetch other sources.
The Pyxis comparison uses main `0f4153cb`, ports `a3bfc58a` and userland/SDK
`1453c097`; existing recipes retain their own immutable source pins.

The SoH root supplies no LICENSE/COPYING grant for the complete game/decomp
source. Its nested [randomizer MIT notice](https://github.com/HarbourMasters/Shipwright/blob/f0599f96f8d442f26d0fa1c5cedafddacd56633f/soh/soh/Enhancements/randomizer/3drando/LICENSE.md)
does not cover the whole tree. Do not classify a complete SoH package as public
from [LUS](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/LICENSE)/
[Torch](https://github.com/HarbourMasters/Torch/blob/2ab12fe9660aec04e02ee89fe81baed304a1a1d6/LICENSE)
MIT licences. Keep the game-derived build/private recipe separate;
private placement itself establishes no redistribution permission. Independently
licensed libraries can become public reusable development packages after their
own notice/provenance checks, without copying game sources into them.

ROM bytes and every ROM-derived asset stay under `/shared/oot/`, outside public
repositories, CI, logs and packages, until a private package exists. The authorised
private comparison matches the supported **NTSC 1.2 (US)** entry in the pinned
[supported list](https://github.com/HarbourMasters/Shipwright/blob/f0599f96f8d442f26d0fa1c5cedafddacd56633f/docs/supportedHashes.json),
rather than the brief's GameCube label. USA GameCube is also supported; extraction
must select the actual matched version. No private digest or ROM bytes are recorded
here. Gameplay screenshots are permitted (chronium, 2026-10-24).

## Rendering: enough API to investigate, not a game qualification

The [desktop SDL backend](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/backends/gfx_sdl2.cpp#L340)
requests depth24, stencil8 and double buffering. Its GL4.1/core/forward-compatible
request is Apple-only. Generic desktop game shaders use GLSL `#version 110` vertex/`130` fragment;
[ImGui uses GLSL `#version 120`](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/Fast3dGui.cpp#L146).
The [renderer](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/backends/gfx_opengl.cpp#L283)
uses unsuffixed FBO/renderbuffer calls, RGB8 and DEPTH24_STENCIL8 attachments,
blits, packed depth readback and desktop `GL_DEPTH_CLAMP`; no additional shader
`#extension` requirement was found in the default path. The FBO/GLSL path
needs GL3.0-era features plus depth clamp (core3.2 or its extension); explicitly
select the measured GL3.3 compatibility profile for the native baseline.

A query-only copy of the existing task-3 gears caller measured these actual
context strings in nested QEMU, four CPUs/4 GiB, on 2026-10-24:

| Query | Result |
| --- | --- |
| `GL_VENDOR` | `Mesa` |
| `GL_RENDERER` | `softpipe` |
| `GL_VERSION` | `3.3 (Compatibility Profile) Mesa 26.2.5` |
| `GL_SHADING_LANGUAGE_VERSION` | `3.30` |
| `GL_EXTENSIONS` | 266 tokens; includes `GL_ARB_framebuffer_object`, `GL_EXT_framebuffer_blit`, `GL_EXT_packed_depth_stencil`, `GL_ARB_depth_clamp` |

This used the already qualified SDL/Mesa archives, current SDK, and a copy of
the OpenArena qualification image (kernel source `0f4153cb`, qualification
configuration). Only the private ISO's Remote beacon option was removed. Raw
queries, three-line diagnostic diff, build inputs and image receipts are in
`/shared/present/soh/source-evidence/gl-query/` and adjacent records. No game or
performance claim follows from strings/extensions. Existing
[task-2](../development/experiments/mesa-softpipe.md) and
[task-3](../development/experiments/sdl2-osmesa.md) rendering checks remain the baseline.

Start with the [explicit 320×240 N64 preset](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/src/fast/Fast3dGui.cpp#L335),
MSAA1/off and swap interval0. This preset is not the renderer's absolute minimum.
MSAA2–8 requests multisampled FBOs, capped by `GL_MAX_SAMPLES`; the selected
softpipe source rejects sample counts above 1. SDL also refuses context MSAA
and nonzero swap intervals. Keep the
existing game pacing rather than claiming vsync. Qualify SoH's generated shaders,
FBO/depth/readback and ImGui together before declaring compatibility.

Native integration needs static `SDL2::SDL2-osmesa-static`, not GLX/EGL/GLES or a
pretend Linux target. LUS's generic non-Linux/OpenBSD branch incorrectly selects
`glewInit()` for a new OS. ImGui's default GL loader calls `dlopen`/`dlsym` on
shared GL libraries; select its supported custom-loader path with the existing
static GL procedures. No driver, shared-library shim or executable-memory grant
is needed for this profile; llvmpipe remains a separate JIT/BSP-removal track.

## Dependency closure

This is the upstream desktop closure, with proposed host/target separation below.
Upstream system requirements are not all version-pinned, so a fully reproducible
Pyxis closure is not resolved yet. Exact declarations are in
[root CMake](https://github.com/HarbourMasters/Shipwright/blob/f0599f96f8d442f26d0fa1c5cedafddacd56633f/CMakeLists.txt),
[game CMake](https://github.com/HarbourMasters/Shipwright/blob/f0599f96f8d442f26d0fa1c5cedafddacd56633f/soh/CMakeLists.txt),
[LUS common](https://github.com/kenix3/libultraship/blob/da6daf1a50a5e46d6641408d52227e31e85e46df/cmake/dependencies/common.cmake)
and [Torch CMake](https://github.com/HarbourMasters/Torch/blob/2ab12fe9660aec04e02ee89fe81baed304a1a1d6/CMakeLists.txt).
Each linked pin below points to its licence at the immutable commit. System
requirements remain unpinned upstream; licence names for those families do not
replace the chosen-version/component audit in task 2.

| Component | Pin / licence | Pyxis disposition |
| --- | --- | --- |
| SDL2 / Mesa | Existing pinned ports: 2.32.10 / 26.2.5 | Reuse `sdl2-dev`, `sdl2-osmesa-dev`, `mesa-dev`; selected closure and notices already qualified. |
| C/C++ runtime | Current matching SDK | Reuse `pyxis-dev`, `libcxx-dev`; C23/C++20, exceptions, RTTI, iostreams, static TLS and real synchronization. |
| zlib / libpng / fmt | Existing 1.3.2 / 1.6.59 / 12.2.0 | Existing `-dev` packages. README's libpng prerequisite is not a direct selected game link; stb does image decoding. |
| ImGui | [4806a1924ff6](https://github.com/ocornut/imgui/blob/4806a1924ff6181180bf5e4b8b79ab4394118875/LICENSE.txt), 1.91.9b-docking, MIT | New reusable `imgui-dev`, matching LUS patch/config (`ImTextureID=void*`), core and selected SDL2/GL3 backends; no mixed headers/binary ABI. |
| spdlog | [486b55554f11](https://github.com/gabime/spdlog/blob/486b55554f11c9cccc913e11a87085b2a91f706f/LICENSE), 1.16.0 fallback, MIT | New `spdlog-dev`; upstream may instead find system ≥1.10. Bundled fmt12.0 differs from existing fmt12.2; preserve/audit that profile or qualify external fmt explicitly. |
| nlohmann-json | Unpinned system requirement, [MIT](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) | New reusable `json-dev`, with a chosen immutable pin before building. |
| tinyxml2 | [321ea883b719](https://github.com/leethomason/tinyxml2/blob/321ea883b7190d4e85cae5512a12e5eaa8f8731f/LICENSE.txt), 10.0.0, Zlib | New `tinyxml2-dev`; used by LUS resource import, also forced by Torch. |
| libzip | Unpinned system requirement, [BSD-3-Clause](https://github.com/nih-at/libzip/blob/v1.11.4/LICENSE) | New `libzip-dev`; choose deflate-only O2R profile first, not an implicit bzip2/xz/zstd/crypto closure. |
| StormLib | [28c9b4be3f23](https://github.com/ladislav-zezula/StormLib/blob/28c9b4be3f23c6b3a5ff55cacac7dbe5b9cdc4fc/LICENSE), 9.25, MIT plus bundled terms | SoH forces MPQ on despite LUS default off. New reusable `stormlib-dev` only if retaining legacy `.otr`/MPQ; proposed first target omits it. |
| yaml-cpp | [56e3bb550c91](https://github.com/jbeder/yaml-cpp/blob/56e3bb550c91fd7005566f19c079cb7a503223cf/LICENSE), 0.9.0, MIT | Torch dependency; host-only if extraction is separated. If retained on target, new reusable `yaml-cpp-dev`. |
| thread-pool | [097aa718f25d](https://github.com/bshoshany/thread-pool/blob/097aa718f25d44315cadb80b407144ad455ee4f9/LICENSE.txt), 4.1.0, MIT | New reusable header-only `thread-pool-dev`; real C++ workers, no synchronous imitation. |
| prism / Monocypher | [1de054450e7b](https://github.com/KiritoDv/prism-processor/blob/1de054450e7b3c5f777d2e3dfcb228ad120c329d/LICENSE), MIT / [0d85f98c9d9b](https://github.com/LoupVaillant/Monocypher/blob/0d85f98c9d9b0227e42cf795cb527dff372b40a4/LICENCE.md), BSD-2-Clause or CC0 | New reusable development packages for the selected LUS closure. |
| libgfxd | [008f73dca8eb](https://github.com/glankk/libgfxd/blob/008f73dca8ebc9151b205959b17773a19c5bd0da/LICENSE), MIT | Excluded from the first profile: `GFX_DEBUG_DISASSEMBLER=OFF` (chronium, 2026-10-24); no `libgfxd-dev` required. |
| dr_libs | [da35f9d6c737](https://github.com/mackron/dr_libs/blob/da35f9d6c7374a95353fd1df1d394d44ab66cf01/LICENSE), MIT-0 or Unlicense | New reusable header package for selected WAV/MP3/FLAC decoders. |
| Ogg / Vorbis / Opus / OpusFile | 1.3.6 / 1.3.7 / 1.6.1 / 0.12; BSD-style component licences | `libogg-dev`, `libvorbis-dev`, `opus-dev`, `opusfile-dev`; Vorbis encoder/file libraries included. Opusfile keeps local FILE/memory/callback APIs; only `op_freopen` is explicitly unavailable (chronium, 2026-10-24). |
| SDL2_net | Unpinned; Zlib | No current `-dev`; Linux's link is unconditional even though discovery is soft. Exclude remote integrations explicitly or port reusable native networking separately; no fake sockets. |
| stb image | [0bc88af4de5f](https://github.com/nothings/stb/blob/0bc88af4de5fb022db643c2d8e549a0927749354/stb_image.h), MIT or public domain | Shared header dependency if selected on target; Torch also bundles a different stb copy. Preserve each source's notices. |
| SDL controller database | Raw `master` download during configure | Pin and retain its licence before any build; no live configure download in a recipe. No controller driver is assigned here. |
| Torch / SoH helpers | Torch MIT root; game-local extraction glue and UI | Host extraction tool; SoH's ImGuiUtils/UIWidgets stay game-local, not invented reusable extensions. |

No ImGuizmo, ImPlot or other third-party ImGui extension occurs at these pins.
If an extension is selected later it gets its own reusable `-dev` package, as
chronium requires. Do not create an unused ImGuizmo dependency. Preserve ImGui's
imstb/ProggyClean notices, LUS's Fast3D/hash/glob notices, and its embedded Font
Awesome font's OFL1.1 notice: the public font bytes were matched exactly to
Font Awesome 4.7.0. StormLib's bundled bzip2 and Torch's bundled miniz-cpp, TinySHA1,
CLI11, compression/graphics helpers require their component notices/provenance;
root MIT alone is insufficient. Torch fetches zlib 1.3.1 despite the root finding
system zlib; keep that host selection separate from target zlib 1.3.2.

Before implementing dependencies, pin the remaining system libraries and request
cache entries for the exact archives/commits. Builds use those mirrors only.
The immutable-fetch/licence audit and public source receipts stay under
`/shared/present/soh/closure-audit/`; no ROM was used for that audit.

## Assets and native platform work

This release migrated from ZAPD/OTRExporter to Torch. Stale sanitizer target names
remain, but they do not establish the current asset path. Upstream
[build instructions](https://github.com/HarbourMasters/Shipwright/blob/f0599f96f8d442f26d0fa1c5cedafddacd56633f/docs/BUILDING.md)
and the root's host-tool targets generate `soh.o2r`; Torch's
[`o2r` CLI](https://github.com/HarbourMasters/Torch/blob/2ab12fe9660aec04e02ee89fe81baed304a1a1d6/src/main.cpp#L140)
reads a supported ROM plus SoH's versioned YAML definitions to produce `oot.o2r`
(or MQ separately). Prefer SoH's `soh-torch`/`ExtractAssets` wrapper: it forces
`PORT_VERSION_ENDIANNESS=ON`, matching the game's seven-byte `portVersion`
reader. Standalone Torch defaults it off; matching pins alone are insufficient.
Linux host extraction is qualified in task 3. ROM inputs, output, scratch and logs go
under `/shared/oot/`, never CI. Runtime reads version-compatible archives;
missing/outdated data must report the problem rather than invoke a file picker.

Upstream currently links Torch into the game and
[calls it from startup](https://github.com/HarbourMasters/Shipwright/blob/f0599f96f8d442f26d0fa1c5cedafddacd56633f/soh/soh/Extractor/TorchExtract.cpp).
Host extraction alone does **not** remove that target closure. Separate the
extraction sources, link dependency and startup/file-dialog callers deliberately;
otherwise a target port still needs Torch, YAML/helpers and process/file-picker
assumptions. MPQ removal must clear both the forced option and public definition;
network removal must gate sources/callers, including unconditional SDLNet init/quit,
not merely hide settings.

| Area | Concrete work before the first run |
| --- | --- |
| Build / language | Native Pyxis CMake branch and static SDK/SDL/Mesa targets. Clang 23 accepts C23/C++20; SDK TLS, exceptions/RTTI and real C++ synchronization exist. Audit the actual imports at target link. Preserve `-fno-fast-math -ffp-contract=off`; no Linux impersonation or arch assumptions outside the proper layer. The private target links in task 5; see its [record](../development/experiments/soh-first-frame.md). |
| Files / saves | Separate read-only package/assets from writable `home://` config, saves, mods and logs. Remove `/proc/self/exe`, getpwuid/HOME/XDG/APPIMAGE discovery; URI text grants no authority. Keep owned native roots and real filesystem errors. SaveManager already checks temp-file write/flush/fsync/close and rename; add/verify native parent-directory publication sync and propagate failed saves. |
| Threads | Resource pool, save worker, game mixer, SDL callback and logging pools use real threads, futures, mutexes/conditions. SDK supports these; no POSIX cancellation/signals. Bound worker selection when hardware_concurrency is unavailable; measure live threads, eager stack/backing and shutdown before setting budgets. No thread-count/peak estimate is claimed. |
| Audio / time | SDL queued S16 stereo output plus the game's mix thread; existing SDL converts to native 48 kHz and owns its callback thread. Grant audio/threads/precise clock and pipe wakeups explicitly; timers use SDL performance counters/delays. Qualify actual buffering/close/underrun behavior, not just link success. |
| Loading / processes | Disable native DLL add-ons and TinyCC scripting/codegen; no JIT for the first profile. Replace ImGui shared-GL loading with static GL. Omit unsupported POSIX crash/signal handlers in favour of normal native fault reporting; no signal or dlopen success stubs. Network/CrowdControl/Anchor and eSpeak integration are outside the first profile. |
| Input | LUS uses SDL scancodes/key state, key/text/focus events; GUI also uses pointer/wheel. Existing SDL keyboard is the first route. GameController discovery/mappings, axis/buttons, hotplug, rumble/gyro are a later native SDL controller track (DualSense Edge/new Steam Controller), not requirements for a keyboard run. |

## Proposed tasks

Implementation proceeds in task order through focused PRs. First checks are
manual, not a new test framework.
Do not lift capacities pre-emptively: measure image/span/stack/peak backing at the
first target links and stop with a bounded proposal if the current ceilings fail.

1. [x] **Reusable ImGui slice.** Pin/cache `imgui-dev` with matching configuration,
   font/component notices and independently selected static SDL/GL backends.
   Delivered `imgui-dev`, separate `imgui-sdl2-opengl3-dev` and the asset-free
   `imgui-demo` package. Nested QEMU: softpipe window, keyboard text/navigation,
   pointer clicks and 1:1 output-following resize; independent staged CMake
   consumer and GDB inspection. [Record](../development/experiments/imgui-native.md).
   The 1:1 window also ran natively on vela; a game frame remains later work.
   **After this task chronium can:** run a reusable native ImGui application.
2. [x] **Remaining shared closure.** Pin unversioned inputs and deliver required
   reusable `-dev` leaves, one library or coupled family per PR, with their
   independent link checks and licence closure. Separate host-only dependencies.
   Delivered profiles and limits: [library development packages](../development/library-dev-packages.md).
   **After this task chronium can:** use the selected libraries in other programs.
3. [x] **Private host extraction.** Pinned Linux host wrappers produced private
   `soh.o2r` and `oot.o2r` for 9.3.1 with `PORT_VERSION_ENDIANNESS=ON`.
   Independent ZIP reopening passed all CRCs, exact port versions and game
   version byte order. The bounded filename-to-owned-bytes wrapper fix, scripts,
   sources, archives and logs stay private. No target game run or public asset.
   [Qualification](../development/experiments/soh-host-extraction.md).
   **After this task chronium can:** prepare his private game data once on Linux.
4. [x] **LUS platform slice.** `libultraship-dev` and the public `lus-demo`
   package use native URI paths, static SDL/GL, declared resources and real joined
   workers/audio producers. A public O2R fixture, uncached one-worker bulk decode,
   UI/FBO/depth, keyboard/pointer/1:1 resize and shutdown are qualified in nested
   QEMU; missing/read-only roots fail visibly. [Record](../development/experiments/lus-native-resources.md).
   No game data or target game run is included.
   **After this task chronium can:** run the native engine/resource substrate.
5. [x] **Private SoH build and first frame.** Actual game code links without
   target extraction and reads the frozen private archives. Nested QEMU renders
   the title/menu with the 320×240 internal 4:3 preset, MSAA off; normal quit and
   missing/invalid-archive failure are qualified. Real audio-worker failure is
   reported and cleaned up. [Record](../development/experiments/soh-first-frame.md)
   gives static size, observed backing and scene-dependent FPS, without a peak,
   native-audio or gameplay claim. No capacity was raised.
   **After this task chronium can:** see Ocarina of Time running on Pyxis.
6. [ ] **Keyboard play, audio and persistence.** Map controls, test focus/lift,
   play a short route, save/reload and refused/full save destinations; qualify
   SDL audio and pacing without sticky input or shutdown leaks.
   [Baseline and current limits](../development/experiments/soh-gameplay.md).
   **After this task chronium can:** play and retain progress with the keyboard.
7. [ ] **Native qualification.** Coordinated hardware session, proper output/input
   authority, measured performance/resources and gallery screenshot if wanted.
   **After this task chronium can:** run the private game on his hardware.

## Decisions (accepted, chronium, 2026-10-24)

1. **First profile:** stable 9.3.1, desktop GL3.3 compatibility on softpipe,
   QEMU first run at N64-native **320×240, 4:3**, using SoH's N64-style preset
   with matching internal resolution, MSAA off and interval0; keyboard and
   existing SDL audio. Keep
   networking, TTS, native DLL/TinyCC add-ons and gamepads outside this slice.
   No llvmpipe/JIT prerequisite or minimum-FPS promise.
2. **Assets/format:** version-pinned Linux host extraction, read-only
   O2R archives on Pyxis, no target ROM/extractor/file picker. Explicitly gate
   Torch out of the game target and disable legacy MPQ/StormLib for the first run;
   retain ZIP/deflate support. Additional formats/features can be assigned later.
3. **Packaging/authority:** independent licensed libraries in reusable
   public `-dev` packages; complete SoH bundle/private assets and recipe stay
   private. Declare only memory, threads, precise clock, display/input, audio,
   event pipe and needed random authority. Bind private assets read-only through
   the existing bundle/package mechanism; store state under `home://soh/` using
   ordinary home authority, with no claim that a subdirectory name isolates it.
   No launcher/network grant or call-site wrapper.
   Publication waits for each component's licence/provenance closure.

## Task 6 decision (proposed)

The long nested default-SDL baseline reproduced terminal HDA stable-position
sampling exhaustion: the last in-range, aligned LPIB pair moved from 1124 to
1324 with no recorded FIFO/descriptor error. Correcting observation timestamp
ordering is ordinary correctness; changing the documented terminal
classification requires a decision. No such recovery change is implemented.

1. **Default: recover a validated moving position through the existing bounded
   timing-recovery path.** After the fixed three-scan budget is exhausted, read
   and validate current RUN/status, both last positions' range/alignment and
   absence of controller/FIFO/descriptor errors. If only position movement
   prevented a stable observation, stop/reset/reprime the stream, discard the
   old epoch and report a discontinuity using the existing recovery budget.
   Keep the ring, timing limits, burst reserve and repeated-recovery bounds.
   Invalid, stopped, errored or uncertain controller state stays terminal.
   This is a reusable HDA rule, not a game/device-ID exception. Alternatively,
   retain terminal sampling exhaustion and leave sustained-audio qualification
   blocked when it occurs. Native behavior is not established by this QEMU run.
