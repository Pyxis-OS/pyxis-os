# Building software on Pyxis

Status: **owner direction, 2026-10-08.** Not scheduled and not assigned; nothing
here authorizes code. It follows [Clang hosted on Pyxis](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
the third LLVM milestone, and needs its own proposal before any work starts.

## Direction

The original 2026-10-08 direction treated source builds as an interim before
binary program packages. That premise is superseded: the native `pkg` client
installs program and development packages, and installed `bin://` is their live
command view ([system layout](../userland/system-layout.md)). Running maintained
build recipes on Pyxis remains a separate, unscheduled direction:

- **Installed as binaries:** the kernel, the boot archive's rescue set, the
  headers and SDK, and the toolchain and build tools needed to build everything
  else.
- **Built on Pyxis:** the ports and every userland program outside the rescue
  set, from the same pinned sources and recipes the host build uses today.

This would trade build time for a system that builds itself. Its outputs could
feed the existing package manager; binary packages are already available and
do not depend on an on-device builder.

## Why it fits

- **The recipe format exists.** Each port pins its source (an internal mirror and
  commit, or an archive and SHA-256), applies ordered patches and runs a small Lua
  build file. The [ports runner](../development/ports.md#building-and-packaging)
  runs on the host today; Lua already runs on Pyxis.
- **The layout separates the two halves.** The default core archive keeps
  the [rescue set](../userland/system-layout.md#programs) in `boot://`;
  other programs are packages projected into installed `bin://`. A source builder
  would need to produce verified package inputs rather than revision directories.
- **Builds suit capabilities.** A build can receive its source and build
  directories, the SDK read-only, and network access only while fetching. The
  host recipes already build offline after fetching, for example with CMake's
  `FETCHCONTENT_FULLY_DISCONNECTED`.
- **Licences get simpler.** A program built on the user's own machine is not
  distributed. [DevilutionX](../userland/devilutionx.md) links GPL code under a
  non-commercial licence, so its binary must not be shared; its recipe and
  patches can be, and a local build needs no exception.

## What it needs

- **Hosted Clang and LLD,** the third LLVM milestone. The
  [hosted Clang tools](../development/hosted-tools.md) record the native compile, link and
  capacity measurements and the delivered prerequisite order.
- **Where the toolchain lives.** The boot archive stays in RAM for the kernel's
  lifetime, and an install copies it to a 512 MiB ESP. A host Clang binary today
  is about 150 MB, plus LLD at about 85 MB; a Pyxis build will differ, but the
  toolchain clearly does not belong in the archive. It needs a pool volume of its
  own, installed and updated like `bin`.
- **Build tools on Pyxis:** `make`, `patch`, `sha256sum` and `tar` (BusyBox and
  sbase may cover some), and either CMake or Ninja. fmt, mbedtls, fastfetch and
  DevilutionX use CMake, itself a large C++ port. Converting some recipes to plain
  Make is an alternative.
- **Fetching.** [HTTP resources](../userland/http-fetch.md) supply complete GET
  snapshots, explicit success/failure and staged POSTs. Large downloads require
  explicitly private npfs backing; RAM has a configured aggregate budget, with
  no transport-wide body cap. Source fetch and ports consume that provider
  rather than owning HTTP engine tasks. Verify the exact
  commit or SHA-256 and publish a source only after complete transfer.
  Archives with checksums are easier than Git on Pyxis, but archives a mirror
  generates from Git commits may not be byte-stable, which a SHA-256 pin needs.
  [Git on Pyxis](../userland/git.md) now supplies the go-git-based HTTPS
  clone/push/pull and local workflow, with recorded QEMU qualification. Its
  restricted symlink profile and command surface remain distinct from adapting
  recipe fetching and its source-authority policy.
- **Running recipes.** [Native command execution](../userland/native-jobs.md)
  supplies argv launch, selected streams, capture/deadlines, pipelines and actual
  result/cleanup for Lua and the ports runner, replacing its host
  `os.execute`/`io.popen` backend. This shared milestone can deliver independently
  of source builds. Native recipe adaptation, hosted build tools and selected
  source/build/SDK authority remain runner work; tools such as make that launch
  children require [nested execution groups](nested-execution-groups.md), a parked
  required direction. Running real ports recipes on Pyxis depends on that gate.
  No POSIX shell is implied.
- **Publishing build outputs.** Installed `bin://` is a read-only package command
  view, not a directory for a builder to write. A builder needs explicit staging
  and publication authority. How it feeds the existing verified package/set
  mechanism still needs a proposal.
- **Build space and speed.** Build directories belong in `tmp://`, which is RAM,
  with only results written to the pool. `make -j` runs separate processes, which
  Pyxis already spreads across CPUs; a single compiler process has one thread.

## Reaching the sources

The source servers, `git.internal` (Forgejo and its `mirrors/` organization)
and `repo.internal` (proxied release downloads), are reachable only on the
owner's tailnet. Pyxis machines sit on the home LAN and have no tailnet client.

**Planned direction (owner, 2026-10-09):** a read-only LAN gateway on a machine
that is on both networks, today the owner's desktop. It forwards only:

- Git smart HTTP reads for the pinned repositories: the `info/refs` service
  discovery GET and the `git-upload-pack` POST. No push or other Forgejo paths.
- `repo.internal` release downloads, the byte-stable archives the recipes
  already pin by SHA-256.

The host firewall admits only the Pyxis machines' reserved LAN addresses. The
gateway serves HTTPS with a certificate Pyxis trusts through the existing
explicit CA policy; verification is never disabled. It holds no Pyxis
credentials, and Pyxis gains no write path to the servers. Integrity does not
depend on the gateway: Git fetches verify the exact pinned commit and archives
their pinned SHA-256. Recipe URLs need a configurable mirror base rather than
the hard-coded upstream URLs they have today. The gateway is a separate deployment
prerequisite: [native HTTP requests](../userland/http-fetch.md) supplies the Pyxis
GET/POST transport and completion contract, not a tailnet client or gateway setup.

Until a builder fetches on its own, the host can push pinned sources to Pyxis
with `xfer` over the remote terminal. Joining the tailnet directly depends on
[Tailscale on Pyxis](toolchains-and-runtimes.md#homelab-administration-over-tailscale),
which needs the Go runtime first; a WireGuard tunnel to the gateway machine is
an option only if a Pyxis machine needs sources away from the home LAN.

## Updates

The original direction rebuilt programs for every kernel revision. The current
[package ABI model](pkg-upgrade.md#decided) instead names supported release ABIs;
development builds remain bound to their exact build. A future source-builder
proposal must define when to rebuild against a changed SDK and how verified
outputs enter the package set. Recipes must retain exact source pins rather
than following upstream branches.

## Possible order

1. Hosted Clang: compile, link and run one C program inside Pyxis.
2. Build one small port by hand inside Pyxis, such as Lua.
3. Run the ports runner inside Pyxis, then build userland's programs with its
   Makefiles.
4. Ninja or CMake, for the CMake-based ports.
5. Package build-output staging, publication and rebuild policy on update.

## Open questions

- Whether a source-built distribution changes the existing online base-package
  Install workflow, and what remains usable before its first build finishes.
- How toolchain and SDK package versions constrain source-built outputs.
- Which authority publishes verified outputs to the package set, and how a
  failed or interrupted build rolls back.
- How build failures, logs and partial results are reported to the user.
- Whether some large ports stay binary, and how that interacts with licences
  such as DevilutionX's.
