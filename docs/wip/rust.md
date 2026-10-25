# Rust on Pyxis

Stage 1 tasks 1–2 are implemented; the [toolchain reference](../development/rust-toolchain.md)
and [qualification record](../development/experiments/rust-core-alloc.md) describe
core+alloc, the allocating hello and unchanged tiny-skia checksum. This is not
std, a hosted compiler or Horizon support. Stage 2 waits for its separate go.

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

## Path to std — later fork, not stage 1

Fork direction accepted (chronium, 2026-10-25): the Forgejo source mirror is
`mirrors/rust`. When stage 2 starts, chronium creates `PyxisOS/pyxis-rust`,
with branch `pyxis` starting at the same full nightly
commit `48f4b05538b8eca412137042d9647a61e692c6b3`. Stage 2 adds the built-in
`x86_64-unknown-pyxis` target by carrying over the qualified stage-1 JSON
settings unchanged, plus the native Pyxis std sys module. That branch is the
future tier-3 upstream proposal, rather than a different compiler identity.

Build the forked toolchain on horse. Stage 2 will settle Rust's CI-LLVM through
an approved cache versus a separate LLVM build; neither is selected or needed
for stage 1. Keep the libc crate in a pinned fork and `[patch.crates-io]` overlay
until its Pyxis entry is upstream. The prebuilt nightly/custom JSON/build-std
core+alloc delivery remains unchanged; no fork build starts in stage 1.

Use a dedicated Pyxis `std::sys` backend and bindings matching Pyxis headers.
The existing [Go adapters](../development/go-toolchain.md) are design evidence,
not a Rust implementation or a reason to imitate Linux syscalls.

| Area | Available now | Rust work / missing contract |
|---|---|---|
| Allocation / startup / args | Synchronized libc heap, native startup, C main arguments | Reusable allocator/bindings; copy arguments, preserve real failures |
| Threads / parking | Join/detach, guarded stacks, native address WAIT/WAKE with monotonic deadlines; libc mutex/condition/once/keys | Rust thread/parking adapters; map requested stacks into 1–8 MiB bounds and respect grant/capacity failures |
| TLS / destructors | Local-exec TLS, libc FS owner, real language/key destructor drain | Rust TLS codegen and destructor registration; no dynamic TLS/modules or second FS owner |
| Time / entropy | Precise monotonic, wall clock, sleep and random under explicit grants | Required startup authority and infallible-API failure policy; no invented times or weak random fallback |
| Stdio | Real synchronized libc descriptors over supplied FILE/pipe/console grants | Rust locks/I/O error mapping; no ambient console; libc descriptor indices are not native capabilities |
| Files / metadata | File/directory operations, held identity and per-field metadata validity | Native metadata representation; unsupported permissions/symlinks remain unsupported, no invented UID/GID or timestamps |
| Paths / OsStr | Native scheme URIs, retained cwd, confined relative paths | Byte-preserving OsStr and native Path parsing; leading `/` is invalid, `..` cannot escape authority, provider URI spelling cannot be Unix-normalized |
| Environment | Native TEXT entries and owned snapshots; capabilities are separate | Safe enumeration/mutation and exported/private TEXT policy; no assumed writable `environ` or text-to-authority conversion |
| Networking | Native IPv4 TCP/UDP/readiness; Go DNS adapter over configured UDP scope, QEMU-qualified | Rust ownership/deadline adapters; libc has no socket API. No IPv6/Unix/raw sockets inferred |
| Process | Explicit native jobs, selected cwd/streams/env/grants, completion/termination | Command lookup/delegation and child cleanup need their own contract; no fork/exec/signal/PID emulation |
| Stack faults / unwind | Guarded eager stacks, process-fatal faults, C++ libunwind | Preserve inline probes; panic-abort first. Rust personality/EH/table retention and C boundaries require separate unwind qualification |
| Parallelism | Online CPU count and native allowed CPU sets | available_parallelism must reflect caller-effective eligibility, not just total online CPUs |

References: [libc limits](../userland/libc-portability.md),
[threads/TLS](../interfaces/threads.md), [libc threads](../userland/threads.md),
[paths](../userland/paths.md), [process state](../userland/process-state.md),
[native jobs](../userland/native-jobs.md), [loading](../kernel/program-loading.md).
These are runtime adaptation tasks, not missing reasons to add POSIX mechanisms
to Caelum. A compiled std is not qualification of every standard operation;
unsupported optional facilities must return honest errors. Missing-grant and
infallible-API behavior must be reviewed before that profile ships.

The later direction remains hexyl → tokei → ripgrep → bat → broot → xh →
gitoxide → Helix, with tiny-skia usable earlier, then wgpu. Crates selecting
only Unix/Windows will need native branches and audited feature sets. Horizon
adds independent winit/wgpu, tokio, audio, gamepad/HID and wasmtime requirements;
a no_std checksum or a basic std does not deliver those subsystems. Hosted
Rust/Cargo, unwinding and tier-3 upstreaming stay separate completion points.
