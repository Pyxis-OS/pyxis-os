# Rust on Pyxis

Stage 1 tasks 1–2 are implemented; the [toolchain reference](../development/rust-toolchain.md)
and [qualification record](../development/experiments/rust-core-alloc.md) describe
core+alloc, the allocating hello and unchanged tiny-skia checksum. This is not
std, a hosted compiler or Horizon support. The separate
[stage-2 std milestone](rust-std.md) has accepted decisions and implementation
authorization (chronium, 2026-10-25).

## Accepted decisions (chronium, 2026-10-25)

1. Pin nightly-2026-10-24, rustc `48f4b05538b8eca412137042d9647a61e692c6b3`,
   with matching rust-src and a hash-checked separate host prefix. No
   RUSTC_BOOTSTRAP, rolling nightly or cross-version LTO/bitcode; use Pyxis
   Clang/LLD. No compiler-container rebuild for this profile.
2. Static no_std, panic-abort, libc startup and a malloc-backed GlobalAlloc with
   correct over-alignment and visible failure; hello then unmodified tiny-skia
   0.12.0 with no-std-float, checksum compared with the host.
3. x86_64-unknown-pyxis, target_os=pyxis, no Unix family; dedicated std later
   with small honest crate patches. Tier-3 upstreaming has chronium as maintainer.

The [component manifest](../../toolchain/rust/host-toolchain.json) is the pin
and checksum source of truth; the [JSON target](../../toolchain/rust/x86_64-unknown-pyxis.json)
is the target-settings source of truth. Rust's bundled LLVM is 23.1.4 versus
Pyxis 23.1.3; ordinary ELF object linking with bitcode/LTO off is qualified.
The caches are `repo.internal/repository/raw-rust/<date>/<file>` (no extra
`dist/`) and the closed `repo.internal/repository/cargo-proxy/` sparse index
and crate archive route. No direct-Internet build fallback or changed same-name
registry is permitted. Later crate changes use pinned `[patch.crates-io]` forks.

## Stage 1 delivery

- [x] Cross target and core+alloc hello: native C main/libc startup, aligned
  allocations, Vec/String/drop, C f64 FFI, initial-thread TLS and visible failures.
  **After this task chronium can:** cross-build and run an allocating Rust program.
- [x] Unmodified tiny-skia 0.12.0, std/PNG/SIMD off, deterministic heap scene
  matching the host checksum.
  **After this task chronium can:** run a real unchanged no_std+alloc rasterizer.

SDK builds, Rust frozen/offline builds, package checks and interactive QEMU
qualification are recorded separately from native hardware or performance.
The optional sample package stays outside core/rescue defaults. There is no
kernel ABI change, userland dependency pin, fake libc-crate target or general
Rust runtime API. The initial 1 MiB guarded stack remains unchanged.

## Path to std

Fork direction accepted (chronium, 2026-10-25): the Forgejo source mirror is
`mirrors/rust`. Chronium has created `PyxisOS/pyxis-rust`; the
implementation branch `pyxis` starts at the same full nightly
commit `48f4b05538b8eca412137042d9647a61e692c6b3`. Stage 2 adds the built-in
`x86_64-unknown-pyxis` target by carrying over the qualified stage-1 JSON
settings unchanged, plus the native Pyxis std sys module. That branch is the
future tier-3 upstream proposal, rather than a different compiler identity.

The [std milestone](rust-std.md) defines the horse toolchain build, native
platform surface, bindings fork, ordered delivery and hexyl qualification.
It selects cached Rust CI-LLVM rather than a second LLVM source build;
that choice and the first std profile are accepted (chronium, 2026-10-25). The existing
prebuilt nightly/custom JSON/build-std core+alloc delivery remains unchanged.
The [Go adapters](../development/go-toolchain.md) are design evidence, not a
Rust implementation or a reason to imitate Linux syscalls.

The later direction remains hexyl → tokei → ripgrep → bat → broot → xh →
gitoxide → Helix, with tiny-skia usable earlier, then wgpu. Crates selecting
only Unix/Windows will need native branches and audited feature sets. Horizon
adds independent winit/wgpu, tokio, audio, gamepad/HID and wasmtime requirements;
a no_std checksum or a basic std does not deliver those subsystems. Hosted
Rust/Cargo, unwinding and tier-3 upstreaming stay separate completion points.
