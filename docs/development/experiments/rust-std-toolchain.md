# Fork-built Rust compiler and core+alloc qualification

2026-10-25, gate 1 of the [std milestone](../../wip/rust-std.md).
This qualifies the compiler and built-in target. Pyxis std, Rust workers,
filesystem adapters and hexyl remain unimplemented. No kernel, ABI, runtime,
package default or compiler container changes are part of this gate.

## Revisions and inputs

Pyxis main `4c5ed5f4`, userland `67aa6220`, ports `a05ca2c3`, fs `f3f22b6e`,
lwIP `d52ebd20`. SDK/kernel and the selected native userland fixture are rebuilt
from these pins using existing builder LLVM 23.1.3-3d69e83. This is a selected
qualification image, not a build of every port or the normal distribution image.

[pyxis-rust #1](https://git.internal/PyxisOS/pyxis-rust/pulls/1),
`42dcad901bd40b22e699eb8ecc56e759353cc1d7`, descends from
`48f4b05538b8eca412137042d9647a61e692c6b3` (nightly-2026-10-24).
Its built-in target preserves the stage-1 JSON settings; normalized target JSON
differs only in tier metadata (unset to 3). `std=false` remains truthful.
The source also registers the new OS, admits the target missing from stage0,
and adds the exhaustive rustdoc OS label. No standard-library backend is added.

The [input manifest](../../../toolchain/rust/fork-toolchain.json) records all
hashes and internal/upstream URLs. Rust's CI LLVM freshness revision is
`7189d39d9ce936881b8132c6addeaf3e295cd455`, LLVM source
`f738f3b0ed65c01edcb44a504a77edc12a82c319` (23.1.4). Its cached archive SHA-256 is
`db64ad7640a3f8b3b0070d9063187c60754ca43dcad2961adc6c4c53621214a3`.
The three 2026-09-29 beta stage0 archives and pinned backtrace source verify
through the internal caches. Rust/backtrace notices are MIT OR Apache-2.0;
LLVM retains its Apache-2.0 WITH LLVM-exception notices. The libc 0.2.191 fork
baseline exists but is not used by this core+alloc graph.

## Host build and export

The Linux-host compiler is built on horse (x86-64 CachyOS, native Clang 22.1.8),
with four bootstrap jobs, the pinned beta and CI LLVM, and no downloaded CI rustc.
Bootstrap, compiler and library Cargo graphs are seeded through the closed
Cargo proxy; actual builds run offline inside a network namespace with no network
access. No user `RUSTC_BOOTSTRAP` workaround or compiler-container rebuild is used.
[Reproduction commands](../rust-toolchain.md#fork-built-compiler-and-built-in-target)
cover the pinned cache, bootstrap configuration, build and export.

Results:

- Stage-2 Linux-host rustc build passes; delivered identity is
  `rustc 1.101.0-nightly (42dcad901 2026-10-25) (pyxis)`, LLVM 23.1.4.
- Existing `rustc_target` built-in consistency/round-trip test passes:
  one selected test, 341 filtered. No new test framework is added.
- Host std/proc_macro and Pyxis core/alloc/compiler-builtins build. Inspection
  of all three target rlibs finds no embedded `.llvmbc`; final target links use
  ordinary native objects with Pyxis Clang/LLD 23.1.3, with LTO off.
- The standalone exporter passes. All compiler, Cargo and library files used
  for QEMU match the public export byte-for-byte; rebuilding both samples from
  that export produces identical qualified P1F files. Original stage-1 prefix
  remains separate and unchanged.
- Frozen `--fork` sample builds use the built-in target and installed libraries,
  without JSON-target or build-std flags. The unchanged tiny-skia 0.12.0 host
  reference prints `fnv1a64=d0dfb9f8f80d6c59 width=96 height=64`.

Target and host libraries must be present together in the final sysroot;
bootstrap can recreate it between build invocations. Export refuses missing
host std or target libraries. An initial private exporter followed bootstrap's
source symlinks back into build outputs; that incomplete copy was removed. The
shipped exporter excludes both source links and copies library source once.
These were host provisioning failures, not successful compiler qualification.

## QEMU and debugger

Nested QEMU/KVM, q35, host CPU, one and four vCPUs, 2 GiB RAM, OVMF, modern
VirtIO GPU/net/RNG and read-only ISO; no disk or native machine is involved.
The fixture contains freshly built native startup/session/Remote programs,
Lua, a RAM home and both unchanged Rust samples. It exposes only loopback
Remote/GDB forwarding, with no game data, install authority or native profile.

| Exercise | One CPU | Four CPUs |
| --- | --- | --- |
| Hello: Vec/String/drop, 16 KiB allocation alignment, C f64 FFI, TLS | Exit 0 | Exit 0 |
| Raster: host-matching full RGBA checksum `d0dfb9f8f80d6c59` | Exit 0 | Exit 0 |
| Explicit panic: visible diagnostic | Exit 101 | Exit 101 |
| Real oversized allocation: visible diagnostic | Exit 102 | Exit 102 |
| Hello after failures | Exit 0 | Exit 0 |

Remote command-completion records assert program results independently of the
shell result. Local QMP keyboard launches also show the checksum and hello
success. GDB stops at Rust main without injecting a guest call: argc is 1,
argv names the actual bundle, RSP is 8 modulo 16, FS is `0x4008`, FS:0 is the
same self-pointer, and initialized Rust TLS at FS-8 is `0x7352757374544c53`.
The program resumes and exits 0. Libc still owns startup and FS.

Both executables enter at `0x400000` with three load segments. Byte comparison
of each debug ELF PT_LOAD and P1F verifies entry, flags, extents and load bytes.
Hello's initialized TLS is eight bytes with alignment eight. Final P1F SHA-256:

- Hello: `33879b4a8e1cda0047e07eec34013f8a334bc6f08d572d50b6215e92bbbfb0e3`.
- Raster: `a93265777be33e6fea0bcc9e4158924206ee875aa96c080f1e59a8123456dabe`.

Raw build logs, input/output receipts, samples, debugger capture and QEMU command
records remain under `/shared/present/rust-std/`. All guests/debuggers are stopped.
There is no native or performance result. Matched launch/session-backing controls
remain required before the next gate changes runtime behavior. This repeat does
not qualify Rust std, threads, an allocator-exhaustion recovery policy, general
crate compatibility or native hardware behavior.
