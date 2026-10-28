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
Neither is a package-install check. Games were launched directly as bundles:
the convenience `.pxs` launchers did not receive JIT authority. That launcher
design remains separate; this check adds no authority workaround.

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
