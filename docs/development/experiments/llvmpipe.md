# llvmpipe qualification

## Attended vela check, 2026-10-27

Chronium ran the private live-RAM packet from parent
`d99630dc3dfaf899b22d05ef8802865f8ced9a40`, with compiler/container LLVM
`2b3a3a41df746cc0ee3d4bfccc5dba495d232f0e`. Development had JIT authority;
`GALLIUM_DRIVER=llvmpipe` was explicit and `LP_NUM_THREADS` unset, preserving
Mesa's default worker selection. Kernel SHA-256:
`12b1752d896abece8a7f4b68938d627d87435e37c10175092ddd4a4d6754fcbb`.
The packet and checksums remain in
`/shared/present/llvmpipe/vela-attended-20261027/`.

Chronium reported OpenArena above 60 fps and SoH at 45–60 fps at 1080p
(he selected a 40 fps cap), responsive DualSense input and working saves.
He described SoH as fully playable. These are attended observations, not timedemo
samples or an NVMe persistence check: this packet used RAM-backed home storage.
Tearing was visible on vela. No display-driver change is included here.

OpenArena used the public 0.8.8 assets in an explicit qualification bundle;
SoH used the previously staged private assets and a same-compiler relink.
Neither is a package-install check. The later merged script-authority change
(#1542, userland #416) preserves the shell's held launch sources, including JIT,
for `.pxs` scripts. The llvmpipe stack includes that change.

The matched trimmed OpenArena timedemo, once on Pyxis and once on Fedora on
vela with upstream default workers, remains pending. The refreshed container
uses LLVM `8bdf1360b8c47640c92eeeb515ee4a08814987e6` for both compiler and
libraries, retaining the JIT adapter and merged append fixes; last night's
native observations do not qualify that new container.

## Trimmed demo playback, 2026-10-28

A prefix of 1,800 whole messages from OpenArena 0.8.8's shipped
`demo088-test1.dm_71`, followed by its ordinary `(-1,-1)` end marker, played
and quit normally once in four-vCPU nested KVM (`host,+invtsc`, 4 GiB,
1280×800 VirtIO display). The source SHA-256 is
`a939cef30d41e78d552785cfd6f0f9f6a42defe05fdfbf6dd84bd943b49c718b`;
the trimmed file is
`c02c5a071f520939a33dc00ffd05c8e4f7b9c190b85e9289203cab685c1d8d9f`.
It retains an unchanged 599,193-byte prefix plus the 8-byte trailer.

The check used the coherent d996/2b image above, default Mesa workers,
`r_mode 0` (320×240), `timedemo 1`, `nextdemo quit`, and `demo bench`.
The renderer reported `llvmpipe (LLVM 23.1.3, 256 bits)`; playback completed
1,803 timed frames in 18.3 s. This single nested playback check establishes
demo/quit correctness, not the new 8b-container build or native throughput.
The native pair will use identical demo bytes and default workers; actual
wall time is machine-dependent. Evidence:
`/shared/present/llvmpipe/timedemo/`.

## Published-container qualification, 2026-10-28

The rebuilt stack uses compiler container
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-8bdf136`, image
`baeaa0e204f54a6a90ade6cd57e88476a170641b33f47cd57df90162ed114cee`.
Compiler, SDK compiler provenance and Mesa's copied target-library metadata
all identify LLVM `8bdf1360b8c47640c92eeeb515ee4a08814987e6`. Target libraries
have threads enabled and RTTI/EH disabled. Their bootstrap SDK is published
Pyxis `a18140067e5fb97a2bd15625d8c5c061f9c1f23e`, including native append.
Ordinary image builds consumed the container prefix without building LLVM.

Parent `1adcae9272c68d546143c485000f608e692bc7e6` included main's script
launch-source fix, userland `2a63b32de4109f6ba0952b240544e718130e569c`
and ports `bf08f5da3c955098d8eddf6b76dff7f20275c31e`. Clean compiler/SDK/Mesa,
SDL, ImGui and ioquake3 builds passed; both core and full live archives assembled.
The final ports follow-up only replaces ImGui's stale hardcoded softpipe label
with renderer-neutral wording; its stderr already reports the actual renderer.

Four-vCPU Q35 nested KVM, `host,+invtsc`, 4 GiB, VirtIO 1280×800 display,
RAM roots: nextafterf boundary checks, llvmpipe default/zero/four-worker triangle,
softpipe without a declared JIT grant, explicit missing-JIT refusal, and `.pxs`
JIT inheritance all passed. These successful triangle files share SHA-256
`21bb67e3254d075dc512ef1308e4ea2e1c6bfe7b9e8363ccbdc6cb62226d47b2`.
With AVX and AVX2 hidden, llvmpipe reported 128-bit vectors and the same checks
passed; ordinary host exposure reported 256-bit vectors. ImGui rendered and
reported `llvmpipe (LLVM 23.1.3, 256 bits)`.

The same 1,800-message demo ran once per worker setting through a `.pxs`
launcher and quit normally, with no LLVM or renderer source changes:

| Workers | Frames | Seconds | FPS | Minimum / mean / maximum / standard deviation, ms |
| --- | ---: | ---: | ---: | --- |
| Mesa default | 1,803 | 17.0 | 106.3 | 4.0 / 9.4 / 555.0 / 8.5 |
| `LP_NUM_THREADS=0` | 1,803 | 25.7 | 70.1 | 6.0 / 14.3 / 566.0 / 8.5 |
| `LP_NUM_THREADS=4` | 1,803 | 16.5 | 109.1 | 4.0 / 9.2 / 540.0 / 8.4 |

Each sample used opengl1, 320×240 and a fresh game process, with no disk shader
cache. These single shared-host nested samples are not a controlled scaling
study or a native comparison. An uppercase-key encoding error in the host QMP
helper interrupted setup after the default sample; zero/four resumed in a fresh
guest with corrected key encoding. No successful sample was repeated. The
native Pyxis/Fedora pair remains scheduled separately, both using default workers.
Small evidence is under `/shared/present/llvmpipe/qualification-20261028/`;
build trees and test images remain VM-local.
