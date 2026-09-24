# Build bundles

The root workflow has separate kernel, SDK, userland and ports jobs, each
publishing its own tar bundle. Kernel and SDK builds can run independently;
ports consumes the SDK bundle, then userland consumes both SDK and ports. The
final image job downloads all four and assembles the ISO without compiling them again.

| Job / artifact | Local target | Tar payload |
| --- | --- | --- |
| kernel / pyxis-kernel | `make bundle-kernel` | `build/caelum.elf` |
| sdk / pyxis-sdk | `make bundle-sdk` | `build/sdk` |
| userland / pyxis-userland | `make bundle-userspace` | `build/userspace-root` |
| ports / pyxis-ports | `make bundle-ports` | `build/ports-root`, `build/ports-dev` |

Archives are written to `build/bundles/{kernel,sdk,userspace,ports}.tar`.
Each also contains its `build/bundle-info` record and payload checksum list.
SDK includes the headers, startup objects and runtime libraries built from the
pinned userland source; the userland job builds applications, not those libraries.
The compiler stays in the prebuilt container and is not rebuilt by these jobs.
The SDK includes the host converter and is intended for the current Linux x86-64
build host. No compiler-container update is needed for this job split.

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
(`ports-dev`). Lua provides its static library and public headers under
`ports-dev/lua`; these do not enter the boot archive or the SDK. Standalone
userland builds receive this prefix through `LUA_PREFIX`. SDK runtime builds
remain independent of ports, so there is no dependency cycle.

Recorded payload checksums do not depend on timestamps. Application and ports
bundles record the exact SDK content identity; image assembly also checks that
the kernel and SDK ABI/format headers match. Userland also records and verifies
the Lua development files used for its session launcher. Mismatches fail rather than silently
building a different component. Records include source revisions, dirty state
and local change hashes, selected builder image, and relevant compiler/flag
provenance. These checks establish matching inputs, not a cryptographic trust
boundary or proof that arbitrary kernel implementation changes preserve behavior.

Normal source builds still use their existing dependencies. A TCC source/patch
change requires rebuilding TCC; CI reuse avoids repeating an already completed
build locally. There is no implicit network access or upstream checkout update
in `make run`. Automatic artifact discovery, local cache selection and reuse
across workflow runs remain [follow-up work](wip/build-artifact-reuse.md).
