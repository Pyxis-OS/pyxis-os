# SDK and repository integration

Pyxis pins [pyxis-userland](https://git.internal/PyxisOS/pyxis-userland) as the
`userspace` Git submodule and [pyxis-ports](https://git.internal/PyxisOS/pyxis-ports)
as `ports`, [pyxis-lwip](https://git.internal/PyxisOS/pyxis-lwip) as
`third_party/lwip`, [pyxis-fs](https://git.internal/PyxisOS/pyxis-fs) as `fs`, and
[pyxis-iegd](https://git.internal/PyxisOS/pyxis-iegd) as `third_party/iegd`.
The committed gitlinks select exact revisions;
normal builds never follow a remote branch or update the pin automatically.

## Checkout and updates

For an existing checkout:

```sh
git submodule update --init userspace ports third_party/lwip fs third_party/iegd
make -j12 image
```

New clones can use `git clone --recurse-submodules`. Run the submodule update
again after pulling a parent commit that changes the pin. Commit and publish
dependency changes in their repository before committing the corresponding gitlink
in Pyxis. A submodule checkout may be detached; create a branch there before
starting work. Local uncommitted source edits are usable for development;
the exported SDK manifest records dirty userland and filesystem inputs.

Kernel source builds require the pinned native filesystem format library.
`make -j16 fs-tools` builds `build/fs-tools/libnpfs-format.a`,
`mkfs.npfs`, `fsck.npfs`, `npfs-inspect`, the FAT32 reader/writer archives and
`fat32-inspect` / `fat32-write`.
The [FAT32 inspector](../../fs/docs/fat32-inspect.md) reads explicitly selected
GPT media without mounting or changing it; the shared engine has no allocator or
kernel dependencies.
When libfuse3 development files are installed, it also builds the optional Linux
`npfs-fuse` mount tool; that tool and its RAM journal replay are host-only.
Caelum owns the native cache and writer; the shared library owns codecs only.
SDK source builds also require it to export the target codec archive and header. See the
[native host-tool guide](../../fs/docs/npfs-host-tools.md) for source import,
inspection, extraction and structural checking.

The relative URL in `.gitmodules` resolves beside the Pyxis repository, using
the parent remote's host and transport. These repositories are public under
`PyxisOS`. CI uses the checkout action's automatic token for its own repository
and reads the public submodules without a custom source-read secret. Anonymous
Git reads must be allowed by the Forgejo instance. The workflow builds the
integrated kernel and ISO; it does not build a compiler or follow any
submodule's latest main.

A pull request that changes a gitlink must pin a commit that contains that
repository's current `main` and that has been published. The `pins` job
(`scripts/check-submodule-pins.sh`) fetches `main` and the pinned commit anonymously
and fails otherwise, naming the submodule, the pin and `main`; rebase the dependency
PR onto `main` and repin. It checks only gitlinks the pull request changed since it
left its base branch, so an unrelated PR is not blocked when a dependency's `main` moves
ahead, and it does not run for pushes or manual runs. A PR changing a gitlink is never
documentation-only, so the image build still runs. The check does not judge whether a
dependency PR is ready to merge: merge the dependency first, as in the PR's stated order.

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
| Pyxis | Kernel, public ABI/format headers, shared shebang parser, LLVM toolchain pin/build script/container, SDK export and image assembly |
| pyxis-userland | libc, libpyxis, libterm, native TLS adapter, startup/link support, applications, initial boot scripts and its TLSF vendor copy |
| pyxis-ports | Host Lua runner, pinned third-party recipes, ordered patches and staged executables/licenses and development libraries/headers |
| pyxis-lwip | Pinned lwIP source subset, license/provenance and any local upstream adaptations |
| pyxis-fs | Native filesystem format codecs, shared FAT32 engine and Linux formatter, checker, inspector/extractor and read-only FUSE mount |
| pyxis-iegd | Pinned Intel MIT driver source and reviewable native/eDP adaptations; Pyxis owns the kernel OAL/hardware integration |

`make sdk` exports headers, shared parser source and compiler settings, builds
the pinned userland runtime and target codecs, then installs startup, libraries,
linker support, the pinned target `libnpfs-format.a`, `libfat32-read.a`,
`libfat32-write.a` and `libfat32-mounted.a`,
and their public headers and licenses
into `build/sdk`. `make userspace` builds applications against that
SDK and the Lua, HTTP-parser and TLS libraries exported by `make ports`. Ports consumes only the SDK,
so building it before userland introduces no cycle. `make image`
includes both in initrd and ISO assembly. The parent passes
explicit output directories, preserving `build/runtime` and `build/userspace`.
See [SDK commands and layout](sdk.md) for standalone and focused builds.

The lwIP configuration, private headers, allocator/clock hooks and build rules
remain in Pyxis under `kernel/net/lwip`. Normal kernel builds consume the pinned
source submodule and record its revision/local state in the kernel bundle
manifest. CI's shared build job therefore checks out submodules. Local image
assembly can still use prebuilt bundles without source submodules.
See [the port boundary](../devices/lwip.md) for worker and packet ownership.

The filesystem build rules remain in Pyxis under `kernel/fs/build.mk`. Kernel
source builds compile only the format library with freestanding flags and private
includes. `scripts/npfs-sdk.mk` builds the same codecs against target SDK headers
for applications, with its own object directory and configuration tracking.
The kernel bundle records its revision and local state alongside lwIP.

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

## Intel driver source foundation

`third_party/iegd` pins upstream alpha.1 commit
`db7b38582c955269a38928dac5e36dd1a3998372` from the internal fork. The fork's
`main` already contained this exact commit and its upstream history when checked;
the annotated alpha.1 tag resolves to it. No base rewrite or forced fork push was
performed. The complete upstream MIT notices remain in the submodule. Adaptations
will be separate published commits, pinned through the normal main-ancestry check.

This source foundation does not compile IEGD objects, install its Linux UAPI,
load firmware, change device selection or activate a display driver. Existing
kernel builds do not consume it yet; no kernel-bundle dependency is claimed until
that build integration is implemented. No upstream source is fetched by the build.
A clone may contain the upstream tests, but they are not admitted to product
builds or installed images by this pin.

The [Intel eDP milestone](../wip/intel-iegd.md) defines the native kernel/OAL
boundary and ordered work. Later builds must explicitly admit the required
source files and notices; do not import all Linux IAL/render objects merely to
satisfy includes. Firmware is separately licensed and pinned when actually needed.
This step changes no SDK/public ABI or compiler-container pin.

## Filesystem repository

[PyxisOS/pyxis-fs](https://git.internal/PyxisOS/pyxis-fs) owns the shared native
filesystem encoding and Linux host tools. The old portable COW core, its host
writer/tools and Unity tests were retired when Caelum moved to the native format.
Its obsolete documents and measurements were also removed; Git retains their
history. They do not describe current interfaces or assign further writer work.

The authoritative [format](../../fs/docs/npfs-format.md) and
[host tools](../../fs/docs/npfs-host-tools.md) describe the codecs, source-importing
formatter, journal replay and structural checker. Pyxis owns capabilities,
namespace integration, the cache, writeback and kernel recovery. NPFS codecs have
no allocator, I/O or callback tables; the host, Caelum and native installer
provide their two memory symbols at link time. FAT32 engines borrow bounded
caller-owned workspace and backing callbacks. The SDK exports the target archives
and records its filesystem revision/local state. Ports consumes that complete
SDK without accessing filesystem sources.

The existing `Filesystem / host-contract (pull_request)` job in pyxis-fs and
Pyxis's `filesystem` dependency job now build the native library and host tools.
They retain their existing names and image-build gating. They run no retired
behavior tests; successful compilation is not recovery or structural-behavior
coverage. Ordinary host-tool use, interactive QEMU and debugger inspection provide
the task-specific validation recorded in the relevant PR and subsystem reference.
No new tests, workflows or compiler-container rebuild are required.

## Toolchain and remaining boundaries

The prebuilt [Pyxis toolchain](../../toolchain/README.md) uses an external SDK
sysroot. Ordinary runtime, header, startup or linker-script changes ship in the
SDK; compiler patches and target/runtime conventions can require rebuilding the
container. SDK manifests identify all three source repository revisions and compiler/host
identities without introducing an ABI compatibility version.

Userland-specific CI, dispatch orchestration and SDK artifact exchange remain
separate work. The owner configures dispatch and publishes compiler containers.
The [ports build](ports.md) consumes the SDK through the same boundary. LLD in the
`pyxis-llvm` fork writes PXE executables; dynamic linking and custom library
formats are not required here.
