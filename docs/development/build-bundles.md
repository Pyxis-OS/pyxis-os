# Build bundles

The root workflow uses one checkout and build job, publishing separate kernel,
SDK, userland and ports tar bundles plus the image artifact. One parallel Make
invocation builds the image and bundles through the existing dependency graph:
kernel and SDK can build independently, ports follows SDK, and userland follows
ports. Outputs stay in the same workspace, without intermediate artifact downloads
or repeated checkouts.

| Component / artifact | Local target | Tar payload |
| --- | --- | --- |
| kernel / pyxis-kernel | `make bundle-kernel` | `build/caelum.elf`, `build/kernel.config` |
| sdk / pyxis-sdk | `make bundle-sdk` | `build/sdk` |
| userland / pyxis-userland | `make bundle-userspace` | `build/userspace-root` |
| ports / pyxis-ports | `make bundle-ports` | `build/ports-root`, `build/ports-dev` |

Archives are written to `build/bundles/{kernel,sdk,userspace,ports}.tar`.
Each also contains its `build/bundle-info` record and payload checksum list.
SDK includes the headers, startup objects and runtime libraries built from the
pinned userland source, plus the pinned target npfs codec archive/header/license.
Its provenance includes the filesystem revision and local state. The userland
target builds applications, not those libraries.
The compiler stays in the prebuilt container and is not rebuilt by the workflow.
The SDK includes the host converter and is intended for the current Linux x86-64
build host. Consolidating the jobs does not change bundle formats or local reuse.

## Local reuse

Download the component artifacts from the same successful workflow run, unwrap
the Forgejo artifact downloads, and extract the contained tar files from the
repository root:

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
Use trusted project artifacts: tar extraction is not an untrusted-package loader.

`PREBUILT` is explicit and may select any subset. For a kernel-only edit, use
`PREBUILT="sdk userspace ports"`; for a port edit against an already selected SDK,
use `make bundle-ports PREBUILT=sdk`. With no selection, existing source builds
remain the default. INIT/WAD/demo overrides still apply during final assembly.
A fully bundled image needs neither initialized source submodules nor a target
compiler, but still needs the normal host assembly/boot tools and repository
bootloader/data files.

The ports bundle separates boot contents (`ports-root`) from development files
(`ports-dev`). Lua, picohttpparser, Mbed TLS, zlib and libpng provide static
libraries and headers under `ports-dev/lua`, `ports-dev/picohttpparser`,
`ports-dev/mbedtls`, `ports-dev/zlib` and `ports-dev/libpng`; these do not enter
the boot archive or the SDK. Standalone userland builds select the current
dependencies with `LUA_PREFIX`, `PICOHTTPPARSER_PREFIX` and `MBEDTLS_PREFIX`;
the image build also supplies `ZLIB_PREFIX` and `LIBPNG_PREFIX` for the planned
screenshot consumer. TLS consumers include the export's `share/mbedtls.mk` to
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

Normal source builds still use their existing dependencies. A TCC source/patch
change requires rebuilding TCC; CI reuse avoids repeating an already completed
build locally. There is no implicit network access or upstream checkout update
in `make run`. Automatic artifact discovery, local cache selection and reuse
across workflow runs remain [follow-up work](../wip/build-artifact-reuse.md).
