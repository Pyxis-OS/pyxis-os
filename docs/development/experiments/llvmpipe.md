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
