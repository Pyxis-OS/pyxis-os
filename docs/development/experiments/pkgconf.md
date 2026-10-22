# pkgconf native path qualification

2026-10-22, parent based on `87bbde00`, userland based on `909faaf4`, ports on
`ec13a464`. pkgconf 3.0.10 is pinned by SHA-256
`11818a25f2730132f3b258ac8be4e82f83b9db03a1a99a6c29cd01b94fc9d310`, fetched
only from the internal raw-github cache. Existing compiler image `3d69e83`.
No LLVM/container change.

The first SDK cross-build found missing `langinfo.h`, `S_ISLNK` and `readlink`.
Libc now declares only `nl_langinfo(CODESET)` and reports its existing UTF-8
encoding; unknown items return an empty string. The port excludes upstream's
symlink-following branch because native filesystems have no symlinks. There is
no replacement symlink operation. All CLI/library sources then link normally.

Ordinary full image build passes. Nested QEMU q35/KVM, host CPU, four vCPUs,
4 GiB, OVMF, VirtIO network/RNG and a private virtio-fs export; live RAM pool,
no native disk. A private boot configuration binds the export read-only as
`sdk://` with the decided include/lib layout. This qualifies the port against
that layout, **not** the package-backed SDK view, which is task 2c.

| Command/check | Observed result |
| --- | --- |
| `pkgconf --cflags --libs raylib` | `-Isdk://include/SDL2 -D_REENTRANT -lraylib -lSDL2` |
| `pkg-config --modversion sdl2` | `2.32.10`; alias has identical executable bytes |
| `pkgconf --variable=pc_path pkg-config` | `sdk://lib/pkgconfig` |
| `pkgconf --variable=prefix raylib` | `sdk://` |
| Explicit `sdk://lib/pkgconfig/raylib.pc` | Same flags as name lookup |
| `PKG_CONFIG_PATH=host://extra-one:host://extra-two` | Both directories searched; explicit path overrides SDK |
| Custom module with `Requires: zlib` and `Libs.private` | `-lcustom -lprivate -lz`, without `--static`; external include/lib paths preserved |
| `PKG_CONFIG_PATH=home://`, metadata copied into RAM | Custom module found at the root |
| Combined `raylib zlib luv fmt` | Dependency link order retained, including the C++ runtime group |
| Every staged module `--modversion`, `--list-all` | All 19 modules readable, no host build prefix |
| `unset sdk`, then raylib query | Diagnostic and status 1; no implicit authority |
| Missing module, normal/silenced errors | Status 1 in both cases; silence suppresses pkgconf's text |

The native callback is reached under GDB: `nl_langinfo(item=1)` returns
`"UTF-8"` during flag rendering. Its ELF is linked from the same objects with
`--oformat=elf`; the executable remains P1F. A raylib consumer cross-links using
the emitted flags, mapping SDK URI paths to the fixture on the host and supplying
the compiler's normal implicit include/library search and runtime group.
This is a link check, not a native hosted-compiler or graphics run.

The new leaf has no prior runtime workload to compare and changes no always-on
kernel path. No performance or native-hardware claim. No package-backed view or
installed `-dev` package qualification until task 2c. Raw source/build/debugger
and Remote records remain at `/shared/present/pkgconf/`; initial client/path and
cross-link fixture setup failures are not qualification passes.
