# Build bundles

Bundles are local only. The root workflow builds the image in one checkout and
build job and uploads nothing (chronium, 2026-10-20). A bundle is a tar of one
component's build that a developer packs on their own machine with `make
bundle-*` and reuses in later builds. One parallel Make invocation builds the
image through the dependency graph: kernel and SDK can build independently, ports
follows SDK, and userland follows ports.

| Component / bundle | Local target | Tar payload |
| --- | --- | --- |
| kernel / kernel | `make bundle-kernel` | `build/caelum.elf`, `build/kernel.config`, `build/kernel-{random,amd,intel,wifi}-NOTICE` |
| sdk / sdk | `make bundle-sdk` | `build/sdk` |
| userland / userspace | `make bundle-userspace` | `build/userspace-root` |
| ports / ports | `make bundle-ports` | `build/ports-root`, `build/ports-dev` |

Archives are written to `build/bundles/{kernel,sdk,userspace,ports}.tar`.
Each also contains its `build/bundle-info` record and payload checksum list.
SDK includes the headers, startup objects and runtime libraries built from the
pinned userland source, plus the pinned target npfs codec archive/header/license.
Its provenance includes the filesystem revision and local state. The userland
target builds applications, not those libraries.
The compiler stays in the prebuilt container and is not rebuilt by the workflow.
The SDK includes the host converter and is intended for the current Linux x86-64
build host.

## Local reuse

Pack the bundles you want to keep with `make bundle-kernel bundle-sdk
bundle-ports bundle-userspace`, or take tar files you packed earlier, and extract
them from the repository root:

```sh
tar -xf /path/to/kernel.tar
tar -xf /path/to/sdk.tar
tar -xf /path/to/userspace.tar
tar -xf /path/to/ports.tar
make run PREBUILT="kernel sdk userspace ports" CPUS=4
```

Extraction replaces files in these disposable build directories. When replacing
an existing bundle, remove its payload directory first (for example
`rm -rf build/sdk`) so files removed in the new bundle cannot survive extraction.
The verifier rejects extra files as well as missing or changed files.
Use only bundles you packed yourself or trust: tar extraction is not an untrusted-package loader.

`PREBUILT` is explicit and may select any subset. For a kernel-only edit, use
`PREBUILT="sdk userspace ports"`; for a port edit against an already selected SDK,
use `make bundle-ports PREBUILT=sdk`. With no selection, existing source builds
remain the default. `INIT` still applies during final assembly; game data and fonts are packed
separately (see [package inputs](../../packages/README.md)).
A fully bundled image needs neither initialized source submodules nor a target
compiler, but still needs the normal host assembly/boot tools and repository
bootloader/data files.

The ports bundle separates boot contents (`ports-root`) from development files
(`ports-dev`). Lua, picohttpparser, Mbed TLS, zlib, libpng and fmt provide
static libraries and headers under `ports-dev/lua`, `ports-dev/picohttpparser`,
`ports-dev/mbedtls`, `ports-dev/zlib`, `ports-dev/libpng` and `ports-dev/fmt`;
these do not enter the boot archive or the SDK. Standalone userland builds select the current
dependencies with `LUA_PREFIX`, `PICOHTTPPARSER_PREFIX` and `MBEDTLS_PREFIX`;
the image build also supplies `ZLIB_PREFIX` and `LIBPNG_PREFIX` for the
screenshot command. TLS consumers include the export's `share/mbedtls.mk` to
use the matching configuration defines and ordered libraries. SDK runtime builds
remain independent of ports, so there is no dependency cycle.

Recorded payload checksums do not depend on timestamps. Application and ports
bundles record the exact SDK content identity; image assembly also checks that
the kernel and SDK ABI/format headers match. Userland also records and verifies
the complete ports development tree used for its session launcher, HTTP library
and native TLS adapter. Mismatches fail rather than silently
building a different component. Records include source revisions, dirty state
and local change hashes, selected builder image, and relevant compiler/flag
provenance. Kernel bundles also include the complete effective Kconfig
configuration and record its content hash. A selected prebuilt kernel uses that
configuration; local `.config` edits apply only to kernel source builds.
These checks establish matching inputs, not a cryptographic trust
boundary or proof that arbitrary kernel implementation changes preserve behavior.

Kernel source builds require the pinned lwIP and filesystem-format submodules. Their
revisions, local state and change hashes are recorded in the kernel bundle;
SDK source builds also require the filesystem-format submodule and record it
in SDK provenance. `make fs-tools` remains an opt-in host build outside these bundles.

Normal source builds still use their existing dependencies; a reused bundle
avoids repeating an already completed build locally. There is no implicit network access or upstream checkout update
in `make run`. Automatic bundle discovery and local cache selection remain
[follow-up work](../wip/later-os-directions.md#package-manager).
