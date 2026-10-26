# Rust cross-toolchains

The original prebuilt profile uses pinned `nightly-2026-10-24`, the native userspace
SDK and Pyxis Clang/LLD. It supplies `core` + `alloc`, not `std`, a hosted Rust
compiler or Unix compatibility. [Qualification](experiments/rust-core-alloc.md)
records the hello, local-exec TLS and unchanged tiny-skia checksum checks.
The [fork-built profile](#fork-built-compiler-and-built-in-target) below adds
native std main and a read-only hexyl profile. Its
[milestone](../wip/rust-std.md) keeps worker, filesystem and first-port delivery
separate.

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

The [std milestone](../wip/rust-std.md) uses a separate Linux-host compiler
with built-in `x86_64-unknown-pyxis`, installed Pyxis std/core/alloc and host
std/proc_macro for build scripts. Gate 2's compiler revision is
`5e85f86de5f7dc45a485fa8a3eb376487923bc0b`; hexyl adds the read-only File
backend at `a5c9a1815816cddb77f5c58238adf549e618b3a4`. Managed workers
are added at `f08e6f816abf6396aa4547fe11ff0f214a951384`; native writable files
and authority metadata use `7bb492df8d26d97a2bc37c7b225c29cd6626cf00`.
The target keeps `target_os="pyxis"`
and has no Unix family. The [compiler record](experiments/rust-std-toolchain.md) and
[first std main record](experiments/rust-std-main.md) separate their qualification
and limits.
The prebuilt stage-1 profile above remains separate and unchanged.

Native libc declarations come from [libc #1](https://git.internal/PyxisOS/pyxis-libc/pulls/1)
at `a6aac31c87316e4a351d8e7185ad8898e545d22a`, with read-only file
bindings added by [libc #2](https://git.internal/PyxisOS/pyxis-libc/pulls/2)
at `417acae00d4e21abf1a668c8dc71361bfa03d3aa`. Managed-worker declarations
come from [libc #3](https://git.internal/PyxisOS/pyxis-libc/pulls/3) at
`bdaad80e28d7dc793f034460787c94dea622f588`; native file helpers use
[libc #4](https://git.internal/PyxisOS/pyxis-libc/pulls/4) at
`94498eae9e79ef076eded365fcd9625f17f63377`, overlaid through exact HTTPS
`[patch.crates-io]` entries in the Rust compiler workspace, library workspace
and sample workspace, with their matching lockfiles. Keep this overlay in
consumer workspaces that import libc; it does not change the closed crates.io proxy. The SDK
uses [userland #384](https://git.internal/PyxisOS/pyxis-userland/pulls/384) at
`6cd1cf577ca46faf031fa7ba2a9a67bae56eabff` for owned all-TEXT capture and the
end-of-TLS-drain cleanup hook. Rebuild the SDK and affected Rust libraries and
programs when these native interfaces change; libc still owns startup and FS. Native writable files and coherent authority
metadata require [userland #388](https://git.internal/PyxisOS/pyxis-userland/pulls/388)
at `9acda3e487af9bdaaed845f02ea4f26ff33e6a78`.

[fork-toolchain.json](../../toolchain/rust/fork-toolchain.json) pins the base
Rust revision, CI LLVM, the three upstream stage0 beta archives, backtrace source
and the native libc revision. Archive fetches use its internal cache URLs and
verify whole-file SHA-256 values. Preparation is offline by default and runs no
downloaded code. It checks the source's stage0 metadata, backtrace gitlink and actual HEAD's
upstream LLVM freshness selection before writing bootstrap's ordinary cache layout:

```sh
toolchain/prepare-rust-fork.py /absolute/pyxis-rust /absolute/rust-bootstrap-cache --fetch
toolchain/prepare-rust-fork.py /absolute/pyxis-rust /absolute/rust-bootstrap-cache
```

On horse, use HTTPS for Git clone/fetch, including the pinned libc overlay:

```sh
git clone https://git.internal/PyxisOS/pyxis-rust.git /absolute/pyxis-rust
git -C /absolute/pyxis-rust fetch origin
git -C /absolute/pyxis-rust checkout --detach 7bb492df8d26d97a2bc37c7b225c29cd6626cf00
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
Pyxis now requires `no-std=false`; the former `true` builds core+alloc only and
cannot supply the first std main.
On Linux with unprivileged user/network namespaces, the qualified build commands
below deny all network access rather than relying on a downloader's fallback:

```sh
export CARGO_NET_OFFLINE=true
unshare --user --map-root-user --net python3 x.py build --stage 2 compiler/rustc
unshare --user --map-root-user --net python3 x.py test --stage 2 \
  compiler/rustc_target --test-args x86_64_unknown_pyxis
unshare --user --map-root-user --net python3 x.py build --stage 2 \
  --set rust.deny-warnings=false \
  --target x86_64-unknown-linux-gnu,x86_64-unknown-pyxis library
```

Unset inherited `RUSTC_BOOTSTRAP`, `RUSTFLAGS` and `CARGO_ENCODED_RUSTFLAGS` before
these commands. Upstream bootstrap manages its own stage flags; do not use a
user `RUSTC_BOOTSTRAP` workaround. Build both library targets in the final command:
bootstrap can recreate the compiler sysroot between invocations. Do not use
`--keep-stage` to skip the final library build before export: both host and Pyxis
libraries must be built for the actual delivered compiler revision.

Upstream std's Cargo manifest offers both `dylib` and `rlib`, while Pyxis is
static-only. Rustc warns that it drops the unsupported dylib crate type. The
example configuration and explicit `--set rust.deny-warnings=false` allow that
known warning without turning it into a bootstrap failure. Warnings remain
visible; inspect and resolve any other warning rather than adding warning
suppression flags or treating unknown warnings as this exception.

Export outside the source/input trees into a fresh prefix, then select the
core+alloc samples or the first std main with the built-in target:

```sh
toolchain/export-rust-fork.py /absolute/pyxis-rust /absolute/fork-prefix \
  /absolute/prebuilt-prefix
toolchain/build-rust-samples.py /absolute/fork-prefix /absolute/sdk \
  /absolute/fork-samples /absolute/fork-work --fork --fetch
toolchain/build-rust-samples.py /absolute/fork-prefix /absolute/sdk \
  /absolute/fork-std-sample /absolute/fork-work --fork --std --fetch
```

Omit `--fetch` after seeding the sample graph for a frozen build. Export verifies
the clean source/compiler identity, host std/proc_macro and Pyxis
std/core/alloc/compiler-builtins; a core+alloc-only fork sysroot is no longer
exportable with this tool. It materializes the compiler libraries while excluding
bootstrap's two recursive source links, copies library source once, and installs unchanged Cargo from its checked
stage-1 archive. `share/pyxis-rust/fork-installation.json` records compiler/Cargo
identities and hashes for the compiler, Cargo, native libraries and metadata.
The exported canonical SDK linker adapter is under `share/pyxis-rust` and is
hashed too. The sample builder verifies the receipt and SDK inputs.
A prefix built against one SDK does not grant compatibility with another.

Both profiles keep Pyxis Clang/LLD as the final linker. The built-in target
retains the stage-1 JSON's ABI/code-generation settings. `--fork` uses installed
libraries without a JSON-target or build-std flag. `--fork --std` builds only
`rust-std-main.pxb` and its debug ELF/map, skipping the raster host reference.
The bundle preserves source, the resolved libc fork and notices, lockfiles and
compiler/SDK provenance through the existing packaging path.

`rust-std-main` enters through ordinary `fn main() -> ExitCode`, using std's
allocator and native adapters. Its default exercise covers owned args and
TEXT snapshots, private TEXT and CAP exclusion, over-aligned allocation,
byte-preserving native paths, randomized collections, clocks/sleep and an
initial-thread TLS destructor marker. Separate `--clock-only`, `--entropy-only`,
`--io`, `--panic` and `--allocation-failure` modes isolate grant, stream and
failure checks. Its manifest requests memory/manage, clock/read+precise+sleep
and random/read; streams remain caller-supplied. It requests no thread or network
authority; its read-only file regression uses its own bundle license through the
existing `app://` grant. Missing grants must produce real failure;
linking std adds no ambient authority.

The read-only profile additionally supports native File open/read/seek/clone,
regular-file length and reported metadata. The worker-enabled prefix also supplies
managed spawn/join/detach, scoped threads, channels and contended synchronization
through existing libc threads and native parking. See the
[worker record](experiments/rust-std-workers.md). Request `threads/manage` and
`memory/manage` for creation, plus clock rights for timed operations. Default
worker stacks are 1 MiB, explicit requests round up within the 8 MiB ceiling,
and real record/backing failures remain errors. Panic aborts the process; join
workers before orderly main return. `yield_now` is a weak CPU hint without a
forced reschedule or fairness promise, and `available_parallelism` remains
Unsupported. The native writable-file extension adds create/exclusive/truncate,
read/write, resize and backend sync. `File::metadata` reports held rights;
`fs::metadata(path)` reports authority obtainable through the captured path.
Files use WRITE, directories use CREATE or REMOVE. Unknown provider facts,
append and writable provider routes return Unsupported; no write request is
prepared while querying permissions. `Permissions::set_readonly` changes only
the local value; applying permissions remains Unsupported. Directory enumeration
and mutation, child processes and network operations remain later work; hexyl
delivery does not complete the std milestone.

## Optional hexyl package

Use the read-only-file compiler prefix above, not gate 2's earlier stdio-only
prefix. From a clean checkout with Pyxis Clang/LLD on PATH:

```sh
make hexyl-package RUST_PREFIX=/absolute/fork-prefix REPOSITORY=/absolute/repository
python3 tools/pxpkg.py check /absolute/repository
```

The target builds the SDK, invokes the explicit ports recipe with the supplied
Rust prefix, then packs and publishes the optional package using the existing
repository signing controls. It never bootstraps Rust or adds hexyl to the base,
rescue archive, default port aggregate or ordinary image CI. A source/SDK change
still requires rebuilding its affected libraries and program. Rust is held out
of the 0.0.9 release; this optional profile is separate from those release artifacts.

The recipe pins v0.17.0, vendors its locked graph through the closed Cargo proxy,
then builds frozen/offline with panic-abort, LTO and embedded bitcode off. It
consumes the canonical adapter in the prefix. The package retains the original
and patched locks, two platform patches, full vendored source/notices and the
Rust/SDK provenance. A consumer needs a libc `[patch.crates-io]` entry only if
its own dependency graph imports libc; hexyl's sole direct libc use was removed,
while the installed std already uses the pinned fork bindings.

Install from the configured repository, then use the published bare command:

```sh
pkg install hexyl
hexyl --character-table=ascii --color=auto home://example.bin
cat home://example.bin | hexyl --character-table=ascii --color=never
```

`ascii` selects the byte panel; borders are still UTF-8. Upstream defaults to
colour Always; Auto uses the supplied stdout, and NO_COLOR suppresses colour
unless Force is selected. Width queries use supplied stdout, stderr, then stdin
and retain the upstream 80-column fallback. Files use existing delegated roots;
linking Rust grants no filesystem access. The
[hexyl record](experiments/rust-hexyl.md) separates actual recipe/package/QEMU
qualification from the unrun native and complete-profile work.

The port retains general upstream I/O behavior: persistent read errors can loop,
and include-mode final buffer-drop errors can be unreported. See the
[recorded limits](../technical-debt.md#hexyl-upstream-io-errors).
