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
integration rebases onto `4c5ed5f4` (intervening documentation/LuaLS changes,
no kernel or ABI-header change). Existing builder
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

## Qualification status

The first successful private run rendered the LUS startup logo, animated OoT
title sequence and the settings menu, opened with Escape. The original game's
version is recognized. No native-hardware or gameplay qualification is claimed.
Final-head quit, capacity observations and failure cases are being completed.

A subsequent real nested HDA observation failure stopped the engine; the game
printed the stopped/disconnected audio reason, destroyed the resource manager
and returned status 1 to the shell without a user fault. This is a containment
check, not audio audibility or sustained-audio qualification. Any explicit Null
backend diagnostic is recorded separately from the default SDL run.

Raw sources, recipe, build/run logs and private images stay in
`/shared/oot/soh-9.3.1-task5/`. No capacities were raised. True peak memory,
stack high-water, native speed/audio and gameplay remain unclaimed.
