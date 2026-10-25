# SoH: private target startup

Task 5 of [Ship of Harkinian](../../wip/ship-of-harkinian.md). The real
9.3.1 game and its native recipe remain private: the complete source has no
blanket distribution grant, and extracted assets remain under `/shared/oot/`.
No ROM, assets, private digests, game source or target image is published here.
Gameplay screenshots are separately permitted.

## Inputs and configuration

SoH `f0599f96f8d442f26d0fa1c5cedafddacd56633f`, LUS
`da6daf1a50a5e46d6641408d52227e31e85e46df`, and the frozen, independently reopened
task-3 archives. The target does not run Torch or inspect a ROM. A separate
native CMake entry selects actual game objects and the staged static LUS target;
network/TTS, physical-controller discovery, host extraction/file pickers and
dynamic extensions are excluded at their sources and callers. C23/C++20,
`-fno-fast-math -ffp-contract=off`, real SDK workers, TLS and exceptions.

Baseline: main `d720b6d9`, full image built before the game adaptation. The
qualification image uses the kernel/code at `4c5ed5f4` (intervening
documentation/LuaLS changes, no kernel or ABI-header change). The final PR is
rebased onto `2e56b541`; its added CA-bundle pin does not change the LUS/game
closure. Existing builder
`pyxis-llvm23.1.3-3d69e83`; no compiler/container rebuild. Public dependencies:
userland #382 (`nearbyintf`, using the rounding-aware implementation without
raising a new inexact flag), ports #179 (public stb include dependency and
rebuilding an ImGui atlas dirtied by application fonts after initialization).
The normal mirror-only LUS recipe and both game targets were rebuilt.

Nested Q35/KVM, four host CPUs, 4 GiB, OVMF, modern VirtIO GPU/network,
headless display; Intel HDA/WAV stereo at 48 kHz. Static SDL2/OSMesa, Mesa
26.2.5 softpipe, GL3.3 compatibility, interval zero, MSAA1/off, the N64-style
320×240 internal preset at 4:3. The GUI follows the actual 1280×768 content
area. No game-performance baseline existed before the port; the public
[LUS slice](lus-native-resources.md) is the functional substrate baseline.

## Startup and ownership

The private bundle declares memory, threads, precise clock, display, keyboard,
pointer, audio, pipe and random authority; no launcher/network. It reads
`app://soh.o2r` and `app://oot.o2r` and requires the exact executable port version.
Writable configuration, logs, mods and saves use `home://soh/` with ordinary
home authority, without a subdirectory-isolation claim. Real stdio/directory
operations replace host filesystem/stream assumptions. Save publication checks
temporary write/flush/file sync/close, rename and native parent-directory sync;
an uncertain outcome is reported without a false rollback claim. Deeper
save/reload qualification belongs to task 6.

The game joins its audio producer and save pool before Context drains its
resource and logger workers and releases GL/SDL/archives. Missing font resources
and game-heap allocation failure report startup failure. A worker audio exception
is transferred to the owner, which closes the loop, cleans up and returns 1;
there is no silent switch to Null or exception unwind through the C game loop.

## Qualification

The final private build renders the LUS/Nintendo startup logos, animated OoT
title sequence and the settings menu, opened with Escape. The original game's
version is recognized. Ordinary pointer input opens and confirms the built-in
Quit dialog; the shell returns without an error. GDB stops at save shutdown
observe the mixer stopped, processing clear and its thread joined before save
and resource teardown. Missing and invalid archives print a specific startup
reason and return status 1. The normal public LUS first-check application also
renders its post-initialization font, passes its resource/FBO checks and exits
through its joined-worker path. No native-hardware or gameplay qualification
is claimed.

A subsequent real nested HDA observation failure stopped the engine; the game
printed the stopped/disconnected audio reason, destroyed the resource manager
and returned status 1 to the shell without a user fault. This is a containment
check, not audio audibility or sustained-audio qualification. Any explicit Null
backend diagnostic is recorded separately from the default SDL run.

## Capacity and frame-rate observations

| Observation | Result |
| --- | --- |
| P1F file | 46,466,146 bytes (44.3 MiB), below the existing 128 MiB captured-image ceiling |
| Page-rounded static span, including BSS | 151,818,240 bytes (144.8 MiB) |
| Main stack / ordinary sibling stack policy | 8 MiB / existing 1 MiB eager guarded default |
| GDB sample, explicit Null backend | Five live tasks, 210,924 backed pages (823.9 MiB) |

The backing sample sums the process VM's owned backed ranges, including its
static image, heaps and stack/TLS. It excludes kernel staging, kernel-owned
shared display mappings, page tables and metadata. It is an instantaneous observation, not peak backing or
stack high-water. No capacity was raised or final native budget settled.

A separate unpaused diagnostic with the existing Null backend and built-in Stats
window showed 11.3 FPS at the Nintendo logo, 5.3 during the opening camera, and
4.6 at the title. These are three displayed rolling values at changing scenes,
not repeated samples of a fixed workload or a before/after benchmark. The GUI
was at 1280×768; game internal rendering stayed 320×240, 4:3. Native performance
and real-SDL sustained performance remain unmeasured. No minimum-FPS gate was
introduced.

Raw sources, recipe, receipts, build/run logs and private images stay in
`/shared/oot/soh-9.3.1-task5/`. The [gallery](../../gallery/README.md) contains an
allowed title screenshot, not game assets or a distributable game bundle.
Keyboard gameplay, audibility/pacing, save/reload and refused/full save targets
remain task 6; native qualification and peak/stack measurements remain later.
The existing nested HDA observation/commit limits were not changed.
