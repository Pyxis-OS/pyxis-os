# SDK and repository integration

Pyxis pins [pyxis-userland](https://git.internal/PyxisOS/pyxis-userland) as the
`userspace` Git submodule and [pyxis-ports](https://git.internal/PyxisOS/pyxis-ports)
as `ports`, [pyxis-lwip](https://git.internal/PyxisOS/pyxis-lwip) as
`third_party/lwip`, and [pyxis-fs](https://git.internal/PyxisOS/pyxis-fs) as `fs`.
The committed gitlinks select exact revisions;
normal builds never follow a remote branch or update the pin automatically.

## Checkout and updates

For an existing checkout:

```sh
git submodule update --init userspace ports third_party/lwip
make image
```

New clones can use `git clone --recurse-submodules`. Run the submodule update
again after pulling a parent commit that changes the pin. Commit and publish
dependency changes in their repository before committing the corresponding gitlink
in Pyxis. A submodule checkout may be detached; create a branch there before
starting work. Local uncommitted source edits are usable for development;
the exported SDK manifest records dirty userland inputs.

The filesystem core is opt-in: `git submodule update --init fs` followed by
`make -j16 fs-tools` builds `build/fs-tools/libpyxis-fs.a`, `mkpyxisfs` and
`pyxisfs-inspect`. Kernel/SDK/image builds do not require it. See the
[host-tool guide](../fs/docs/host-tools.md) for empty-image creation and inspection.

The relative URL in `.gitmodules` resolves beside the Pyxis repository, using
the parent remote's host and transport. These repositories are public under
`PyxisOS`. CI uses the checkout action's automatic token for its own repository
and reads the public submodules without a custom source-read secret. Anonymous
Git reads must be allowed by the Forgejo instance. The workflow builds the
integrated kernel and ISO; it does not build a compiler or follow any
submodule's latest main.

To update a checkout that still points to the former personal namespace:

```sh
git remote set-url origin ssh://git@git.internal:2222/PyxisOS/pyxis-os.git
git submodule sync --recursive
```

Linked worktrees share their repository's remote configuration. Update the origin
of standalone dependency clones separately; changing a remote does not change
the checked-out revision or dependency pins.

## Ownership and build order

| Repository | Owned inputs |
| --- | --- |
| Pyxis | Kernel, public ABI/format headers, shared shebang parser, elf2pxe, compiler patches/container, SDK export and image assembly |
| pyxis-userland | libc, libpyxis, libterm, native TLS adapter, startup/link support, applications, initial boot scripts and its TLSF vendor copy |
| pyxis-ports | Host Lua runner, pinned third-party recipes, ordered patches and staged executables/licenses and development libraries/headers |
| pyxis-lwip | Pinned lwIP source subset, license/provenance and any local upstream adaptations |
| pyxis-fs | Native filesystem format, freestanding core, empty-image formatter and diagnostic inspector with Linux host adapter |

`make sdk` exports headers, shared parser source and compiler settings, builds
the pinned userland runtime, then installs startup, libraries, linker support
and elf2pxe into `build/sdk`. `make userspace` builds applications against that
SDK and the Lua, HTTP-parser and TLS libraries exported by `make ports`. Ports consumes only the SDK,
so building it before userland introduces no cycle. `make image`
includes both in initrd and ISO assembly. The parent passes
explicit output directories, preserving `build/runtime` and `build/userspace`.
See [SDK commands and layout](sdk.md) for standalone and focused builds.

The lwIP configuration, private headers, allocator/clock hooks and build rules
remain in Pyxis under `kernel/net/lwip`. Normal kernel builds consume the pinned
source submodule and record its revision/local state in the kernel bundle
manifest. CI's kernel job therefore checks out submodules too. The image job
can still assemble prebuilt bundles without source submodules.
See [the port boundary](lwip.md) for worker and packet ownership.

The public ABI remains authoritative in Pyxis. Userland consumes it through the
SDK, with no kernel-private include paths or copied ABI headers. The shared
shebang source is exported as `share/pyxis/shebang.c` and compiled into libpyxis.
Kernel and userland each maintain a copy of the same pinned TLSF release with
their own adapters and upstream/license records; allocator logic was unchanged
by extraction.

The userland import preserves relevant source/vendor history, authors, dates
and license notices from Pyxis. Its [import record](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/IMPORT.md)
identifies the source revision and filtering operation. The original full
history remains in Pyxis.

## Filesystem repository

[PyxisOS/pyxis-fs](https://git.internal/PyxisOS/pyxis-fs) will own the shared native
filesystem format/core, host formatter/inspector and eventual Linux FUSE adapter.
The [initial format milestone](wip/filesystem-readonly.md) now pins the MPL-2.0
shared core at `fs/`. Its opt-in `make fs-tools` builds the freestanding archive
and Linux host formatter/inspector
with `HOSTCC`/`HOSTAR` forwarded as `HOST_CC`/`HOST_AR`, explicit source/output
directories and no kernel or SDK include paths. The tools create empty standalone
sparse images and report pool/volume diagnostics. Its
[format and tool contract](https://git.internal/PyxisOS/pyxis-fs/src/commit/8c4ffa67595f05eeef97b159d0af0cfb84da1ef5/docs/format.md)
lives in that repository, including accepted follow-up decisions. See the
[implemented core boundary](../fs/docs/core.md) for codec, construction, selection,
catalog allocation-proof and lifetime contracts. Pyxis retains the public OS ABI, capabilities
and namespace integration. Default kernel, SDK, ports and image targets do not
build the core or acquire a filesystem dependency. CI and the compiler container
are unchanged.

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
