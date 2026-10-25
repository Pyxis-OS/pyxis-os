# Native LUS resource slice

Public qualification for SoH task 4; no ROM, extracted game assets or complete
game sources are used. LUS is pinned at
`da6daf1a50a5e46d6641408d52227e31e85e46df`, matching SoH 9.3.1. Shared development
leaves retain their own pins. The [recipe](../../../ports/libultraship/README.md)
records profile, paths, ownership and licences.

## Build and configuration

The unmodified `2446d3b8` image built before adaptation. The final integration
is rebased on `41f1e4fc`, with the shared-SHA SDK/userland and ports changes kept.
Existing builder `pyxis-llvm23.1.3-3d69e83`, `make -j16 image DISTRIBUTION=full`; no compiler fork or
container rebuild. A fresh mirror-only recipe build and a separate CMake project
linking `LibUltraShip::LibUltraShip` qualify the staged headers/archive/closure.
The SDK adds address-only static-image bounds; no syscall or loader shim.

Nested QEMU/KVM Q35, four host CPUs, 4 GiB, OVMF, modern VirtIO GPU/network,
Intel HDA/WAV backend at 48 kHz stereo. Static SDL2/OSMesa uses Mesa 26.2.5
softpipe, desktop GL3.3 compatibility, MSAA off and interval zero. Headless and
GTK-on-Xvfb runs keep the UI at the actual drawable size, scale 1.0. This is
functional qualification, not an FPS or native-performance measurement.

## Results

- A deterministic deflate ZIP/O2R built from literal public JSON and the pinned
  public GL shaders decodes through LUS's real resource factory. Synchronous
  JSON load and an uncached bulk load on one actual resource worker complete.
  A missing resource returns no resource; a missing archive prints its URI and
  exits 1. Removing the `home` capability reports state preparation failure and
  exits 1 instead of using a host/CWD fallback.
- Archive mutation and saving a configuration under read-only `app://` are
  refused; the original configuration remains unchanged. Writable state uses
  `home://lus-slice/`; checked temporary/write/close/rename publication succeeds.
  This does not claim power-loss durability.
- The pinned interpreter and ImGui render together. An independently constructed
  FBO checks real depth ordering by color and depth readback. Runtime GL strings
  identify the selected Mesa compatibility context and softpipe. Keyboard text
  changes the field, a pointer click increments its counter, and GTK output resize
  changes 640×448 content to 1100×791 at scale 1.0, without a fixed canvas.
- Escape ends the UI, joins the real SDL queued-audio producer, then drains and
  joins resource workers before GUI/GL/archive teardown. The asynchronous logger
  owns its pool and restores the prior default logger before joining it.
- GDB hardware breakpoints observed open resource admission at shutdown entry,
  then closed admission, a null resource pool/worker pointer and an empty cache
  after join. Static-image bounds resolve to the actual image through BSS.
  Archive close also observes zero active ZIP borrowers before handle disposal.
  Debugger pauses are not audio-timing evidence.

Raw build logs, GDB transcripts and QEMU captures remain under
`/shared/present/soh/lus-task4/`; the fixture and manual application are public
recipe sources. The upstream host-tools compiler-error report was drafted only
for chronium, not filed.

## Limits

The producer submits silence to exercise a real audio queue; audibility is not
claimed. Extended UI/debugger checks explicitly select the existing Null backend
in their disposable configuration, separately from the real-SDL lifecycle run. A longer nested run hit the existing HDA commit-clock limit and stopped
the engine. LUS now surfaces SDL stopped/disconnected status and exits 1 rather
than silently replacing the requested backend or waiting forever. No kernel
limit was changed. Native audio/gameplay and private archive semantics remain
later tasks.

Fixed stereo is qualified; stop/join the producer before backend/channel changes
or Context destruction. Context destruction is owner-only, outside resource
workers, without new concurrent submissions. Folder archives, target extraction,
file pickers, MPQ, dynamic modules, gamepad discovery and network features are
outside this profile. Peak memory and a game-size workload are unmeasured here.
