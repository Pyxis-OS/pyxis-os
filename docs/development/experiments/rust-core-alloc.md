# Rust core+alloc and tiny-skia qualification

2026-10-25, stage 1 of the [Rust plan](../../wip/rust.md). Accepted profile:
prebuilt nightly-2026-10-24, native SDK/Clang/LLD, static no_std P1F,
panic-abort and libc-backed allocation; no public ABI or kernel change.
The [toolchain reference](../rust-toolchain.md) gives reproducible commands.

## Source and toolchain

Base main `2446d3b8`, userland `8a2ff95e`, ports `05ab881f`, fs `f3f22b6e`.
The four host distribution SHA-256 values match
`toolchain/rust/host-toolchain.json`. Actual rustc `-Vv` confirms
`1.101.0-nightly`, commit `48f4b05538b8eca412137042d9647a61e692c6b3`, LLVM
23.1.4, Linux x86-64. Cargo is nightly `15be64aa0859b5c36f7a86d52f7aa0db44ba4d84`.
Pyxis SDK/kernel builds use unchanged builder LLVM 23.1.3-3d69e83.

Fresh libc/SDK and production kernel builds pass. Target builds then pass
with frozen/offline Cargo, ordinary ELF objects and no cross-version LTO.
Host reference selects Linux explicitly. Tiny-skia 0.12.0 and its nine-crate
registry closure are unchanged; hashes/notices are retained. No std/PNG/SIMD
feature or libc-crate dependency is used. Linked ELF/SDK inspection found no
missing symbol requiring a libc patch.
The cross-linked sample/package source checkpoint is `00509e30ad21`;
later ancestry and documentation updates do not change that tested code.
The checkpoint is retained on `probe/rust-core-alloc-qualified` (never merge).
After the shared-SHA SDK work merged, the final refresh uses main `41f1e4fc`,
userland `b468f20c` and ports `6520b079`, without changing those inherited pins.
SDK/kernel, required native libraries, native userland and frozen Rust builds
pass again. The refreshed source checkpoint `7dbf3509611b` is retained on
`probe/rust-core-alloc-refreshed` (never merge). The Rust target and runtime
code are unchanged; the builder additionally hashes every archive named by the
SDK link group rather than a copied library list.

Raw logs, exact image and package hashes, link traces, host result, compiler
identities and captured command outcomes remain under `/shared/present/rust/`.
The host toolchain keeps its original verified distributions and source notices.

## QEMU and debugger

Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, modern VirtIO GPU/net/RNG and
VirtIO-SCSI read-only ISO. No disk, RF or native machine is involved. The
qualification archive is the previously verified clean 0.0.8 RAM/Lua/Remote
archive for the initial direct-bundle checks. Its original SHA-256 is
`023a587a89a17984f11d479ed61b6c50d3085bede739afb57df079d8719728d3`.
The initial package round replaces native userland with a fresh build from
source and uses its actual `00509e30ad21` image identity and an explicit RAM
package/provider profile. The final refreshed round rebuilds the new pinned
userland and uses its ordinary live RAM configuration and `7dbf3509611b` identity. It never relabels an old
ABI to admit a new package. This is a qualification fixture, not a full
distribution build; unchanged third-party fixture inputs stay identified.
No game data or private native profile is added. Only loopback
HTTP/Remote/GDB host ports are exposed; the guest reaches the private repository through QEMU user NAT.

Direct ordinary bundle launch passes:

- `rust-hello`: Vec/String/drop, zeroed and grow/shrink allocations at 16 KiB
  alignment, C f64 FFI and initialized local-exec TLS.
- `rust-raster`: `fnv1a64=d0dfb9f8f80d6c59 width=96 height=64`, equal to the
  same-source Linux reference, including all premultiplied RGBA bytes.
- `rust-hello --panic`: visible diagnostic, process exit 101. A subsequent hello
  succeeds.
- `rust-hello --allocation-failure`: a real oversized allocation refused before
  physical backing, visible diagnostic and process exit 102. A subsequent hello
  succeeds. The first version of this check allowed LLVM to elide its unused
  allocation/free pair; the final pointer escape preserves the real request.

At the hardware breakpoint on Rust main, GDB observes `argc=1`, RSP alignment
8 modulo 16, FS `0x4008`, FS:0 self-pointer `0x4008`, and initialized Rust TLS
at FS−8 equal to `0x7352757374544c53`. This is libc's context, not a new Rust
FS owner. Raster disassembly contains page-sized inline stack-probe decrements
and touches before its remaining frame allocation. Actual ELF/map evidence
retains the native code and TLS symbols; a bytewise
comparison confirms each debug ELF PT_LOAD matches its P1F segment, flags,
extent and entry. There is no dynamic TLS resolver. The hello calls the real
libc fma/malloc symbols; no guest function was injected.

## Normal package path

The standard packer/checker accepts the optional `rust-samples` recipe. The
initial package `core-alloc+00509e30ad21` has 174 files, 4,687,872 archive bytes,
SHA-256 `5987992ec5015807150f9a59030598cd8864be302facda80c3d3207f80d802a1`.
The final refreshed `core-alloc+7dbf3509611b` includes the new SDK notices: 182
files, 4,767,744 bytes, SHA-256
`a0e87bffd4ec7c1853396d39f362fe7ae9bbce6b3c5e5ccd907608220e3ed2c6`.
This includes source/notices, not debug ELFs or the host compiler distribution.
A loopback unsigned HTTP repository supplies the package to the guest's normal
HTTP provider. `pkg --yes install rustlab/rust-samples` publishes active set 1;
`pkg verify` confirms selected files/indexes. Fresh bare-name `rust-hello` and
`rust-raster` launches pass, as do installed panic (101), allocation failure (102)
and another successful hello after both. All package/launch/failure checks pass
again with the refreshed SDK, with the same host/raster checksum. The runtime
stays core+alloc throughout.

Initial fixture/setup attempts are retained: the old release archive correctly
refused a package bound to a different image revision; a missing provider and
missing configure-network/confirmation setup were corrected in the private
fixture. The final source-built package round uses ordinary authority and the
existing explicit `--yes` option. No package or OS ABI line was rewritten to
make a refusal disappear, and no package/client/kernel fix was needed.

The unrelated full all-ports build stopped because the disposable builder lacks
host Meson. The required Lua/libpng/TLS closure and complete native userland
were built through their ordinary targets instead. Kernel, SDK, selected source
builds, the private QEMU fixture and the Rust package checks pass; a full local
all-ports distribution build is not claimed.

## Limits

These are functional results, not performance, native-hardware or complete
rasterizer conformance measurements. They do not qualify Rust std, sibling Rust
threads/destructors, dynamic TLS, unwinding or hosted tools. The later std
fork and tier-3 proposal remain separate work. No new test/CI infrastructure,
compiler-container rebuild, source dependency pin or core-image default was added.
