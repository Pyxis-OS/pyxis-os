# LLVM development prefix

Status: task-1 producer in progress; not delivered. The pinned source currently
fails in two ORC portability paths. See the [accepted source-closure decision](../wip/quake3-llvmpipe.md#task-1-build-checkpoint).

`toolchain/build-llvm-development.sh` is an opt-in build of native static LLVM
libraries and headers for Mesa. Its source is selected by `toolchain/llvm-development/revision`; the initial
build uses the existing hosted pin and compiler container. It does not
replace the compiler, alter the SDK, install a package or enable JIT execution.

```sh
JOBS=8 toolchain/build-llvm-development.sh /absolute/sdk /absolute/output /absolute/work
```

Use a current complete SDK, matching installed cross compiler, Linux C/C++
compiler, CMake, Ninja and Python. `LLVM_SOURCE` can select a clean checkout of
the exact hosted pin; otherwise the script fetches only from the internal fork.
Persistent work/output directories are locked. Tool or option changes require a
fresh work directory; SDK changes invalidate affected objects by content.

The target is X86, threads on, exceptions/RTTI off, static only, with optional
host libraries disabled. The closure is `bitwriter engine mcdisassembler core
executionengine scalaropts transformutils instcombine native mcjit`. Linking the
manual object-emission program builds its archive dependencies. The export
contains those archives, public/generated LLVM headers, the LLVM licence and
SDK/build inventories under one immutable `llvm-development-SHA256` directory,
plus a deterministic transfer tar and checksum.

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
expect `LLVM_MESON_DISCOVERY COMPLETE`. This discovers all ten component groups
through Meson's ordinary LLVM config-tool dependency.

The build also produces `WORK/target/bin/pyxis-llvm-object-check`. Place it at
`app/bin/check.pxe` with `toolchain/llvm-development/check.json` as `manifest.json`
in a temporary bundle. Run it with a writable output path:

```text
tmp://llvm-object-check.pxb tmp://llvm-object.o
```

Expect `LLVM_OBJECT COMPLETE target=x86_64 bytes=... execution=NONE`. Inspect the
result with an object reader: ELF64 little-endian x86-64 relocatable, exporting
`pyxis_answer`. The program emits into owned memory and writes an ordinary FILE;
it never maps or executes generated instructions. Neither linking MCJIT nor
this check qualifies executable-memory support. Luna builds published prefixes;
chronium publishes them. No compiler-container rebuild is needed.
