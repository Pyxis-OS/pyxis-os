# Rust core and alloc cross-toolchain

The opt-in Rust profile uses pinned `nightly-2026-10-24`, the native userspace
SDK and Pyxis Clang/LLD. It supplies `core` + `alloc`, not `std`, a hosted Rust
compiler or Unix compatibility. [Qualification](experiments/rust-core-alloc.md)
records the hello, local-exec TLS and unchanged tiny-skia checksum checks.
The [Rust plan](../wip/rust.md) keeps the accepted later std-fork direction.

## Host prefix and build

Use Linux x86-64, Python 3.12 or newer, Make, the existing Pyxis compiler and a
fresh output/prefix. Ordinary kernel, SDK, ports and image builds do not install
Rust. Chronium need not rebuild the compiler container for this profile.

```sh
export RUST_PREFIX=/absolute/pyxis-rust-nightly-2026-10-24
toolchain/install-rust.py "$RUST_PREFIX" "$PWD/build/rust-host"
make -j16 sdk
toolchain/build-rust-samples.py "$RUST_PREFIX" "$PWD/build/sdk" \
  "$PWD/build/rust-samples" "$PWD/build/rust-work" --fetch
```

Run the SDK/sample commands where the existing Pyxis Clang is on PATH, including
inside its unchanged builder container; expose the prefix at the same absolute
path. Rust's Linux host binaries remain separate from target SDK libraries.
The installer reads [the component pin](../../toolchain/rust/host-toolchain.json),
fetches only through `repo.internal/repository/raw-rust/<date>/`, checks all four
archive hashes before installation, and preserves source/notices/provenance.
It refuses an existing prefix and never selects a global rustup toolchain.

The initial `--fetch` seeds the closed Cargo proxy's locked graph, including
rust-src build inputs. Without it, the builder uses `--frozen` and performs no
network access. OUTPUT must be fresh; WORK is reusable. Choose another output
or remove only the generated output before rebuilding. An SDK/compiler/link
configuration change selects a new target directory automatically. The cache
key covers actual compiler/runtime bytes and link settings; a compiler runtime
that differs from the SDK copy is refused. Cargo tracks Rust source changes.
The builder removes inherited global Rust/profile flags and selects the Linux
host target explicitly for the reference, keeping Pyxis link flags off host code.

Outputs are `rust-hello.pxb`, `rust-raster.pxb`, debug ELFs/maps and provenance.
The host reference prints:

```text
rust-raster: fnv1a64=d0dfb9f8f80d6c59 width=96 height=64
```

The shared scene uses tiny-skia 0.12.0 unchanged, `default-features=false` and
`no-std-float`; std, PNG and the crate's SIMD feature are off. The lockfile,
checked registry hashes, all nine dependencies' notices and source receipts
stay beside the sample workspace. There is no crate patch overlay for this graph.

## Target and startup contract

The [JSON target](../../toolchain/rust/x86_64-unknown-pyxis.json) identifies
`target_os="pyxis"` without `unix` or `windows`. It uses x86-64 SysV C ABI/SSE2,
small code model, static non-PIE relocation, no red zone, inline stack probes
and executable local-exec TLS. CPU-specific AVX/FSGSBASE behavior is not assumed.
Its settings transfer to the later built-in target; they are not copied from
Rust's kernel/soft-float bare-metal profile.

The builder invokes the exact prefix's Cargo with `-Zjson-target-spec` and
`-Zbuild-std=core,alloc`. Rustc is commit
`48f4b05538b8eca412137042d9647a61e692c6b3`, bundled LLVM 23.1.4; Pyxis uses
LLVM 23.1.3. Both emit native ELF objects. LTO and embedded bitcode are off.
The [link adapter](../../toolchain/rust/pyxis-link.py) reads the SDK's ordinary
`share/pyxis.mk`: startup, linker script, flags and archive group. The selected
Pyxis Clang/LLD writes P1F directly, with debug ELF/map sidecars made from the
same object inputs. No converter, Linux startup or bundled rust-lld is used.

Samples export C-ABI main; libc owns `_start`, binding/TLS/stdio/heap startup
and FS. A real initialized Rust thread-local is checked on the initial thread.
The sample allocator wraps malloc/free with checked over-allocation and a saved
base pointer, preserving alignments above libc aligned_alloc's 4 KiB limit.
Zeroing and grow/shrink reallocation retain their Rust semantics.

`rust-hello` checks Vec/String growth/drop, 16 KiB-aligned allocations and a
C f64 `fma` call. `--panic` writes a fixed stderr diagnostic and exits 101;
`--allocation-failure` makes a real oversized request and exits 102. The latter
keeps the returned pointer observable so allocation/free elision cannot turn
it into a false success. Failure handlers use `_Exit`, without Rust unwinding.
The small runtime is qualification support, not a general Rust platform library.

## Packaging and authority

Both manifests request only `memory/manage`; streams are supplied by the caller.
There is no thread/network grant or stack override. The normal 1 MiB guarded
initial stack suffices for these checks; no growth is added.

From a committed checkout, build the samples above into `build/rust-samples`,
then create an ordinary optional development repository:

```sh
make build/os-release
python3 tools/pxpkg.py pack --build build packages/rust-samples.lua /absolute/repository
python3 tools/pxpkg.py publish --repository rustlab /absolute/repository
python3 tools/pxpkg.py check /absolute/repository
```

This is unsigned local publication, not a public upload. Use the existing
[package signing workflow](../interfaces/packages.md) for public publication.
The package exposes bare commands `rust-hello` and `rust-raster`; core/rescue
archives and the default package set do not change. Each bundle carries the
Rust library/compiler-builtins and dependency notices, SDK notices, source,
lockfiles and exact toolchain/SDK provenance. Rust's full compiler distribution
remains in the host prefix, outside the target package.

For another core+alloc program, retain the sample's panic-abort profile and
libc startup contract, use the same target/link adapter and declare actual
needed grants. Standard-library crates cannot be made supported by setting
`cfg(unix)` or adding a successful stub. Paths, environment, threads, native
network/process adapters and Rust unwinding remain later std work.
