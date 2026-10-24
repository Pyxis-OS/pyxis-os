# Native ImGui slice (2026-10-24)

Initial qualification base `e2fa2694`, userland `f3fa4b5a`, ports base `a3bfc58a`, ImGui
`4806a1924ff6181180bf5e4b8b79ab4394118875` (1.91.9b-docking), SDL 2.32.10,
Mesa 26.2.5. Existing `pyxis-llvm23.1.3-3d69e83` builder; host Meson/Ninja/Mako
prerequisites installed in the disposable container, no compiler rebuild.

Baseline captured before port edits: ordinary `make -j16 image DISTRIBUTION=full`
passed at `a9b9b0d9` with userland `7eebc915`. Compiling the unmodified core and
built-in backends against that SDK failed at `sys/wait.h` (default shell helper)
and `dlfcn.h` (default GL loader). Thus there is no runnable ImGui timing baseline.
The subsequent main changes concern Git; final qualification uses the base/pin
above. No kernel, libc or existing SDL behavior is changed by this slice.

Native-profile compilation and ordinary full image pass. Core and separate
backend prefixes expose pkg-config metadata and relocatable CMake targets.
An independent CMake application linked using `ImGui::SDL2OpenGL3` with separate
prefixes in `CMAKE_FIND_ROOT_PATH`; it did not require manual include/link flags
beyond its own GL prototypes definition. MIT/font/stb and CC-BY range notices,
profile provenance and source accompany the archives.

Interactive nested QEMU: Q35/KVM, host CPU, four CPUs, 4 GiB, modern VirtIO
GPU/net/RNG, 1280×800 output. The private qualification archive added only the
asset-free demo; the shipping image/package policy does not embed it. GL 3.3
compatibility, static Mesa softpipe, MSAA off, swap interval zero. Real text
entry, pointer button activation and keyboard activation worked; count advanced
from zero to two. Context-local cut/paste preserved the edited text. Logical
canvas resized 960×640 → 640×480 → 960×640 without a crash or corrupted frame.
The upstream demo window also rendered. Escape returned to the prompt.

GDB hardware breakpoint at `ImGui_ImplSDL2_NewFrame` (no injected calls) showed
matching IO/canvas sizes at both resolutions, rendered vertices, one-window
platform flags and keyboard/docking enabled. A Remote launch lacking DISPLAY
failed visibly with `Missing required resource display`, without starting a
second renderer. This is not native-machine or SoH qualification, a comprehensive
ImGui API check, a frame-rate comparison, or a system-clipboard claim.

Raw build/interaction/GDB records and private QEMU configuration:
`/shared/present/soh/imgui-task1/`. The gallery keeps an actual framebuffer.
Final package check at `088edde3`: packed all eight named core/backend/runtime
and demo development/program packages, checked the repository, installed from
an unsigned loopback repository in a matching fresh guest, and launched the
installed bare `imgui-demo` command successfully. Native `pkg-config --cflags
--libs imgui-sdl2-opengl3` produced the two backend archives, core, SDL2-osmesa,
OSMesa and C++/C runtime in dependency order, without ordinary SDL2. The final
staged demo and the interacted-with binary have identical SHA-256. Rebased onto
`b14eafb0` / userland `c0c7c159` / ports `029a712` to retain OpenArena integration;
ImGui profile and program files are byte-identical after this rebase.

No ROM or derived game assets were accessed by this task.
