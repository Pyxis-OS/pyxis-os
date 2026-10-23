# Library development-package qualification

2026-10-23. Implemented interfaces/profiles are in the
[library reference](../library-dev-packages.md).

## Revisions and build

Qualification parent `47084049`, containing accepted plan `9e000d43` and main
`d4b38c6f`; userland `c0a2a622`, ports `ee3fceb9` (on ports main `f409955`).
Builder `pyxis-llvm23.1.3-3d69e83`, selected SDK and ordinary full image,
`make -j8 image DISTRIBUTION=full VIRTIO_NET=1`. The host generated the unchanged
Kconfig header with genconfig; the builder lacks that Python module, so its
image invocation used `-o build/kernel-config.h`. No compiler rebuild.

The first image build passed at the earlier ports `3709028`. Restacking onto
new libuv commits exposed their missing terminal header in the old SDK; the
merged userland/main companion fixed that. The final fresh SDK/ports/full image
passed at the revisions above. Later record/reference edits do not change the
qualified runtime inputs.

## Packing and live SDK

The real `development-packages` loop packed the sixteen selected dev recipes
with the prepared full build (`-o userspace -o ports -o build/os-release`),
including the fourteen ports-library packages and the two existing SDK packages.
Host repository checking passed. The twelve additions contain 256 files and
16,156,672 archive bytes; all sixteen archives total 52,567,040 bytes. Inspection
found 2,139 SDK-view files with no duplicate ownership. These are artifact sizes,
not install timing or a full-upstream API claim. All sixteen public library
pkg-config modules name the SDK view; wpa-rsn is absent from the package set.

Q35 nested KVM, four max CPUs, 4 GiB, OVMF, VirtIO GPU/RNG/network, default live
RAM package storage. Clean installation automatically selected pyxis-dev,
libcxx-dev, zlib-dev and sdl2-dev as named dependencies; all sixteen packages
coexisted and native `pkg verify` passed. pkg-config returned static chains for
libpng, fmt, Lua 5.1/LPeg/luv and Mbed TLS, including required subdirectory/config
flags. Both Lua ABIs owned distinct view files.

A local-only second zlib version added a harmless share/ qualification marker:
answering `n` returned cancellation and retained set 1/the original selection.
Confirming installed the new version. Removing zlib alone was refused while
libpng depended on it. Removing both withdrew the headers/archive from the live
view; a fresh hosted compile failed on the missing PNG header while an already
linked compression program still ran. Reinstall restored compilation, execution
and `pkg verify`. The fixture version/marker is not a release recipe.

## Compile, link and run

Existing published SDK-aware Clang/LLD fixture `785717a1...` was fetched from the
same public repository used for raylib, its archive SHA-256 checked against the
index, and its unchanged tool bundles exposed through a read-only virtio-fs
`host://` export. Their declared `sdk` grant selected the current installed set.
No package ABI admission or kernel provided-ABI list was changed. The compiler
reported Clang 23.1.3 / LLVM source `819c2b77`. Cross-toolchain builds provided
an earlier check; the hosted checks below compiled and linked inside Pyxis.

```text
host://clang.pxb host://fixtures/compression.c -lpng -lz -o tmp://compression.pxe
pkg-config --cflags --libs luv lpeg
host://clang.pxb host://fixtures/profile.c -lraylib -lSDL2 -o tmp://profile.pxe
```

No extra include or library search path was used. Module-specific Lua 5.1 and
Mbed TLS flags followed pkg-config; fmt linked its C++ dependency group.
Nine C/C++ programs covered every public archive:

| Consumer | Observed result |
| --- | --- |
| zlib + libpng | Compression round trip and PNG signature/version checks |
| fmt | C++ format result `42:sdk` |
| picohttpparser | HTTP response status/header/body-boundary parsing |
| utf8proc + tree-sitter | UTF-8 decoding and parser create/delete |
| Lua 5.5.1 | Embedded expression returns 42 |
| Lua 5.1 + LPeg + luv | Pattern match and libuv version through independent ABI |
| libuv | Native timer callback and clean loop close |
| Mbed TLS + X.509 + TF-PSA-Crypto | Version, X.509 context lifecycle and full SHA-256 of `abc` |
| raylib + SDL2 | Color conversion and linked SDL version |

The crypto fixture supplied real libc entropy/time/allocation hooks; no TLS
handshake or successful fallback was substituted. Timer/luv fixtures initially
refused their missing PIPE CREATE authority, then passed with explicit manifest
grants. Their final manifests declare memory, clock and pipe; crypto declares
memory, clock and random. Every expected PASS marker and successful command
completion was checked. Rendering/audio retain their separate raylib records.

## Limits and raw records

These are QEMU functional checks, not native or performance evidence. They
qualify the selected operations and package/view integration, not every API in
upstream headers, a new TLS transport, private-library extraction or a new
CMake consumer. Raw commands, fixtures, provenance, package counts and transcripts
remain under `/shared/present/library-dev-sweep/`; no raw dumps are committed.
