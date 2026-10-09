# Building software on Pyxis

Status: **owner direction, 2026-10-08.** Not scheduled and not assigned; nothing
here authorizes code. It follows [Clang hosted on Pyxis](toolchains-and-runtimes.md#llvmclang-transition-and-hosting),
the third LLVM milestone, and needs its own proposal before any work starts.

## Direction

Until Pyxis has a proper package manager, an installed system builds its
software from source, as Gentoo does:

- **Installed as binaries:** the kernel, the boot archive's rescue set, the
  headers and SDK, and the toolchain and build tools needed to build everything
  else.
- **Built on Pyxis:** the ports and every userland program outside the rescue
  set, from the same pinned sources and recipes the host build uses today.

This trades build time for a system that builds itself. It is interim: binary
packages can come later, and they are cached outputs of the same recipes, so the
on-device builder becomes the build side of the eventual package manager rather
than throwaway work.

## Why it fits

- **The recipe format exists.** Each port pins its source (an internal mirror and
  commit, or an archive and SHA-256), applies ordered patches and runs a small Lua
  build file. The [ports runner](../development/ports.md#building-and-packaging)
  runs on the host today; Lua already runs on Pyxis.
- **The layout already separates the two halves.** An installed archive keeps
  only the [rescue set](../userland/system-layout.md#programs) in `boot://`;
  every other program lives in `bin://`, one directory per kernel revision.
  Source builds would produce that directory instead of the installer copying it.
- **Builds suit capabilities.** A build can receive its source and build
  directories, the SDK read-only, and network access only while fetching. The
  host recipes already build offline after fetching, for example with CMake's
  `FETCHCONTENT_FULLY_DISCONNECTED`.
- **Licences get simpler.** A program built on the user's own machine is not
  distributed. [DevilutionX](../userland/devilutionx.md) links GPL code under a
  non-commercial licence, so its binary must not be shared; its recipe and
  patches can be, and a local build needs no exception.

## What it needs

- **Hosted Clang and LLD,** the third LLVM milestone.
- **Where the toolchain lives.** The boot archive stays in RAM for the kernel's
  lifetime, and an install copies it to a 512 MiB ESP. A host Clang binary today
  is about 150 MB, plus LLD at about 85 MB; a Pyxis build will differ, but the
  toolchain clearly does not belong in the archive. It needs a pool volume of its
  own, installed and updated like `bin`.
- **Build tools on Pyxis:** `make`, `patch`, `sha256sum` and `tar` (BusyBox and
  sbase may cover some), and either CMake or Ninja. fmt, mbedtls, fastfetch and
  DevilutionX use CMake, itself a large C++ port. Converting some recipes to plain
  Make is an alternative.
- **Fetching.** HTTPS already works natively. Archives with checksums are easier
  than Git on Pyxis, but archives a mirror generates from Git commits may not be
  byte-stable, which a SHA-256 pin needs. The [Git investigation](git-on-pyxis.md)
  compares a restricted Git CLI with a native libgit2 fetch command, records
  current SDK/filesystem gaps, and proposes a first task for owner review.
- **Writing `bin://`.** Today only the installer writes it, and spaces receive it
  read-only. A builder needs a staged directory, verification and a switch, like
  [system updates](../userland/system-updates.md), with new authority to do so.
- **Build space and speed.** Build directories belong in `tmp://`, which is RAM,
  with only results written to the pool. `make -j` runs separate processes, which
  Pyxis already spreads across CPUs; a single compiler process has one thread.

## Updates

System updates keep the binary path for the kernel, rescue set, SDK and
toolchain. Programs then rebuild against the new SDK: fetch the pinned recipes
for the new revision, rebuild what changed, and switch only after every build
succeeds. The program revision always matches the installed SDK and kernel
revision, and builds never follow upstream branches on their own.

## Possible order

1. Hosted Clang: compile, link and run one C program inside Pyxis.
2. Build one small port by hand inside Pyxis, such as Lua or Kilo.
3. Run the ports runner inside Pyxis, then build userland's programs with its
   Makefiles.
4. Ninja or CMake, for the CMake-based ports.
5. A toolchain volume, staged `bin://` builds, and rebuilding on update.

## Open questions

- What the installer offers before the first build finishes: a system with only
  the rescue set, or optional prebuilt programs as a cache.
- Whether the toolchain volume follows the kernel revision like `bin`.
- Which authority may write `bin://`, and how a failed or interrupted build
  rolls back.
- How build failures, logs and partial results are reported to the user.
- Whether some large ports stay binary, and how that interacts with licences
  such as DevilutionX's.
