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

## Fork-built compiler and built-in target

Gate 1 of the [std milestone](../wip/rust-std.md) supplies a separate Linux-host
compiler with built-in `x86_64-unknown-pyxis`, installed target core+alloc and
host std/proc_macro for build scripts. It supplies no Pyxis std yet. The
[qualification record](experiments/rust-std-toolchain.md) identifies the source,
binaries and QEMU checks. The prebuilt stage-1 profile above remains available.

[fork-toolchain.json](../../toolchain/rust/fork-toolchain.json) pins the base
Rust revision, CI LLVM, the three upstream stage0 beta archives, backtrace source
and later libc baseline. All fetches use its internal cache URLs and verify
whole-file SHA-256 values. Preparation is offline by default and runs no downloaded
code. It checks the source's stage0 metadata, backtrace gitlink and actual HEAD's
upstream LLVM freshness selection before writing bootstrap's ordinary cache layout:

```sh
toolchain/prepare-rust-fork.py /absolute/pyxis-rust /absolute/rust-bootstrap-cache --fetch
toolchain/prepare-rust-fork.py /absolute/pyxis-rust /absolute/rust-bootstrap-cache
```

The source checkout needs sufficient upstream Git history. Materialize
`library/backtrace` from the verified archive at its recorded cache path,
stripping that archive's single root directory; retain its MIT/Apache notices.
No automatic upstream submodule fetch is enabled. The CI LLVM archive retains
its Apache-2.0 WITH LLVM-exception notices and Rust's bundled distribution notices.
These tools add no dependency to ordinary image builds or the compiler container.

Copy [the bootstrap configuration](../../toolchain/rust/fork-bootstrap.example.toml)
into the fork as `bootstrap.toml`, replacing the absolute cache and compiler paths.
Use a private Cargo home with the closed proxy configuration already used by the
sample workspace (`toolchain/rust/samples/.cargo/config.toml`). Before the offline
build, seed its locked bootstrap, compiler and library graphs with the pinned
stage-1 Cargo and rustc; each command runs from the fork checkout:

```sh
export CARGO_HOME=/absolute/fork-cargo-home
export RUSTC=/absolute/prebuilt-prefix/bin/rustc
/absolute/prebuilt-prefix/bin/cargo fetch --locked --manifest-path src/bootstrap/Cargo.toml
/absolute/prebuilt-prefix/bin/cargo fetch --locked --manifest-path Cargo.toml
/absolute/prebuilt-prefix/bin/cargo fetch --locked --manifest-path library/Cargo.toml
unset RUSTC
```

Use the native host C/C++ compiler for rustc; use the existing Pyxis compiler for
target native objects and final links. The configuration uses Rust's CI LLVM,
`download-rustc=false`, full bootstrap, no LTO, and no embedded target bitcode.
On Linux with unprivileged user/network namespaces, the qualified build commands
below deny all network access rather than relying on a downloader's fallback:

```sh
export CARGO_NET_OFFLINE=true
unshare --user --map-root-user --net python3 x.py build --stage 2 compiler/rustc
unshare --user --map-root-user --net python3 x.py test --stage 2 \
  compiler/rustc_target --test-args x86_64_unknown_pyxis
unshare --user --map-root-user --net python3 x.py build --stage 2 \
  --target x86_64-unknown-linux-gnu,x86_64-unknown-pyxis library
```

Unset inherited `RUSTC_BOOTSTRAP`, `RUSTFLAGS` and `CARGO_ENCODED_RUSTFLAGS` before
these commands. Upstream bootstrap manages its own stage flags; do not use a
user `RUSTC_BOOTSTRAP` workaround. Build both library targets in the final command:
bootstrap can recreate the compiler sysroot between invocations. `no-std=true`
selects core+alloc only for Pyxis; Linux receives std and proc_macro for host tools.

Export outside the source/input trees into a fresh prefix, then build the same
samples with the built-in target:

```sh
toolchain/export-rust-fork.py /absolute/pyxis-rust /absolute/fork-prefix \
  /absolute/prebuilt-prefix
toolchain/build-rust-samples.py /absolute/fork-prefix /absolute/sdk \
  /absolute/fork-samples /absolute/fork-work --fork --fetch
```

Omit `--fetch` after seeding the sample graph for a frozen build. Export verifies
the clean source/compiler identity and required libraries, materializes the
compiler libraries while excluding bootstrap's two recursive source links,
copies library source once, and installs unchanged Cargo from its checked
stage-1 archive. `share/pyxis-rust/fork-installation.json` records compiler/Cargo
identities and file hashes; the sample builder verifies them and the SDK inputs.
A prefix built against one SDK does not grant compatibility with another.

Both profiles keep Pyxis Clang/LLD as the final linker. The built-in target
matches the stage-1 JSON's ABI/code-generation settings; only tier metadata
changes. `--fork` uses installed core+alloc, without a JSON-target or build-std
flag. Standard operations, native libc bindings and hexyl remain later gates;
build success is not a claim that those APIs work.
