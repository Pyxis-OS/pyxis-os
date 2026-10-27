# LLVM development prefix

`toolchain/build-llvm-development.sh` is an opt-in build of native static LLVM
libraries and headers for Mesa. `toolchain/llvm-development/revision` selects
its source independently of hosted Clang, using the existing compiler container.
It does not
replace the compiler, alter the SDK, install a package or enable JIT execution.

```sh
JOBS=8 toolchain/build-llvm-development.sh /absolute/sdk /absolute/output /absolute/work
```

Use a current complete SDK, matching installed cross compiler, Linux C/C++
compiler, CMake, Ninja and Python. `LLVM_SOURCE` can select a clean checkout of
the exact development pin; otherwise the script fetches only from the internal fork.
Persistent work/output directories are locked. Tool or option changes require a
fresh work directory; SDK changes invalidate affected objects by content.

The target is X86, threads on, exceptions/RTTI off, static only, with optional
host libraries disabled. The closure is `bitwriter engine mcdisassembler core
executionengine scalaropts transformutils instcombine native mcjit orcjit`. Linking the
manual object-emission program builds its archive dependencies. The export
contains those archives, public/generated LLVM headers, the LLVM licence and
SDK/build inventories under one immutable `llvm-development-SHA256` directory,
plus a deterministic transfer tar and checksum. SDK notices and embedded
third-party Support terms accompany the LLVM licence.

`bin/llvm-config` runs on the Linux build host. It is the upstream tool compiled
against host Support with the target build's generated component graph and
flags. Its reported includes/libraries refer to the adjacent target prefix,
not host LLVM. The generated component table excludes unbuilt archives and
groups with missing dependencies, so Meson cannot select an absent optional
module. Asking for an unbuilt component fails. It is for cross-build
discovery, not a Pyxis executable. The prefix can move as one directory.

## Manual qualification

Point a Meson cross file's `llvm-config` binary at that prefix and select the
SDK's cross compilers/sysroot. Configure `toolchain/llvm-development/meson.build`;
expect `LLVM_MESON_DISCOVERY COMPLETE`. This discovers all eleven component groups
through Meson's ordinary LLVM config-tool dependency.

The build also produces `WORK/target/bin/pyxis-llvm-object-check`. Place it at
`app/bin/check.pxe` with `toolchain/llvm-development/check.json` as `manifest.json`
in a temporary bundle. Run it with a writable output path:

```text
tmp://llvm-object-check.pxb tmp://llvm-object.o
```

Expect `LLVM_ORC_REFUSAL COMPLETE transport+shared-library=NOT_SUPPORTED`,
then `LLVM_OBJECT COMPLETE target=x86_64 bytes=... execution=NONE`. Inspect the
result with an object reader: ELF64 little-endian x86-64 relocatable, exporting
`pyxis_answer`. The program emits into owned memory and writes an ordinary FILE;
it never maps or executes generated instructions. Neither linking MCJIT nor
this check qualifies executable-memory support. Luna builds published prefixes;
chronium publishes them. No compiler-container rebuild is needed.

## Explicit JIT sections

**Accepted (chronium, 2026-10-27):** only SectionMemoryManager's default native
mapper selects JIT allocation. Its native `MF_JIT` marker requires held
`jit/manage`; ordinary mapped RW users stay on MEMORY even when that grant is
held. Owning allocation descriptors retain a non-reused identity and native
service; borrowed section views retain that provenance and cannot release the
whole allocation. Protection rounds contained views to native pages and requests
RW/NX, R/NX or R/X. No raw-address lookup grants authority over reused storage.
The original owner outlives every borrowed view. `MemoryBlock` has additional
native provenance fields: rebuild the library prefix and its native consumers
together; do not mix old headers or archives with the new layout.

The kernel's completed PROTECT supplies instruction visibility. The LLVM cache
hook performs no additional raw-address operation or executable-memory grant.
NearBlock is advisory and may be ignored. Missing JIT authority refuses; no
compiler or shipped space newly acquires it.

[LLVM JIT checks](experiments/llvm-jit.md) qualify generated ORCv2 functions through
an explicitly selected RuntimeDyld/SectionMemoryManager object layer. They do
not qualify default JITLink or MCJIT. MCJIT's unsupported process bootstrap and
ignored finalization failures remain [open decisions](../wip/jit-llvm.md).

## Boundaries and qualification

The development-only fork refuses ORC's FD-based remote-executor factory with
`operation_not_supported` before taking descriptors. The shared-library path
resolver reports the same error, and its validator accepts no shared library.
Its POSIX mode/symlink caches are absent on Pyxis. Ordinary Support native FILE
lookup/private memory remain available. The execution-engine C header includes
its own standard `size_t` definition. Hosted Clang and installed compiler pins
are unchanged.

[Task-1 qualification](experiments/llvm-development.md) records host and target
builds, relocated Meson discovery, an independently linked consumer of the
exported prefix and four-CPU QEMU object emission. That initial check executed
no generated code. Native hardware and hosted compilation of the consumer on
Pyxis remain unrun; the separate JIT record
reports the later ORCv2 execution and current MCJIT refusal.
The prefix is a config-tool export; it does not ship a CMake package.
