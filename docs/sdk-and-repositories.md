# SDK and repository integration

Pyxis pins [pyxis-userland](https://git.internal/chronium/pyxis-userland) as the
`userspace` Git submodule and [pyxis-ports](https://git.internal/chronium/pyxis-ports)
as `ports`. The committed gitlinks select exact revisions;
normal builds never follow a remote branch or update the pin automatically.

## Checkout and updates

For an existing checkout:

```sh
git submodule update --init userspace ports
make image
```

New clones can use `git clone --recurse-submodules`. Run the submodule update
again after pulling a parent commit that changes the pin. Commit and publish
userland or ports changes in their repository before committing the corresponding gitlink
in Pyxis. A submodule checkout may be detached; create a branch there before
starting work. Local uncommitted source edits are usable for development;
the exported SDK manifest records dirty userland inputs.

The relative URL in `.gitmodules` resolves beside the Pyxis repository, using
the parent remote's host and transport. CI checks out submodules using the
`PYXIS_SOURCE_READ_TOKEN` secret, which must grant repository read access to
Pyxis, pyxis-userland and pyxis-ports. The existing workflow builds the integrated
kernel and ISO. It does not build a compiler or follow either submodule's latest main.

## Ownership and build order

| Repository | Owned inputs |
| --- | --- |
| Pyxis | Kernel, public ABI/format headers, shared shebang parser, elf2pxe, compiler patches/container, SDK export and image assembly |
| pyxis-userland | libc, libpyxis, libterm, startup/link support, applications, initial boot scripts and its TLSF vendor copy |
| pyxis-ports | Host Lua runner, pinned third-party recipes, ordered patches and staged executables/licenses and development libraries/headers |

`make sdk` exports headers, shared parser source and compiler settings, builds
the pinned userland runtime, then installs startup, libraries, linker support
and elf2pxe into `build/sdk`. `make userspace` builds applications against that
SDK and the Lua library exported by `make ports`. Ports consumes only the SDK,
so building it before userland introduces no cycle. `make image`
includes both in initrd and ISO assembly. The parent passes
explicit output directories, preserving `build/runtime` and `build/userspace`.
See [SDK commands and layout](sdk.md) for standalone and focused builds.

The public ABI remains authoritative in Pyxis. Userland consumes it through the
SDK, with no kernel-private include paths or copied ABI headers. The shared
shebang source is exported as `share/pyxis/shebang.c` and compiled into libpyxis.
Kernel and userland each maintain a copy of the same pinned TLSF release with
their own adapters and upstream/license records; allocator logic was unchanged
by extraction.

The userland import preserves relevant source/vendor history, authors, dates
and license notices from Pyxis. Its [import record](https://git.internal/chronium/pyxis-userland/src/branch/main/IMPORT.md)
identifies the source revision and filtering operation. The original full
history remains in Pyxis.

## Toolchain and remaining boundaries

The prebuilt [Pyxis toolchain](../toolchain/README.md) uses an external SDK
sysroot. Ordinary runtime, header, startup or linker-script changes ship in the
SDK; compiler patches and target/runtime conventions can require rebuilding the
container. SDK manifests identify both repository revisions and compiler/host
identities without introducing an ABI compatibility version.

Userland-specific CI, dispatch orchestration and SDK artifact exchange remain
separate work. The owner configures dispatch and publishes compiler containers.
The [ports build](ports.md) consumes the SDK through the same boundary. Native
PXE binutils support, dynamic linking and custom library formats are not required here.
