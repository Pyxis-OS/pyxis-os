# Rust on Pyxis

Stage 1 investigation and tasks 1–2 accepted and assigned by chronium,
2026-10-25. The decisions below authorize the bounded implementation;
qualification is not yet complete. First deliver a cross-built
`core` + `alloc` program and an unmodified tiny-skia memory-rendering check.
Neither `std`, a hosted Cargo/rustc nor Horizon is in that first delivery.

Inspected main `2446d3b8`, userland `8a2ff95e`, ports `05ab881f`, filesystem
`f3f22b6e`, and the existing LLVM 23.1.3 SDK. Source/manifest inspection and one
host-side Clang data-layout query are complete. No Rust compiler was installed,
Rust code compiled, target linked, QEMU booted or native machine contacted.
Link compatibility below is a proposed contract, not a passed build result.

## Accepted decisions (chronium, 2026-10-25)

1. **Toolchain and installation. Accepted:** pin Linux-amd64
   `nightly-2026-10-24`, including matching rust-src, outside the builder image.
   Use an opt-in, hash-checked host prefix fetched through approved internal
   caches. No `RUSTC_BOOTSTRAP`, rolling nightly, LLVM rebuild or ordinary CI
   dependency. Stable 1.99.1 is current, but does not supply the proposed
   nightly `build-std` workflow. A built-in target/prebuilt target libraries can
   revisit stable later. Container inclusion is optional later; chronium builds
   a new container only when its installed Rust components actually change.
2. **First runtime profile. Accepted:** static, non-PIE P1F through the
   existing Pyxis Clang/LLD and libc startup; `panic=abort`, libc-backed
   `GlobalAlloc`, visible panic/allocation failure and nonzero process exit.
   Start with the hello, then tiny-skia 0.12.0 without its std/PNG/SIMD features.
   No kernel change, unwinding, Rust threads or replacement FS owner.
3. **Target identity and later std. Accepted:**
   `x86_64-unknown-pyxis`, `target_os="pyxis"`, no Unix/Windows family flag.
   Implement a dedicated Pyxis std platform later, reusing truthful libc/native
   facilities. Accept small pinned crate patches for this identity rather than
   selecting Unix code that assumes sockets, signals, fork or Unix paths.
   Tier-3 upstreaming with chronium as maintainer and a libc-crate entry follow
   qualification; they are not prerequisites for the first JSON target.

## Pin and cache admission

The dated [distribution manifest](https://static.rust-lang.org/dist/2026-10-24/channel-rust-nightly.toml)
reports rustc `1.101.0-nightly (48f4b0553 2026-10-23)`, source commit
`48f4b05538b8eca412137042d9647a61e692c6b3`. It advertises these Linux-amd64
components as available; their **manifest-provided** SHA-256 values are below.
Archives have not been fetched or verified in this investigation.

All URLs use `https://static.rust-lang.org/dist/2026-10-24/` plus the filename:

| Component / filename | SHA-256 |
|---|---|
| `rustc-nightly-x86_64-unknown-linux-gnu.tar.xz` | `8ad144cf790032d0c5bb24c2a3b366834b3f6366a51c461f2f01c30625387dc3` |
| `cargo-nightly-x86_64-unknown-linux-gnu.tar.xz` | `e1445ac2dc3c74a9976e5878214f39f0ea9f7ec353bf0074a8a9c2c412e096f0` |
| `rust-std-nightly-x86_64-unknown-linux-gnu.tar.xz` (host) | `400853c9a95b2be16a72ba27ab95bde77656f89d84077bd5a2319e4b12f5d27e` |
| `rust-src-nightly.tar.xz` | `9339dc1e3f961c36d4ed371dd4cadbf3ffeeb083b87f29f112c67dc3062e9d89` |

Retain Rust's MIT/Apache-2.0 notices, the distribution's bundled LLVM and other
component notices, compiler identity and source manifests. Rebuilding the Pyxis
SDK does not rebuild Rust. Changing Rust invalidates its target libraries and
application artifacts, not the C compiler container.

The pinned Rust tree references LLVM commit
`f738f3b0ed65c01edcb44a504a77edc12a82c319`; its
[version file](https://github.com/rust-lang/llvm-project/blob/f738f3b0ed65c01edcb44a504a77edc12a82c319/cmake/Modules/LLVMVersion.cmake)
says 23.1.4. This is source evidence; the implementation must also retain
`rustc -Vv`. Pyxis Clang/LLD is 23.1.3. Emit normal ELF relocatable objects,
`lto=false`, `embed-bitcode=no`, no linker-plugin/cross-language LTO, and link
with Pyxis LLD, not bundled rust-lld. That keeps LLVM bitcode out of the
cross-version boundary. [Rust's LTO documentation](https://doc.rust-lang.org/rustc/linker-plugin-lto.html)
requires toolchain compatibility for mixed-language LTO; matching version
numbers alone would not qualify it.

Caches provisioned by chronium (2026-10-25):
`https://repo.internal/repository/raw-rust/<date>/<file>` maps to
`https://static.rust-lang.org/dist/<date>/<file>`; do not repeat `dist/` in
the proxy path. The closed Cargo proxy is
`https://repo.internal/repository/cargo-proxy/`, including its sparse index
and crate download route. Install only hash-checked pinned components and
freeze the dependency closure, including rust-src build dependencies. No
direct-Internet build fallback is permitted.

Use [Cargo source replacement](https://doc.rust-lang.org/cargo/reference/source-replacement.html)
for the unmodified crates.io mirror. Use a separate, small
`[patch.crates-io]` overlay of exact Forgejo fork commits for Pyxis changes;
never publish changed crates under the same registry identity. A locked,
checksum-verified [vendor directory](https://doc.rust-lang.org/cargo/commands/cargo-vendor.html)
is a suitable initial offline input once seeded through approved cache routes.
Record every dependency and notice, then build with `--frozen`. The proxy
index/download route has been checked; target builds remain to be qualified.

## Target and link boundary

The proposed [custom JSON](https://doc.rust-lang.org/nightly/rustc/targets/custom.html)
uses the pinned compiler's schema; custom target JSON and
[build-std](https://doc.rust-lang.org/nightly/cargo/reference/unstable.html#build-std)
are version-bound. For this nightly include `-Zjson-target-spec` as well as
`-Zbuild-std=core,alloc`. Host build scripts/proc macros stay Linux binaries;
only the selected target receives SDK linker flags.

| Target property | Proposed setting / source |
|---|---|
| Architecture / C ABI | x86-64, little-endian LP64, 64-bit pointers, SysV C ABI, maximum native atomic width 64 |
| LLVM triple / Rust identity | LLVM `x86_64-unknown-none-elf`; Rust OS `pyxis`, vendor `unknown`, empty environment and target families |
| Code / relocation | Small code model, static relocation, no PIE/dynamic libraries; SDK executable convention starts at `0x400000` |
| CPU / floating point | Generic x86-64/SSE2, normal hard-float C calling convention; no AVX/FSGSBASE assumptions |
| Red zone / probes | Red zone disabled; inline stack probes, verified in emitted code, no signal-based guard recovery |
| TLS | `has-thread-local=true`, executable local-exec, x86-64 variant II; libc retains FS and its TCB |
| Panic / linker | Abort; GNU C-driver linker flavor through a narrow SDK wrapper, existing Pyxis Clang and LLD |

Do not copy Rust's built-in
[`x86_64-unknown-none` spec](https://github.com/rust-lang/rust/blob/48f4b05538b8eca412137042d9647a61e692c6b3/compiler/rustc_target/src/spec/targets/x86_64_unknown_none.rs)
unchanged: that target
selects kernel code model, soft-float/no-SSE ABI, rust-lld and PIE defaults.
Those are not the Pyxis userspace ABI.

The existing builder's Clang query:

```sh
x86_64-unknown-pyxis-clang -S -emit-llvm -x c /dev/null -o -
```

reports this layout, also present in the pinned Rust x86-64 spec:

```text
e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128
```

The wrapper consumes the current [SDK](../development/sdk.md#building-applications)
startup, static link flags and linker script: `crt0.o`, target objects/Rust
rlibs, then the SDK archive group (`libc`, `libterm`, `libpyxis`, compiler-rt
builtins), with 4 KiB segment alignment. Avoid duplicate forced definitions
between Rust compiler-builtins and compiler-rt; retain the link map and actual
undefined-symbol closure. Do not force whole-archive for both runtimes.

Use `#![no_std]` / `#![no_main]` and export a C-ABI `main(argc, argv)`;
`_start` and `libc_enter` remain libc-owned. Existing startup supplies bindings,
TLS, allocator and stdio before main. LLD already writes P1F directly and carries
the TLS template through the existing linker symbols; no ELF converter or Rust
linker backend is needed. Confirm Rust's actual relocations/FFI at qualification,
including float arguments and over-aligned allocations. Link a separate debug
ELF with the same addresses when inspecting the guest.

## Assigned implementation

1. [ ] **Cross target and core+alloc hello.** Put target/link integration and the
   qualification sample under Pyxis's `toolchain/`, using the owner-provided
   cached host prefix and existing SDK. Use explicit libc FFI, not a fictitious
   supported libc-crate target. `GlobalAlloc` uses malloc/free; checked
   over-allocation and a retained base pointer handle alignment above malloc's
   16-byte guarantee. Libc aligned_alloc itself stops at 4 KiB, so it cannot
   silently satisfy every Rust Layout. Keep zeroing/reallocation alignment and
   failure semantics correct. Print with libc stdio, drop a Vec/String, and
   make panic/allocation failure produce bounded diagnostics and nonzero
   process exit; no successful fake operation or unwind.
   **After this task chronium can:** build, package and run an allocating Rust
   program on Pyxis.
2. [ ] **Unmodified tiny-skia check.** Pin `tiny-skia = 0.12.0`,
   `default-features=false`, `features=["no-std-float"]`; leave std, PNG and SIMD
   off. Its published crate checksum is
   `47ffee5eaaf5527f630fb0e356b90ebdec84d5d18d937c5e440350f88c5a91ea`,
   license BSD-3-Clause. Freeze and audit the complete Cargo.lock closure;
   a top-level crate checksum is not dependency closure. Render a deterministic
   scene into a heap pixmap and report a checksum against the same-source host
   reference. No display API or image decoder is needed.
   **After this task chronium can:** run a real unchanged no_std+alloc rasterizer
   crate, with checked output.

Package ordinary `.pxb` program directories through the existing package
recipe/packer, as [Go programs](../development/go-toolchain.md) do. The sample
requests `memory/manage`; caller-supplied output grants remain required. It
needs neither network nor thread creation. Keep the 1 MiB initial stack unless
measured needs justify a bounded manifest request. Include notices, target,
compiler, lockfile and SDK provenance. Do not add it to the core/rescue archive
or change a package ABI line to bypass admission.

Qualify with ordinary SDK builds and interactive QEMU: visible hello, allocation
alignment/drop behavior, explicit panic failure, actual P1F launch/exit and the
tiny-skia checksum. Inspect the ELF/map, stack-probe and TLS codegen in GDB.
No automatic new CI suite, compiler container rebuild, performance claim or
native run is required for this first delivery. Any required shared libc change
belongs in a userland PR before a parent pin; none is identified as necessary
for the proposed hello.

## Path to std — later proposal, not stage 1

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
