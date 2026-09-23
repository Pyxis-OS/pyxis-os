# SDK, target toolchain and repository separation

Status: working milestone draft, not an implementation assignment. Agreed
direction is distinguished from proposals and open decisions below. See the
[planning index](boot-sdk-ports.md) for sequencing.

Agreed ownership:

- Pyxis: kernel, exported native ABI/format headers, boot/image assembly,
  toolchain patches/container recipe, elf2pxe and pinned integration revisions.
- Userspace: libc, libpyxis, libterm, startup/link support, native applications
  and initial boot scripts.
- Ports: pinned third-party source descriptions, patches, recipes and staging
  rules; consume an explicit SDK rather than internal kernel source paths.

Ship the host elf2pxe executable in the SDK; userspace and ports consume it there.
Its source stays in Pyxis alongside the format definition.

Keep public ABI headers authoritative in one repository and export them into
the SDK. Do not move kernel-private headers into the sysroot or maintain manual
copies across repositories. Existing consumers remain under project control:
no compatibility shims or automatic ABI-version increments are implied.

The agreed build order avoids a userspace/SDK dependency cycle:

1. Use the prebuilt compiler and export target headers into a staging sysroot.
2. Build userspace runtime/startup and libraries against those headers.
3. Export the complete SDK: public headers, startup objects, libc/native/terminal
   archives, linker support and the host executable converter.
4. Build applications and selected ports against that SDK, stage their outputs,
   and assemble the boot archive/ISO alongside the kernel.

The userspace repository owns libc and the native runtime, with separate runtime
and application build phases. Pyxis owns public ABI headers and orchestrates SDK
assembly. This does not require a separate runtime repository.

Keep the compiler binary and evolving target sysroot separate. Configure the
compiler for an external sysroot and supply the selected SDK to each build.
The agreed target is x86_64-unknown-pyxis, with __pyxis__, appropriate startup
and linker defaults, a defined C data/calling convention and target libgcc.
A target name alone is insufficient. Port code must use only the runtime and
machine features Pyxis actually supports.

Continue using ELF objects, indexed .a libraries and a debug ELF followed by
ELF-to-PXE conversion initially. Native PXE binutils support, dynamic linking
and custom library formats need separate justification.

Ordinary header/library/linker-script/converter changes should ship in the SDK.
Compiler/binutils patches, target conventions, compiler runtime configuration
or required host dependencies may require a container rebuild. Record exact
source revisions and required compiler identity with SDK artifacts; this is
build provenance, not a new compatibility-versioning policy. Integration and
future dispatch jobs should select an exact SDK and source revisions, rather
than racing an unqualified latest artifact. The owner controls dispatch setup
and container publication.

## Completion boundary

A pinned userspace checkout builds runtime libraries and applications using the
prebuilt target compiler and exported SDK. Pyxis assembles the kernel, initrd,
ISO and SDK artifacts without compiling GCC/binutils in ordinary workflows.
The ports repository can consume that SDK through the same explicit contract.

Repository separation should preserve the working boot environment. The owner
creates repositories, configures dispatch integration and rebuilds/publishes the
compiler container when required. Initial orchestration stays in Pyxis with
pinned submodules; cross-repository workflow design is separate work.

## Focused tasks

- [x] Separate runtime/application builds, export a relocatable SDK, and build
  existing applications using only the compiler, application sources and SDK.
  Implemented usage and layout are in [the SDK reference](../sdk.md). This step
  retains the existing x86_64-elf compiler and does not change the container.
- [ ] Add the Pyxis compiler target and pinned toolchain patches, build it
  separately, and update the container recipe. Use GCC 16.2.0 and binutils 2.47;
  the owner rebuilds/publishes the container when ready.
- [ ] Extract userspace with source/license history preserved into
  `pyxis-userland`, then integrate the pinned submodule without changing boot.
  The owner creates the empty repository when extraction is ready.

## Decisions before the remaining implementation

- Define the Pyxis target's exact driver/startup defaults and libgcc build.
  The SDK layout is now implemented; the first export still uses x86_64-elf.
  Local validation may use the existing binutils 2.46.1 installation until the
  target toolchain step; its actual identity is recorded in the SDK manifest.
- Settle repository extraction order and source/license history preservation.
- Identify the container changes before asking the owner to rebuild it.

No custom object/archive format, dynamic linking or package manager is required.

## References

- [Current compiler container](../../ci/Containerfile) and
  [artifact workflow](../../.forgejo/workflows/build.yml).
- [GCC sysroot selection](https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html).
