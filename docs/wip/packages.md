# Packages and upgrades

The [0.0.10 upgrade reference](../userland/package-upgrades.md) supersedes the rollback
and predecessor-retention portions of this historical task record.

Status: **data and program packages implemented.** The owner requested this plan
on 2026-10-18 and accepted its three [decisions](#decisions) that day.
[Slice 1](#slice-1-data-packages) is complete; program packages, the core archive
and online Install are delivered in [package upgrades](pkg-upgrade.md#step-2-programs-out-of-the-archive-implemented).
`pkg upgrade` has recorded QEMU qualification; its native ThinkPad upgrade gate
remains open. Dynamic linking and building packages on Pyxis remain later work.

The original problem, owner direction, proposal and staged plan below preserve
the earlier design context. Their revision-bound programs, unsigned repositories
and reboot-switched package sets are superseded by the
[ABI/core/updater decisions](pkg-upgrade.md) and the current
[package formats](../interfaces/packages.md).

**Superseded in part (chronium, 2026-10-22):** packages are read as
`pkg://ID`, not `ID://` or `"package"` directory grants, and bundles list theirs
in `"packages"` ([the `sdk://` view](sdk-view.md), task 2c-1). The `ID://`
roots, `package_roots` and `"package"` grants described below are history.

## Problem

Historical motivation, measured on 2026-10-18; not the current archive or
installed layout.

- **The ESP is full.** The personal boot archive is about 215 MB, and the
  installer's ESP is 512 MiB ([installer](../userland/installer.md)). `stick-deploy`
  keeps a baseline copy in `boot/installed/` and writes `initrd.cpio.deploy-new`
  before renaming it, so a deploy briefly holds three archives (about 645 MB) and
  failed on 2026-10-18. Installer task 5's immutable boot attempts also keep the
  current and previous pairs beside new staging
  ([installer TUI](installer-tui.md)), so shrinking the archive is a prerequisite
  there too.
- **Most of the archive is data.** The ordinary archive staged on 2026-10-18 is
  88.9 MB: `nvim.pxb` 30.2 MB, root programs 19.1 MB, Quake shareware 18.7 MB,
  `sdk/usr` 4.2 MB, Doom shareware 4.2 MB. The personal build adds 135.2 MB of
  licensed game data (Quake paks 53.0, Duke 3D 44.4, Diablo `spawn.mpq` 25.4, Doom
  12.4) and, since #963, 13 MB of [fonts](../userland/fonts.md#installed-collection)
  in the `fonts` data package after task 5.
- **Programs are tied to the kernel revision.** Update copies every program into
  `bin/REVISION` and keeps two complete revisions, so no program changes without
  a new kernel and ESP, and only from live media. Resolved by step 2 task 3 of
  [package upgrades](pkg-upgrade.md): programs are packages.
- **Toolchains are installed by hand.** The hosted Clang payload is uploaded with
  `xfer`, unpacked with `tar` and switched with `mv` under `home://toolchain/clang`
  ([clangd](../userland/clangd.md)); old trees stay until `rm -r` removes them
  ([rm](../userland/rm.md)).

## What exists today

| Piece | Behaviour relevant here | Reference |
| --- | --- | --- |
| Bundles | A `.pxb` is a directory with `manifest.json` and `app/`; the manifest names entries, a stack, grants and read-only `resource_dirs` roots. A DIRECTORY grant may name a `source` and a `root` alias; only trusted configuration or shell mount ownership makes a source exist. | [program bundles](../userland/program-bundles.md) |
| Program lookup | Held command-directory lists with live package projections and owned selections, used by the shell, Lua and libuv; no implicit boot, cwd or development-catalog search. | [command lists](../interfaces/command-search.md) |
| Update | From live media, publish the kernel and archive through the shared boot publisher, retaining previous and rescue entries. Installed packages and owner configuration remain on the pool; programs use the live package command view rather than new `bin/REVISION` directories. | [system updates](../userland/system-updates.md) |
| Boot archive | The default `core` distribution contains boot init, the rescue set, firmware and installer inputs. SDK assets, fonts and games are packages. `full` retains the development staging trees; optional personal builds are separate. | [system layout](../userland/system-layout.md) |
| npfs | `RENAME` is atomic within one mount; callers explicitly sync to make it durable (COMMITTED). A file may replace a file; a directory may replace only an empty directory. Regular-file hard links exist within one npfs volume; no symbolic links. | [directories](../interfaces/directories.md#atomic-rename-and-directory-moves), [npfs format](../../fs/docs/npfs-format.md) |
| Links | Native symbolic-link creation/following is unavailable. Git uses `core.symlinks=false`, preserving link blobs as regular files containing their targets; native links are not a prerequisite for that profile. | [directories](../interfaces/directories.md), [libc](../userland/libc-portability.md#name-limits-and-symbolic-links), [Git](../userland/git.md) |
| HTTP | Responses are completed, re-readable snapshots. RAM backing defaults to 64 MiB per provider; large bodies need private npfs backing. One 30 s deadline by default, explicit longer values allowed; no resume; one exchange at a time. | [HTTP transactions](../interfaces/http-transactions.md) |
| Download cache | `repo.internal` proxies release downloads for host builds; nothing on Pyxis uses it. | [ports](../development/ports.md) |
| Toolchain delivery | Task 4 replaces manual payload publication with the `clang-hosted` package and its declared command exports. | [hosted tools](../development/hosted-tools.md#pool-and-authority) |
| Executables | P1F is fixed-address with no symbols, loaded by the kernel image loader. The only written P2F idea is an undecided P1F superset with an optional symbol table, for backtraces. Nothing defines exported symbols, P1D or P2D. New formats go through the launcher and loader path. | [P1F](../../include/pxe/p1f.h), [backtraces](https://git.internal/PyxisOS/pyxis-os/issues/1337), [subsystems](subsystems-per-space.md) |

## Owner direction (2026-10-18)

Historical direction. The ABI, signing, archive and updater contracts are now
defined by [package upgrades](pkg-upgrade.md); dynamic linking remains future work.

- Plan a package manager and updater. Ready-built binary packages first; building
  from source later, once Git and make or CMake exist on Pyxis.
- Signatures and trust are out for now: there are no external consumers.
- `bin/REVISION` goes eventually. Each program gets its own version. Long term,
  the kernel, SDK, userland and ports become standalone components blended into
  an installed system; gitlinks remain how a consistent set is built.
- No ABI stability policy now. Programs are decoupled from kernel revisions
  through dynamic linking in Pyxis-native formats, not ELF shared objects: a P2F
  superset that exports symbols, and P1D/P2D for dynamically linked programs.
- Names, proposed with the owner's agreement: `.pxe` executables and `.pxl`
  libraries (renamed from `.pxo`, which would read like a compiler object), with
  versioned `.pxl.REVISION` files reached through stable names.
- Real native links are a prerequisite for those stable library names. Git's
  delivered restricted profile does not require native symbolic links.
- Order: (1) data packages: game data, fonts, toolchain payloads; (2) program
  packages still tied to a system release, rebuilt per release but delivered as
  packages; (3) a dynamic linking milestone that unties programs from kernel
  revisions.
- The upgrader is a separate program, not the installer. It upgrades through the
  package manager and also replaces the kernel and boot archive, using the owned
  ESP view being built in installer task 5 (#797).

## Proposal

Historical proposal, refined by the slices below and superseded where noted at
the top. It does not override the current format or updater reference.

### Packages and repositories

A package is an immutable directory tree with a manifest: its ID, its own
version, its kind (`data`, `program` or `toolchain`), every file's size and
SHA-256, and exact dependencies. A repository is static files over HTTP: an
index listing each package version with its archive size and SHA-256, and one
archive per version. Any HTTP server can serve it.

- **Integrity, recommended:** SHA-256 for each archive in the index and each file
  in its manifest, as Update already does for programs. This catches truncation
  and corruption. The original unsigned-only proposal is superseded by
  [signed indexes](../interfaces/packages.md#signed-indexes); unsigned development
  repositories require an explicit configuration opt-in.
- **Archives:** plain USTAR tar, which Pyxis `tar` reads and the payload producer
  already writes. Not the parked `.pxa` ZIP, which stays a stored-launch format.
  No compression in slice 1.
- **Downloads:** archives go to private npfs HTTP backing with an explicit
  deadline derived from the index size, as the 2026-10-17 payload publish needed.
  The repository server answers byte ranges, but the HTTP provider does not
  request them, so there is no resume yet. A 369 MB toolchain payload takes
  minutes at the measured rates.

### Layout and the current set

A new `packages` volume on the pool, created by Install and Update like `home`:

```text
store/ID/VERSION/     immutable package trees, each with its manifest
sets/N.lua            installed sets: exact package versions, as Lua literal data
current               FILE naming the active set
staging/              downloads and unpacking in progress
```

The client unpacks into `staging/`, verifies every file against the manifest,
syncs, and moves the tree to `store/ID/VERSION` with one directory `RENAME`.
It writes the next set record, syncs, writes `current.new`, and renames it over
`current`. That rename is the atomic visibility switch; an explicit sync makes
it durable. Rollback publishes the previous set through the same switch. The
pointer must be a file, because a directory can replace only an empty one. The client keeps the current and
previous sets and removes unreferenced store trees after the next boot, so no
running program still holds them; it removes trees entry by entry itself, as the
installer does.

A switch affects new spaces and new launches only. Running programs keep the
views they hold, as with Update today.

### Reaching package files

Data access uses explicit roots, independently of the
[command-directory list](../interfaces/command-search.md). A space receives the
`packages` volume as one read-only directory source. A bundle names the
package it needs in a DIRECTORY grant with a `package` ID and its own `root` alias, so a port might read
`doom_data://DOOM.WAD`; the bundle launcher resolves the ID through the current
set at each launch. A space can also name a package as a root in its
configuration, for tools used from the shell, resolved when the space starts.
Each game port and libfont's callers then read from their declared root instead
of `boot://share/`. The [format reference](../interfaces/packages.md#reaching-package-files)
has the exact rules.

### Personal and licensed data

A LAN-only repository on horse, built by a host packer from the owner's local
inputs selected by [local packer recipes](../../packages/README.md) and served over HTTP on the LAN. It is never published,
staged in shared locations, built in CI or committed. The client takes a list of
repository URLs from system configuration, so a personal repository sits beside
a public one.

### The boot archive

Licensed game data is packaged from the owner's local inputs. The default core
archive keeps boot init, the rescue set, firmware and installer inputs; SDK
assets and game data come from packages. Installed configuration lives on the
pool. The full development distribution and optional personal builds are
separate from that default. The volatile live-package workflow below records
the earlier slice-1 design; see [system layout](../userland/system-layout.md)
for the current distributions.

**Game packages (owner design, 2026-10-20):** games become whole packages, program
plus data, named after the game: `doom` and `quake`. The separate `doom-data` and
`quake-data` data packages are not continued. Both repositories offer the game
packages: the public one with shareware data, the personal one with the owner's
retail data. Accepted defaults:

1. When several configured repositories offer the same ID, `pkg` on a console
   shows a numbered choice (1, 2, …, n), as other package managers do. Outside a
   console it refuses and lists the choices. The caller selects explicitly with
   `ID@VERSION` or a repository-qualified `REPO/ID`, for example `personal/doom`.
2. The chosen repository is recorded with the installed package. Later updates
   never silently switch its origin.
3. Until game packages existed, the game programs shipped in the image and
   required their data packages (`doom` required `doom-data`); stage 2 task 3
   removed them. 0.0.7 closes slice 1 as planned. Game packages are the first program-packages
   work ([stage 2](#staged-plan)) right after 0.0.7; the
   [stage 2 proposal](#stage-2-game-packages-proposal-2026-10-20) details it.

Game packages contain programs, which stay tied to a release until
[dynamic linking](#dynamic-linking-later-milestone-outline-only). Each repository
therefore holds a build per release, and `pkg` refuses a build made for another
release.

Licensed data stays packages-only. Fonts stay the `fonts` package; they do not
return to the image (owner, 2026-10-20).

### Client and upgrader

The package client lists, installs, removes, switches, rolls back and verifies.
It holds only an HTTP endpoint and write authority on the `packages` volume, which
nothing else writes.

| Option | For | Against |
| --- | --- | --- |
| C client now (accepted) | Runs on existing native libraries: the HTTP provider, npfs and bundles. Small, available in slice 1. | A second client later if Go takes over. |
| Go client | The owner's preferred fit for this kind of program. | Needs the [Go runtime port](go-runtime.md) first, which isn't assigned. |

The accepted path keeps every format language-neutral (Lua literal records read
as data, line-based inventories and USTAR archives), so a later Go client is a
drop-in replacement.

#### Upgrader: `pkg upgrade` (accepted direction, owner, 2026-10-20)

This subsection preserves the 2026-10-20 design. Its reboot-switched package
sets and placeholder verifier were superseded by [package upgrades](pkg-upgrade.md).
The current upgrader uses signed release indexes and the shared boot publisher;
QEMU upgrade/new/previous/rescue boots are qualified, while the native ThinkPad
upgrade check remains open. The paragraphs and accepted decisions below are
historical, not the implemented updater contract.

`pkg upgrade` updates the running installed system in place. A release package,
for example `pyxis-system@VERSION`, carries the kernel ELF and the boot archive
cpio. `pkg` fetches and verifies it like any other package, then writes it to
the ESP of the install that booted, using the boot-bound identity. There is no
PXE boot, no separate update path and no choice of target.

The write reuses the installer's [publisher](../userland/installer.md#shared-mounted-esp-payload-stager):
a scratch publication, previous and rescue entries retained, and the boot menu
switched only once publication is complete. Programs are release-tied for now,
until [dynamic linking](#dynamic-linking-later-milestone-outline-only). The
release's package set is therefore staged and switched at the same reboot.
Because the switch happens at reboot, running programs never see their package
set change, and no running-program lifetime contract is needed. Booting the
previous entry rolls back both the kernel and archive and the package set.

The [details](pkg-upgrade.md) (three decisions accepted, owner, 2026-10-20; the rest
a proposal): release package format, booted-ESP identity, the ESP-write grant, set
staging, rollback, failures and qualification.

Accepted decisions:

1. **One publisher.** `pkg upgrade` and the installer share one publisher. The
   installer keeps Install for fresh media. Its Update tab retires once
   `pkg upgrade` is qualified.
2. **Trust.** `verify_release_signature()` exists from the first version and
   returns VERIFIED, REFUSED or NOT_VERIFIED. There is no verifier yet, so it
   returns NOT_VERIFIED until one exists.
   - Each repository's configuration states its trust. NOT_VERIFIED is accepted
     only from a repository explicitly configured as unsigned. Each such upgrade
     prints one visible line, for example
     `upgrade: horse/public is unsigned (trusted by configuration)`.
   - Any repository not marked unsigned is refused.
   - Signed releases are required before any non-LAN repository is used.
   - Only `pkg` holds the ESP-write grant. The upgrade asks for console
     confirmation, defaulting to No, and never reports a blanket success.
3. **Schedule.** After 0.0.7, in stage 2 with the game packages. The 0.0.7
   [installer gate](installer-tui.md) stays, because it qualifies the shared
   publisher.

### Native links (prerequisite milestone)

[Native npfs hard links](../interfaces/native-hard-links.md) are implemented and
natively qualified. Multiple regular-file names in one volume share identity and
contents; counted lifetime, lazy first-use feature activation and count-aware
fsck/replay are provided. Source LOOKUP/READ_FILES/WRITE_FILES and destination
CREATE authorize linking; links carry no persistent rights ceiling. Package
slices 1 and 2 still do not depend on links; stable library names do.

- **Symbolic links:** a new inode kind holding a target, behind a `required`
  feature bit set only when a pool contains one, so older readers refuse such a
  pool instead of misreading it. Storing and reading one (what Git needs) is
  simple. Following one is the capability question: a relative target must not
  climb above the directory the lookup started from, and an absolute `ROOT://`
  target can only mean the caller's own root of that name, never authority the
  link's creator held. Following stays an explicit per-lookup choice; the default
  remains never.

For stable library names, either kind works. With hard links, `libc.pxl` and
`libc.pxl.REVISION` are one file, and the stable name moves atomically: link a
new name, then rename it over `libc.pxl` and explicitly sync. Accepted: hard links
first, with symbolic links (store, read, explicit follow) as a later step for Git
and ports.

### Dynamic linking (later milestone, outline only)

- **Formats:** P2F, a P1F superset with a symbol table that can also export
  symbols; P1D/P2D for position-independent programs and libraries carrying
  relocations and needed-library records. `.pxe`, `.pxl`, `.pxl.REVISION`.
- **Loading:** a userspace loader in the launcher path, as the subsystems
  invariant requires; the kernel keeps loading P1F, including the loader itself.
- **ABI names:** a system library's ABI name is what packages record in
  `abi`, and the system's list grows and shrinks by the
  [release-ABI policy](pkg-upgrade.md#decided).
- **Toolchain and SDK:** LLD writes the new formats; position-independent builds
  of libc, libpyxis, libterm, libfont and libc++; relocation kinds; a TLS model
  beyond today's static local-exec; library naming and versions; the SDK exports
  `.pxl` files.
- **Authority:** a program reaches its libraries through its bundle or declared
  package roots, never through an environment search path.

None of this is designed here.

## Staged plan

1. **Slice 1, data packages:** package and index formats with a host packer; the
   `packages` volume and set switching; the C client; boot init publishing package
   sources; ports and libfont reading declared roots; game data, fonts and the
   hosted toolchain payload as packages; the LAN repository on horse; the archive
   shrunk so `stick-deploy` fits again.
2. **Program packages per release:** release-versioned program packages, the
   command index generated at set switch, `bin/REVISION` retired, and the upgrader.
   It starts with the [game packages](#the-boot-archive) `doom` and `quake`
   (program plus data, one build per release) right after 0.0.7. The game
   programs then leave the image entirely (owner direction, 2026-10-20).
   [Proposal](#stage-2-game-packages-proposal-2026-10-20): decisions accepted (owner, 2026-10-20).
3. **Native links:** [hard links are complete](../interfaces/native-hard-links.md).
   Stage 4's stable `.pxl` names use them; slices 1 and 2 do not depend on them.
   Symbolic links follow later.
4. **Dynamic linking:** programs untied from kernel revisions and versioned on
   their own; `.pxl` libraries under stable names.
5. **Source builds:** packages built on Pyxis once Git and make or CMake exist.

## Decisions

Accepted by the owner, 2026-10-18, all as proposed:

1. **Package layout and the current set:** a separate `packages` volume with
   immutable `store/ID/VERSION` trees, set records and a `current` file switched
   by rename, keeping current and previous sets and collecting garbage after
   boot. Programs reach package files through the existing DIRECTORY grants.
   Rejected: packages inside the `bin` volume, which mixes per-kernel and
   per-package lifetimes; per-package pointers, which lose whole-set atomicity
   and rollback.
2. **Native link kind for stable names:** hard links first, with a link count in
   the inode reserve behind a `read_only_compatible` feature bit; symbolic links
   later, with explicit, capability-bounded following. Rejected: symbolic links
   first, or both in one milestone.
3. **Client language:** a C client now on native libraries, with formats a later
   Go client can reuse. Rejected: waiting for the Go runtime port.

## Slice 1: data packages

**Status: complete (2026-10-20), closing slice 1 for 0.0.7.** All seven tasks are
merged and every task 7 gate has passed. The game programs that remained in the
image left it with the stage 2 game packages.

### Accepted (owner, 2026-10-18)

1. **Formats in Lua, loaded strictly as data.** The repository index and each
   package manifest are Lua files returning one table. Because trust is out, both
   are untrusted input: they are parsed with the strict literal parser, which
   accepts only a returned table of tables, strings, finite numbers and booleans
   and never executes source, as boot reads the
   [installed configuration](../userland/init.md#installed-configuration)).
   There is no Lua VM, environment, function, global or library, and the parser
   is bounded by source size, nesting, value count and work. Programs name a
   package as directory source `package.ID` with their own root alias. Versions
   are opaque strings ordered by the index; dependencies are exact.
2. **The `pkg` client and its authority.** `pkg status`, `pkg list
   [--available]`, `pkg install ID[@VERSION]...`, `pkg remove ID...`,
   `pkg rollback` and `pkg verify`. Each change writes one new set and switches
   `current` once. Repository URLs live in `system://config/packages.lua`. Only a
   space whose configuration grants package management holds write authority on
   the `packages` volume and an HTTP endpoint; other spaces see packages read-only
   through declared sources.
3. **The repository and the archive cut.** Pyxis has its own static repository,
   held on horse for the near-to-mid future: `index.lua` and
   `packages/ID/VERSION.tar`, written only by the host packer, which replaces the
   index last by rename. A public tree holds fonts and toolchain payloads; a
   separate personal tree holds the licensed game data and is never published.
   Both are served read-only by a second [dufs](https://github.com/sigoden/dufs)
   instance on horse, without upload flags, bound to horse's LAN address, as a
   systemd user unit the owner enables. horse's ufw must allow that port from the
   LAN (owner action). dufs answers byte ranges, which resumable downloads can
   use later, and provides a browsing view. Nexus at `repo.internal` stays the
   build-input download cache and Forgejo's registry the builder-container
   registry; neither hosts Pyxis packages. The image build stops taking the
   licensed data inputs, so installed systems get that data only from packages;
   the later owner direction (2026-10-19) also removes directly usable
   shareware from live archives in favour of network-installed packages.

### Tasks

| # | Task | Repository |
| --- | --- | --- |
| 1 | Formats and host packer: the index, manifest and file inventory in [package formats](../interfaces/packages.md); `tools/pxpkg.py` publishes packages from recipes and checks a repository. | pyxis-os |
| 2 | The `packages` volume: Install creates it and Update creates it when missing; spaces receive it as one read-only `packages` source; bundles name `"package": "ID"` grants resolved at launch, spaces name package roots, and boot init reports a damaged set. | userland |
| 3 | The `pkg` client: index and archive fetch through the HTTP provider, SHA-256 verification, a bounded USTAR reader of its own (`tar` is GPL BusyBox), staging, the store move, set records, the switch, rollback and cleanup after boot. | userland |
| 4 | One consumer path: volatile npfs packages for live boots, repository configuration commands, then declared data roots for the six game ports and the toolchain/clangd default. Accepted details below; implemented and merged, with no archive fallback or local seed. | kernel, userland, ports |
| 5 | The archive cut: licensed data inputs become packer inputs only, and `share/fonts` moves to a package. Games later become [game packages](#the-boot-archive). Complete (2026-10-19). | pyxis-os |
| 6 | The repository on horse: [prepared setup](../../tools/package-repository/README.md), two trees and the dufs user unit deployed on horse; complete (2026-10-19). | host |
| 7 | [Qualification record](../development/experiments/packages-slice1/README.md): real-repository QEMU checks, owner-run vela install/verify, fonts and all four Doom/Quake programs, toolchain package and clangd, native removal/rollback and reboot lifetime, and private games; complete (2026-10-20). | records |

Order: 1, then 2 and 3 in parallel, then 4 and 5, then 6 and 7.

### Task 7 qualification status

- [x] Complete QEMU checks against the real horse repository (available fonts
  and Doom/Quake shareware, removal/rollback, missing-package refusal and RAM lifetime).
- [x] Owner-run vela packet: repository configuration, install and verify,
  font-min, Doom, Chocolate Doom, Quake and Chocolate Quake work (2026-10-19).
- [x] Native removal/rollback and reboot lifetime checks: hands-free on the
  ThinkPad, live boot with RAM package state (2026-10-20,
  [record](../development/experiments/packages-slice1/README.md#thinkpad-removal-rollback-and-reboot-lifetime)).
- [x] Private DevilutionX/EDuke32 packages and gameplay qualification: packages
  published, all six games started from them in QEMU, and native play passed:
  EDuke32 and DevilutionX on vela, retail Doom and Quake on the owner's stick
  (2026-10-20, [record](../development/experiments/packages-slice1/README.md#private-game-packages)).
- [x] Fresh hosted toolchain package with clangd and current command exports,
  published to the horse public tree; QEMU `pkg install`, C and C++ compile,
  link and run, and clangd in Neovim qualified (2026-10-20). The owner's vela
  live-RAM install, clangd in Neovim and a hello-world C compile and run worked.

Owner-requested pkg Review now uses `[y/N]`, default No: exactly `y` or `Y`
accepts; all other input cancels without switching the set. Installer typed
confirmations are unchanged.

No native/PXE action by alpha. The
[qualification record](../development/experiments/packages-slice1/README.md)
keeps measured checks separate from the remaining gates.

### Task 1 details (accepted, owner, 2026-10-18)

1. **Inventory:** a separate `package.files`, one `SHA256 SIZE PATH` line per
   file, parsed as a stream with fixed per-line bounds. Lua stays the format for
   metadata. The literal parser's limits (8,192 values, 1 MiB) could not hold the
   2026-10-18 toolchain payload's 8,701 files as one table.
2. **Archive layout:** `package.lua`, `package.files` and `tree/`; the store keeps
   the same shape in `store/ID/VERSION`, and the package source is rooted at
   `tree/`. Regular files and directories only, and the members exactly equal
   the inventory.
3. **Packer:** a Python tool on horse reading trusted Lua-literal recipes,
   emitting Lua literal text, with a check command that applies the client's
   strict literal rules.

### Task 2 details (accepted, owner, 2026-10-18)

1. **Resolution at launch:** each space gets one read-only `packages` source,
   and the bundle launcher reads `current` and the set record through it to open
   `store/ID/VERSION/tree` read-only. One source per package would have needed a
   larger source budget and dotted source names, and would have seen a switch
   only after a reboot.
2. **Naming:** manifests use `"package": "ID"` in directory grants, read-only
   only; spaces name package roots in configuration. A required package that is
   missing fails the launch naming it; an optional one is absent.
3. **Damage:** a damaged `current` or set record never stops boot. Boot init
   logs one line naming the cause, and lookups fail until `pkg rollback`; there
   is no automatic fallback to an older set. A launch reports the package as
   missing; a package root reports the cause when its space starts.

### Task 3 details (accepted, owner, 2026-10-19)

- [x] Implement the client and qualify installed QEMU operation (userland #301, Pyxis #983).

The native `pkg` client and trusted provisioning are implemented in userland
#301 / Pyxis #983. The [client reference](../interfaces/packages.md#package-client)
contains commands, authority, repository configuration, verification,
publication and recovery. [QEMU qualification](../development/experiments/packages-client/README.md)
records the revisions, runtime observations, costs and limits. Consumer
conversions, archive removal, horse deployment and native slice qualification
remain tasks 4–7.

Accepted by the owner on 2026-10-19:

1. **Management and recovery:** the installed `package_management = true` gate
   provisions declared writable packages and read-only configuration/inspection
   sources. HTTP backing is a separate private npfs volume. Mutations use an
   exclusive directory lock; explicit `pkg unlock` reports staging and clears
   an abandoned lock only after the writer has stopped. There is no age-based
   stealing or liveness heuristic; boot maintenance leaves locks for unlock.
2. **Fetch and verification:** serial HTTP snapshots, size-derived deadlines
   and an optional bounded `--timeout` override; the existing six-step SHA-256
   order and an independently written bounded USTAR reader. No retry, alternate
   downloader, whole-archive allocation or large-body RAM fallback.
3. **Publication and retention:** immutable numbered sets and predecessor
   records, synchronized staged trees and `current.new` followed by one atomic
   visibility switch and explicit final sync. Rollback creates a fresh set;
   damaged metadata needs an explicit verified target. Collection runs only
   before consumers at boot, retaining current and predecessor, with a shared
   five-second between-operation budget and conservative refusal on unclear
   evidence. Preparation uses native foreground interruption; the confirmed
   switch settles before reporting its actual result.

**Provider grants (accepted, owner, 2026-10-19):** bundle manifests may declare
read-only provider sources. A declaring bundle receives only its declared
providers, including when optional requests are unavailable, and inherits no
other provider roots or aliases. Bundles declaring none preserve current
behavior. The launcher checks actual exported protocol, transport and rights;
client-side attenuation alone is insufficient. See
[explicit provider grants](../userland/program-bundles.md#explicit-provider-grants).

### Task 4: one consumer path for live and installed systems

**Owner direction, 2026-10-19:** every game reads its declared package root.
Live and tester boots install on demand into a volatile RAM-backed packages
volume using the same `pkg` client as installed systems. There are no archive
fallback trees and **no local seed repository**. Testers configure their own
network repository; a public repository comes later. This replaces the earlier
slice-1 exception keeping directly usable shareware in the live archive.
The three decisions below were accepted by the owner on 2026-10-19;
implementation and QEMU qualification of task 4 are authorized.

#### Implementation boundary

The RAM pool reuses npfs codecs, worker, cache, journal and volume machinery;
it does not relabel the ordinary RAM directory tree or expose a raw installable
disk. Existing live `home://` and `tmp://` remain separate RAM trees. Repository
configuration uses the npfs `system` volume and the confined writer described
below; immutable `boot://config/live.lua` remains the session configuration.

Repository HTTP fetch accepts DNS hostnames, including single-label names such
as `horse`, through the existing HTTP provider. It still needs a working DNS
server and a record for that name, without mDNS or guessed-address substitution.

#### Decision 1 — RAM pool and lifetime (accepted, owner, 2026-10-19)

**Accepted:** a filesystem-private, sparse RAM backing for one npfs pool, created
by trusted live boot before spaces start. Reuse the real npfs format and worker,
with a narrow memory transport instead of exposing a synthetic installable disk
or manufacturing a GPT. Add bounded initial formatting/provisioning using the
existing codecs; ordinary programs receive roots, not raw backing or creation
rights. Keep current `home://` and `tmp://` RAM trees unchanged.

Use a configurable **2 GiB logical pool capacity** as the authored live default,
not an eager 2 GiB allocation or a format limit. Allocate zero-filled pages on
write, within that capacity and available memory, using existing BSP ownership
rules and bounded work. The pool holds separate `packages`, private `http-work`
and `system` volumes. Store and activation trees remain together in `packages`;
HTTP backing remains separate from consumer views. Initialization/allocation
failure reports the unavailable feature; it never claims physical storage,
falls back to another backend or prevents the ordinary live shell starting.

Retain backing for the boot and all derived handles; it is gone at reboot, with
no promise of general hot-unmount. Sync still executes npfs ordering/commit;
RAM transport flush orders completed memory writes, not persistence over reboot.
Current/predecessor records, rollback and writer recovery use the same client.
No new runtime collector or live-inode eviction: reboot discards the whole pool;
a long session can exhaust it and must receive an explicit capacity/error result.
Sparse backing may retain its high-water allocation until reboot.

A roughly 370 MB toolchain is acceptable, but its RAM cost is not just 370 MB:
a download plus extraction needs roughly two payload-sized copies, and retaining
another selected version can need three, plus metadata, caches and applications.
The capacity is shared, not a reservation guaranteeing that workload on a
low-memory machine. Qualification must cover graceful bounded failure as well
as successful installation; do not weaken `pkg`'s npfs checks.

#### Decision 2 — repository commands and the config writer (accepted, owner, 2026-10-19)

*Since 2b-1 (2026-10-22) repositories are named: `pkg repository add NAME URL`
and `remove NAME` over `{ name, url }` entries
([2b decisions](#2b-decisions-chronium-2026-10-22),
[reference](../interfaces/packages.md#repositories-and-fetches)). The writer and
URL rules below are unchanged.*

**Accepted:** `pkg repository list`, `pkg repository add URL` and
`pkg repository remove URL`. They use the existing strict literal
`system://config/packages.lua`, with the same eight distinct URLs, 2,048-byte
per-URL bound and HTTP/HTTPS trailing-slash/no-credentials/query/fragment rules.
Add/remove accept a URL with or without the final `/`, appending it before
validation, storage and comparison; the 2,048-byte bound includes that slash.
They require package-management authority; list uses the read-only config
source. Addition of an existing normalized URL reports already configured
without changing configuration; removal matches either spelling and reports
not found when absent. Missing configuration starts empty; malformed existing
data refuses rather than being silently discarded. Adding a URL fetches and
strictly validates its `index.lua` before saving; an
unreachable host, HTTP error or invalid index refuses without changing the
configuration. Remove does not require a connection.

On live boots, trusted startup creates `system/config/packages.lua` in the
volatile npfs `system` volume from the archive's empty/default literal list.
Expose `system://` read-only for inspection; ordinary live programs receive no
write access to it. No boot, network or machine-setting configuration is migrated
into that volume by this task. Installed systems keep the existing persistent path and owner edits.

Use a **separately declared, name-confined config-writer directory view**,
issued only through the management gate, permitting the canonical file and one
fixed temporary sibling. It forwards the underlying same-directory rename and
sync, but refuses access to `boot.lua`, network/machine settings, other names,
links or moves outside the view. Keep `package_config` read-only; `pkg` receives
no unrestricted write authority on `system/config`.

Serialize add/remove with package mutation using the existing writer lock;
read and validate the current list after acquiring it. Write/reparse/sync the
complete temporary file, replace the canonical name atomically, then explicitly
sync its parent. A final-sync failure is reported as uncertain, with no retry or
claim that the old list necessarily remains selected. Use the same ordering on
live RAM backing; successful live edits last only until reboot. Existing pkg
processes keep their captured configuration; a new invocation reads the edit.

DNS names such as `http://horse:PORT/` must work when configured DNS resolves
them. Failed resolution/refused HTTP leaves the selected set unchanged, without
numeric-address substitution. The empty shipped list gives an actionable
`pkg repository add NAME URL` instruction; it never invents a public endpoint.

#### Decision 3 — live grants and on-demand use (accepted, owner, 2026-10-19)

**Accepted:** Development, Multiplexer and Remote explicitly enable
`package_management = true`, sharing the same volatile store/config and writer
lock. Read-only stays inspection/consumer-only. Use the installed grant model:
declared writable package management and the confined config writer, read-only
inspection/config sources, and explicitly declared GET-only HTTP/HTTPS providers
for `pkg`. Provider grants do not become ambient grants to ordinary children.
Trusted provider startup receives its own private npfs HTTP staging; consumers
receive only their declared read-only package trees. No raw disk, installer or
pool-creation authority reaches `pkg` or games.

The user adds a repository and runs `pkg install ID`; game launch itself never
auto-fetches, opens a management dialog or acquires management/network authority.
Before installation, an offline or unconfigured live/PXE system still boots its
ordinary tools, but cannot run a game whose data is absent. There is no offline
shareware path, local seed, automatic alternative or archive-data search. The
boot archive loses those data trees as the task-5 archive cut lands; opt-in tester
ISOs follow the same network-only rule. No licensed inputs enter repos or logs.

Games use `doom-data`, `quake-data`, `diablo-data` and `duke3d-data` roots; the
Doom/Quake pairs share their respective package. Bundle the three bare game
executables, keep saves and explicit application arguments unchanged, and put
DevilutionX program assets inside its bundle. `clang-hosted` declares its tool
commands in `package.lua`, publishing the complete tool bundles through the
common live package-command view. Neovim launches bare `clangd`, just as the
shell launches bare `clang` or `lld`; no per-program catalog grant, alias runtime
or fixed `toolchain://` space root is needed. Newly installed commands are
available to later launches in existing spaces. Already admitted processes and
resource views retain their selected version. No archive fallback consumer path
remains.

#### Task 4 delivery

1. Implement bounded private RAM-npfs creation/provisioning and live grants;
   qualify no-disk boot, low-memory/capacity failure and reboot-to-empty lifetime.
2. Add repository commands and the confined writer. Qualify persistence on a
   disposable installed pool, volatility on live, lock contention, unchanged
   config/set on refusals and DNS-name HTTP fetches. No local seed transport.
3. Migrate the six ports and declare toolchain command exports; make Neovim use
   bare `clangd` through the shared resolver. Qualify both live-RAM and
   installed package launches plus missing-package refusal. Publish dependencies
   before parent pins. Keep the archive cut in task 5 and full native/consumer
   slice qualification in task 7; no native/PXE action without owner coordination.

- [x] Implement live RAM-npfs provisioning, the confined repository writer and commands.
- [x] Convert the six ports and publish toolchain commands through the common view.
- [x] Build and qualify the bounded task-4 scope in QEMU;
  [record](../development/experiments/packages-consumers/README.md).
- [x] Owner merge; repository/native tasks remain separate.

Small-pool exhaustion refused without switching, but reported writer cleanup
unconfirmed; writable recovery remains unqualified. Native/PXE action still
requires owner coordination.

#### Command-search integration

The command-search prerequisite is merged. Task 4 resumes on its common live
package-command view: package manifests explicitly declare exports, undeclared
files export nothing, and launch retains the selected complete bundle policy.
There is no `package_catalog` grant or separate package-bundle alias mechanism.
The [command-directory interface](../interfaces/command-search.md) describes
lookup, inheritance and held-view lifetime.

Neovim obtains server-resource views from the actual admitted libuv selection,
without resolving `clangd` again. A package switch affects subsequent launches;
open header buffers keep their original resource view, including after server
exit. Native build-tool descendant chains still need the separate nested-launch
policy and qualification; publishing compiler commands does not provide it.

### Task 5 — archive cut (complete, 2026-10-19)

- [x] Remove game data and font collection from image assembly; keep personal-use
  executables as separate explicit opt-ins.
- [x] Provide packer recipes for unchanged cached fonts and public shareware,
  with owner-local selections documented separately.
- [x] Move `font-min` to a declared read-only `fonts` package root.
- [x] Build and qualify the archive cut in QEMU;
  [byte counts and limits](../development/experiments/packages-archive/README.md).

[Package inputs](../../packages/README.md) replace the retired image-data
variables. The cut took the default boot archive from 105,754,112 to 70,097,408
bytes, and the ISO from 116,867,072 to 81,211,392 (2026-10-19,
[record](../development/experiments/packages-archive/README.md)). No image,
live/tester boot or consumer has an archive fallback or local seed; fetching
still requires an explicitly configured network repository. The game programs
that remained in the image left it with the [game packages](#the-boot-archive)
in stage 2.

### Task 6 — horse repository (complete, 2026-10-19)

- [x] Luna prepared both repository trees and the unit from `880ff590` in
  `~/src/pyxis-pkgrepo`; the owner enabled the user unit and LAN ufw rule.
- [x] Orchestrator verified the active service at `192.168.0.213:5245`, public
  index HTTP 200, the personal index and write refusal (PUT HTTP 403).

The 2026-10-21 public tree offered `fonts` at
`nerd3.5.1-plexsans1.1.0-plexserif2.0.0-noto2.015-notomono2.014` (published
2026-10-21; the earlier `nerd3.5.1-plexsans1.1.0-plexserif2.0.0` stays beside it);
the personal tree offers `doom-data`
`shareware-1.9` and `quake-data` `shareware-1.06`. These are deployment facts,
not shipped repository defaults. Clients still configure their URLs explicitly.

`clang-hosted` was published to the public tree on 2026-10-20 from a fresh
producer build; see the [qualification record](../development/experiments/packages-slice1/README.md#hosted-toolchain-package-2026-10-20).
The earlier manual payload was never packed.

### Later ideas

- A read-only web view of the repository beyond dufs's directory listing.

- **Live fetch progress (idea, owner-accepted for later, 2026-10-20):** the
  install display shows only the indexed size while an archive downloads,
  because the HTTP provider returns a completed snapshot and reports nothing
  before that. Bytes, percent and rate during the transfer would need an
  HTTP-provider progress counter readable while a GET is in flight, and a way
  for the client to read it without a second concurrent operation in the same
  process (the provider admits one exchange at a time and
  [adds no concurrency](../interfaces/http-transactions.md)). It changes the
  provider interface, so it needs its own design and owner decision first.

- **Public package mirror (historical owner idea, 2026-10-19):**
  the implemented signing/Worker machinery is recorded in
  [package upgrades](pkg-upgrade.md#step-3-the-updater). The original options
  and prerequisites below describe the pre-signing design, not current client
  trust or proof that any particular deployment is live:
  the repository is static files (`index.lua` plus archives), so it can be
  served from object storage behind Cloudflare with no server code: Cloudflare
  R2 (small free storage tier, no egress fees), or a Backblaze B2 bucket behind
  a Cloudflare domain for larger storage. Uploads go outbound from horse, so
  nothing opens toward the home network. Prerequisites before anything is
  public:
  1. Signed indexes with the owner key and a pinned public key in client
     configuration. Today `pkg` trusts the index because it comes from the LAN,
     and the SHA-256 in the index proves integrity, not publisher identity.
     Signatures were deferred from slice 1.
  2. A retention policy (for example current and previous release per package),
     since storage is driven by kept versions; the toolchain is about 370 MB per
     revision.

  Until then testers run their own repository.

  Tester ISOs (chronium, 2026-10-21): the mirror's storage also holds the tester
  ISOs, published as release artifacts beside a `SHA256SUMS` file and a signature
  made with the same signing key and tooling as the packages. An ISO is built from
  the same tagged revision as that release's packages. The
  [website](website.md) links the current one. Not decided: download bandwidth and
  caching for the ISO on the mirror host. Signing and image-generation machinery
  are now implemented; [signing operations](../../packages/signing/README.md)
  remain chronium's, and the private keys stay with chronium. This signing
  chain is separate from the public TLS certificate of `pkg.pyxis.nexus`; see
  [certificates](website.md#certificates).

## Stage 2: game packages (proposal, 2026-10-20)

**Historical proposal; all three [decisions](#stage-2-decisions) accepted (owner,
2026-10-20).** Game packages and their command exports were implemented. This
section records their original revision-bound design; the release-ABI model,
program packages and updater now follow [package upgrades](pkg-upgrade.md).
Retained examples below describe the earlier format, not today's ABI policy.

### Binding a build to a system

[Package ABI decision](#package-abi-and-failed-starts) (accepted, chronium, 2026-10-21) named this
field `abi`, required it on `toolchain` too, and made the running side a list.

- Program packages get a new kind, `program`. The manifest and index entry
  carry `abi`, which **for now** is the 12-hex source revision of the system
  the build is for.
  This is the value the running kernel reports as `build_revision` in
  [SYSTEM_INFO_IDENTITY](../interfaces/system-information.md), and the one
  Update already uses for `bin/REVISION`. The comparison is exact string
  equality, with no ordering or ranges. There is no ABI stability policy, so a
  build for another revision is not assumed to work.
- `abi` is required on `program` and `toolchain` and refused on `data`.
  `clang-hosted` is packed with the ABI of the checkout that produced it.
- `pkg` reads the running revision through a new read-only `system_info` grant.
  It does not offer a bound entry for another system
  (`pkg: doom is not available for this system (2 builds for other systems)`),
  and it refuses an operation on a set that keeps stale builds, naming them all
  at once with both sides and one command that clears them:
  `pkg: 2 installed packages are not built for this system, which provides f107a7adc816: doom (needs ABI 05fc52104a2c), quake (needs ABI 05fc52104a2c). Replace with: pkg install doom quake. Or drop with: pkg remove doom quake`.
  This covers install, remove, rollback and verify. A build without a revision
  (an archive outside Git) refuses every bound package.
- At launch, the bundle launcher refuses a bundle exported by a bound package
  built for another revision. The refusal names the package and the remedy
  `pkg install doom`. This catches a set left from before the installer's
  Update, until that Update retires. The set's command index records each
  package's `abi` and the launcher compares it with the running ABI from its
  `system_info` grant (chronium, 2026-10-21; option A).
- **Versions** stay unique per ID across systems, because the store holds both
  releases' builds while `pkg upgrade` stages the next one. A bound version is
  `VARIANT+LABEL`. `VARIANT` names the content, for example `shareware-1.9`.
  `LABEL` is the release tag for a tagged build (`shareware-1.9+0.0.8`) and
  the revision otherwise (`shareware-1.9+3f1c2a9b0d4e`). The packer composes
  the version, and the client never parses it: only `abi` binds. A second
  build with the same label refuses as an already published version.

The index lists one entry per build:

```lua
{
  id = "doom",
  version = "shareware-1.9+0.0.8",
  kind = "program",
  abi = "3f1c2a9b0d4e",
  archive = "packages/doom/shareware-1.9+0.0.8.tar",
  size = …,
  sha256 = "…",
},
```

`pkg list --available` shows unbound entries and the entries for the running
system, then one line counting the builds for other systems it hid. Within each
repository, "install takes the last version" applies after this filter. Each
entry costs more literal values, so the per-index limit in the
[format reference](../interfaces/packages.md#literal-data) is recomputed.
[Retention](#rebuilding-the-trees) keeps the count bounded.

### Game package contents

```text
doom: tree/doom.pxb/            port bundle, command doom
      tree/chocolate-doom.pxb/  port bundle, command chocolate-doom
      tree/DOOM.WAD             shareware, or the owner's retail IWAD
      tree/e1m1sec.lmp …        optional demo recordings
      tree/licenses/            port licences and notices, data notices

quake: tree/quake.pxb/  tree/chocolate-quake.pxb/
       tree/id1/pak0.pak  tree/id1/pak1.pak (retail)  tree/licenses/
```

- **Commands** use the existing bundle exports:
  `doom = { path = "doom.pxb", command = "doom" }`, and likewise for the
  others.
- **Data:** each bundle's package grant names its own game. `"package": "doom"`
  replaces `"doom-data"`, and the root alias stays `doom_data`, so the ports
  still read `doom_data://DOOM.WAD`. Only the manifest line changes. The bundle
  also sees its sibling bundles, read-only, which grants nothing new. No new
  grant kind is needed.
- **Version mix (limit):** the launcher resolves `doom` from the current set at
  launch, as it does today. A command captured from an older retained selection
  can therefore run one version's program with a newer version's data. Both
  builds are for the running system, so this works, but the pairing is not
  exact. A grant bound to the containing tree would make it exact; that is not
  proposed now.
- **Variants:**
  - Doom: `shareware-1.9` in the public tree, and for example `ultimate-1.9` in
    the personal tree.
  - Quake: `shareware-1.06`, and for example `retail-1.06`.
  - The owner's local recipes choose the retail names.
  - For one system, both trees carry the same port build; only the data differs.
- **Size:** each build repeats its data bytes. The cost is largest for retail
  Quake in the personal tree. This is why [retention](#rebuilding-the-trees)
  matters.

### Choosing a repository

This replaces the slice 1 rule that an ID offered by two configured
repositories is refused.

- **Names:** repositories get explicit names, under the package ID rules, so
  `REPO/ID` parses without ambiguity:

  ```lua
  return {
    repositories = {
      { name = "public", url = "http://192.168.0.213:5245/public/" },
      { name = "personal", url = "http://192.168.0.213:5245/personal/" },
    },
  }
  ```

  The commands become `pkg repository add NAME URL` and
  `pkg repository remove NAME`. Names are not derived from the URL, because a
  path component is a server layout detail and would become a contract.
  `pkg upgrade`'s per-repository trust setting later joins the same entry.
- **The choice:** `pkg install doom` for an ID that isn't installed counts only
  repositories that offer a build for the running system. If one does, it is
  used without a question. On a console, several repositories give the
  numbered choice:

  ```text
  pkg: doom is offered by 2 repositories
    1  public/doom@shareware-1.9+0123456789ab
    2  personal/doom@ultimate-1.9+0123456789ab
  Choose 1-2, or press Enter to cancel:
  ```

  Outside a console, `pkg` refuses and prints the same list with `REPO/ID`
  spellings.
- **Explicit selection:** `REPO/ID` and `REPO/ID@VERSION` select directly.
  `ID@VERSION` offered by several repositories gives the choice.
- **Order:** the choice comes before the `[y/N]` review, and the review shows
  the origin: `add doom@shareware-1.9+0123456789ab (public)`.
- **Dependencies** follow the same rules, chosen when reached, after the
  packages that need them were fetched; a stored exact dependency is hashed in
  place rather than fetched. Game packages have none.

### Recorded origin

- **Set records:** each set record entry gains `repository = "public"`. The
  record changes in place, with no version field and no reader for older
  records ([pre-release data](#pre-release-data-chronium-2026-10-21)).
- **Reinstall:** `pkg install doom` when `doom` is installed resolves only from
  its origin, taking the last build there for this system. It asks no question
  and ignores other repositories. This is how a game gets new data or replaces
  a stale build.
- **Switching origin** happens only on request: `pkg install personal/doom`.
  The review shows `doom: public → personal`.
- **Unpinned entries:** the next resolve asks as for a new install and records
  the answer.
- **`pkg upgrade`** resolves each bound package for the new system from its
  origin only and never looks elsewhere. A package whose origin has no build for
  the new system is a removal listed in the review, which the user must accept
  or cancel the upgrade
  ([`pkg upgrade` decision 3](pkg-upgrade.md#decided)).
- **Removing a repository:** `pkg repository remove NAME` leaves installed
  packages working, because the store is local, and prints the packages pinned
  to it. Resolving those packages refuses, naming the missing origin, until the
  name is added again or the origin is switched. The origin is a configured
  name, not a URL, so removing a name and adding it again with a new URL
  deliberately moves it, for example to a mirror.

### Leaving the image

- **Image:** assembly stops installing the `doom`, `chocolate-doom`, `quake`
  and `chocolate-quake` bundles, and the `EDUKE32=1` and `DEVILUTIONX=1` image
  flags retire (decision 3), so the image has no games left. The ports still build them as port outputs
  for packing.
- **Ports:** their manifests name `doom` and `quake` in place of `doom-data`
  and `quake-data`, and the port READMEs follow.
- **Recipes:** `packages/doom.lua` and `packages/quake.lua` replace
  `doom-shareware.lua` and `quake-shareware.lua`.
- **Data IDs:** `doom-data`, `quake-data`, `duke3d-data` and `diablo-data`
  get no new versions. Their archives stay in the
  [frozen trees](#stage-2-decisions) until those are removed at 0.0.8. On a system that has them
  installed, they become unreferenced data. `pkg remove doom-data quake-data`
  removes them; `pkg` does not remove them on its own.
- **Live and tester boots** work as they do for fonts: configure a repository,
  then `pkg install doom`.
- **Technical debt:** the "game programs in the image" exception is removed.

### Rebuilding the trees

- **Recipes:** recipes for program packages take `kind = "program"` and a
  `variant` in place of `version`. A new `build` entry resolves against the
  `--build DIR` argument: the build directory of the system the package is for.

  ```lua
  return {
    id = "doom", variant = "shareware-1.9", kind = "program", depends = {},
    commands = {
      doom = { path = "doom.pxb", command = "doom" },
      ["chocolate-doom"] = { path = "chocolate-doom.pxb", command = "chocolate-doom" },
    },
    entries = {
      { build = "ports/doom/stage/bin/doom.pxb", at = "doom.pxb" },
      { build = "ports/chocolate-doom/stage/bin/chocolate-doom.pxb", at = "chocolate-doom.pxb" },
      { build = "ports/doom/stage/share/licenses/doom", at = "licenses/doom" },
      { file = "../third_party/doom-shareware/doom1.wad", at = "DOOM.WAD" },
      { file = "../third_party/doom-shareware/LICENSE", at = "licenses/data/LICENSE" },
    },
  }
  ```

  The paths are illustrative. Task 1 takes them from the ports' declared
  outputs.
- **Packing:** the packer takes `abi` from the build's revision record, the
  one the kernel embeds. The label is the tag at that commit, or the revision
  otherwise.
  - `make game-packages REPOSITORY=DIR` packs the in-tree `doom` and `quake`
    shareware recipes. It is never a dependency of `make image`, CI or any
    other target; it runs only as the explicit step below.
  - It refuses when the checkout has local changes, because `abi` would
    name a commit whose bytes differ.
  - Personal packages use owner-local recipes of the same shape:
    `python3 tools/pxpkg.py pack --build build /private/doom.lua REPOSITORY`.
- **Only builds that will be used** (decision 1): game packages are packed
  only for a build that will actually run them, never per commit, per CI run
  or speculatively. Packing is an explicit step of exactly two procedures:
  - **Release:** at release time, the tagged build packs the public shareware
    packages, and luna on horse packs the personal retail packages, both into
    the horse trees. [Releases](../releases.md) gains this as a checklist step.
  - **Deploy:** a build explicitly being deployed to a real machine, such as
    the owner's stick, packs that system's packages into the horse trees as a
    step of that deployment's run sheet.
  - QEMU checks of a branch pack into a temporary local repository that is
    removed afterwards, never into the horse trees.
- **Retention:** `pxpkg.py prune REPOSITORY --keep-abi ABI...` removes bound
  entries for every ABI not listed.
  - It writes and renames the new index first, syncs, and then deletes the
    archives. Unbound entries are untouched.
  - Tagged releases are kept by listing their revisions.
  - Prune is the only way to remove published archives; replacing one is
    still refused.
  - A client holding an older index can get HTTP 404 for a pruned archive. The
    error names the URL.

### Stage 2 decisions

1. **What a build is bound to. Accepted (owner, 2026-10-20):** the exact
   system revision, as above, with no wasted builds. Game packages are packed
   only for builds that will actually be used: a tagged release at release
   time, and a build explicitly being deployed to a real machine such as the
   owner's stick. Never per commit, per CI run or speculatively. Packing is an
   explicit step of the release and deploy procedures
   ([rebuilding the trees](#rebuilding-the-trees)), and an explicit `prune`
   keeps the trees bounded. Rejected: binding to the release tag only, which
   leaves untagged deployed builds without games; checking nothing, or
   accepting any revision in a release line, which loads a mismatched build
   against kernel and protocol changes when there is no ABI stability.
2. **Formats and 0.0.7.** The `program` kind, `abi`, named repositories and
   set-record origins change the index, manifest and set records. 0.0.7's
   strict client would refuse a whole index that contains them. **Accepted (owner, 2026-10-20):** when stage 2 lands, horse keeps a
   frozen copy of both trees as they stood at 0.0.7, in `0.0.7/public/` and
   `0.0.7/personal/`. The copy is hard-linked, so it takes no extra space and
   needs no tool support. 0.0.7 systems configure those URLs. The live trees
   and the packer move to the new records only. The frozen copies exist only
   until 0.0.8 exists; then they are removed, as a step of the 0.0.8 release.
   Rejected: an index in 0.0.7's form of the expressible entries kept in every tree,
   an ongoing cost for a release that upgrades away; no provision, so 0.0.7
   live boots lose fonts and games.
3. **EDuke32 and DevilutionX. Accepted (owner, 2026-10-20):** they become the
   personal-only game packages `duke3d` and `diablo` in the same task, with the
   same shape, and are never packed into the public tree. The `EDUKE32=1` and
   `DEVILUTIONX=1` image flags retire, and the image has no games left.
   Building them stays an explicit opt-in for `make game-packages`. Rejected:
   keeping them as image opt-ins until a later task.

### 2b decisions (chronium, 2026-10-22)

2b's names, choice and origins touch the same set records as
[`pkg upgrade`](pkg-upgrade.md) T4a part 2, which moves set publication into a
library shared with Install and Update. All three defaults were accepted:

1. **Two parts.** 2b-1 (names, `REPO/ID`, the numbered choice; the refusal of an
   ID offered by several repositories goes) changes no set record and lands
   first. 2b-2 (set-record origins, resolving from the origin, the pinned list
   on `pkg repository remove`) follows T4a part 2 and writes origins through its
   shared library, so Install and Update carry them over. The release package
   records no origin.
2. **The earlier `packages.lua`.** A list of bare URLs is refused as invalid,
   naming `system://config/packages.lua`, like other
   [pre-release data](#pre-release-data-chronium-2026-10-21). The one-time step
   after the Update that brings 2b-1 removes the file and adds the repositories
   again by name; it is in the 2b-1 PR description.
3. **Shareware into `public/`.** A client without 2b-1 refuses every command
   once two configured repositories offer one ID, so the shareware `doom` and
   `quake` are packed into `public/` only at the first repack after chronium's
   stick runs 2b-1.

### Pre-release data (chronium, 2026-10-21)

Before a public release there are no format versions and no readers for older
package data. The repository index, the manifest, the volume records
(`current`, `sets/N.lua`, `sets/N.previous`) and the command index have one
form each; anything else is refused as invalid like any damaged file. Volumes
written by 0.0.7 or 0.0.7a are cleared once by hand after the Update that
brings this change; the steps are in #1078's description, not the reference.
Install, remove and rollback also stop re-hashing members carried over
unchanged; only added or changed members are verified, and `pkg verify` still
hashes everything.

### Task 4: trees on horse (2026-10-21)

**Decisions (chronium, 2026-10-21).**
1. Until named repositories (2b) land, `public/` offers only `fonts` and
   `clang-hosted`, and `personal/` offers retail `doom` and `quake` plus
   `duke3d` and `diablo`. `pkg` refuses every command when two configured
   repositories offer one ID for the running system, so the shareware `doom`
   and `quake` join `public/` with 2b, at the first repack after chronium's
   stick runs 2b-1 ([2b decision 3](#2b-decisions-chronium-2026-10-22)).
2. R is `origin/main` when luna starts, `07752b7f`. Luna built chronium's stick
   installer from the same clean checkout in the same run.

**What was done.** Luna built everything from one clean clone at
`07752b7f04a30879dcd99514f87338d5d0c6d699` (submodules at its pins; clean before
and after) in `pyxis-llvm23.1.3-3d69e83`: the image, the fonts, the four
personal game packages from owner-local program recipes, and the hosted Clang,
LLD and clangd producer. It packed them into staging trees and staged the
installer under the PXE "Install Pyxis" entry as `stage2-07752b7f`; the
default entry is unchanged. Both trees pass `check --abi 07752b7f04a3`. The
orchestrator then moved the 0.0.7-format trees to `0.0.7/public/` and
`0.0.7/personal/` and the staging trees into place.

| Archive | Bytes | SHA-256 |
| --- | --- | --- |
| `public/packages/fonts/nerd3.5.1-plexsans1.1.0-plexserif2.0.0-noto2.015-notomono2.014.tar` | 19554304 | `4294963a54f6f2a75103c00248e00312af387a62c38f22c2ab14225f0405864d` |
| `public/packages/clang-hosted/90770efb…eaa5fa.tar` | 373176320 | `e0cb89a0191e0b78a90636686efed4507532eab9281f149c8add169ae3218ffc` |
| `personal/packages/doom/ultimate-1.9+07752b7f04a3.tar` | 14703104 | `4198cd4b4b552f402167b2fe527358a5e5d9fc2e04f856f50b1edd87eab44320` |
| `personal/packages/quake/registered-1.06+07752b7f04a3.tar` | 54996480 | `9df27c212feff812c893e4e2275229c9b4d38ef1ed2c21f88b939b5931837f81` |
| `personal/packages/duke3d/megaton+07752b7f04a3.tar` | 48320000 | `a57083f812a643853a943641c2825d53ed93b6be3ee79ced29fd118520933b57` |
| `personal/packages/diablo/shareware+07752b7f04a3.tar` | 35281920 | `0f80eb193501bc24bfd54ede15337acd96d82223baf89c528a3940fbb295184d` |

- `public/index.lua`: `36f45b53bdd89e70a859f1d9eb389eadb12202826a6c62f12ed745f7b8e62cd7`
- `personal/index.lua`: `9d6aaa412d0210f6681027d513317400b2515d8ba54c9e084dc90f6b62a421eb`
- Installer `stage2-07752b7f`: `caelum.elf`
  `9e3198a4f5ae894c59c6f0690252a70b6105fa01ca783acfcd0a31406eb84606`,
  `initrd.cpio` `986e905e403c19a8e04605f80379c2ab4fe791a0863e6edfd76156931f7ef6d4`.

**Checks.** Over HTTP, both live indexes match, the frozen 0.0.7 indexes are
unchanged, a byte range returns 206 and PUT returns 403. A current client on
another revision, in QEMU, read both trees: it offered `fonts` and reported
`5 builds for other systems hidden`.

**URL change.** 0.0.7 and 0.0.7a systems, such as vela, must use
`http://192.168.0.213:5245/0.0.7/public/` and
`http://192.168.0.213:5245/0.0.7/personal/`; the live trees are refused there as
`invalid repository index`. Clients use horse's LAN address because the LAN has
no DNS record for `horse` (chronium, 2026-10-22).

### Stage 2 game-package tasks

| # | Task | Repository |
| --- | --- | --- |
| 1 | Formats and the packer: the `program` kind, `abi`, recipes with `variant` and `build`, `VARIANT+LABEL` versions, `prune`, `make game-packages`, and the [format reference](../interfaces/packages.md). | pyxis-os |
| 2 | Client and launcher: the running-revision check at install, rollback and launch; named repositories; the numbered choice and `REPO/ID`; origins in set records; resolving from the origin. | userland |
| 3 | Game packages: the port manifests name `doom` and `quake`, image assembly drops the four bundles, in-tree recipes; personal-only `duke3d` and `diablo` with the `EDUKE32`/`DEVILUTIONX` image flags retired; the technical-debt entry removed. | ports, pyxis-os |
| 4 | Trees on horse: the frozen 0.0.7 copy (removed once 0.0.8 exists), the trees in the current format, public shareware and personal retail builds for the revision then in use; the [repository setup](../../tools/package-repository/README.md), the release checklist step and the deploy run-sheet step. | host, docs |
| 5 | Qualification in QEMU: the numbered choice, the refusal outside a console, `REPO/ID`, resolving from the origin, the refusals for another system at install and launch, and a 0.0.7 client against the frozen tree. Then an owner run of retail Doom and Quake from the personal tree. | records |

Order: 1, then 2 and 3 in parallel, then 4, then 5.

- [x] Task 1: the formats and the packer with `abi` and `check --abi`, checked
  on the host. Nothing on horse is repacked before task 4.
- [ ] Task 2: client and launcher, in two parts.
  - [x] 2a, client: `pkg` reads the new formats and refuses builds for another ABI at install,
    remove, rollback and verify, naming both sides; builds for other systems are
    hidden and counted. Checked in QEMU, live and across two installer Updates.
  - [x] 2a, launch: the launch-time refusal, from `abi` in the set's command
    index (option A, chronium, 2026-10-21). Checked in QEMU: a doom built for a
    previous ABI is refused by name with the remedy; current builds launch.
  - [x] One format (chronium, 2026-10-21): the format versions and the readers
    for older package data are gone; older data is refused as invalid and
    cleared by hand once ([pre-release data](#pre-release-data-chronium-2026-10-21),
    #1078).
  - [ ] Follow-up: the Lua and libuv launchers in the ports (Lua jobs, Neovim's
    `:!` and `clangd`) call the `_reason` forms so their refusals carry the
    line too; today they report a bare `CALL_DENIED`.
  - [ ] 2b, in two parts ([decisions](#2b-decisions-chronium-2026-10-22)).
    - [x] 2b-1: named repositories, `REPO/ID`, the numbered choice and its
      refusal outside a console, origins in the review. Checked in QEMU.
    - [ ] 2b-2, after `pkg upgrade` T4a part 2: set-record origins, resolving
      from the origin, switching origin by `REPO/ID`, and the pinned list on
      `pkg repository remove`.
- [x] Task 3: game packages. The ports name `doom`, `quake`, `duke3d` and
  `diablo`, the image carries no games, and the `EDUKE32`/`DEVILUTIONX` flags
  are retired.
- [x] Task 4: trees on horse ([record](#task-4-trees-on-horse-2026-10-21)).
- [ ] Task 5: qualification.

## Package ABI and failed starts

Package installation and launch use [release ABI declarations](../interfaces/abi.md)
under the accepted [ABI/core/updater model](pkg-upgrade.md#decided). Development
changes happen in place; this adds no compatibility reader or shim.

**Failed starts accepted (chronium, 2026-10-21), implemented:** runtime startup
publishes `PROCESS_START_FAILED` with the step and native status. The shell
reports `did not start: standard input: unknown system call` even before the
child has stderr. Unknown syscall results stay distinct from UNAVAILABLE.
[Process completion](../interfaces/processes.md#implemented-process-completion)
defines the result, retirement and reporting contract;
[qualification](../development/experiments/startup-diagnostics.md) records the
mismatched-build reproduction and limits.

The motivating stale `clang-hosted` build (`05efd5b21877`, userland `c3be6fe6`)
exited 1 on vela's 0.0.7a (chronium, 2026-10-20). Earlier QEMU inspection on
`7666891985fa` found four calls to retired HANDLE_INFO number 5, then exit 1.
Three optional runtime queries were ignored; stdin setup exited before stderr.
The kernel's all-ones unknown-call status had been folded into UNAVAILABLE.
The shell could name the ordinary exit status, but could not recover the startup
reason. Rebuilding the runtime fixes future programs; existing static programs
still need ABI refusal or rebuilding.

- [x] Native failed-start completion, TLS-free runtime reporting, preserved
  unknown-call status and parent diagnostics. Launcher refusals remain separate
  from post-launch runtime failure; ordinary application exits are unchanged.

## Package roots (accepted, chronium, 2026-10-21)

**What failed.** chronium installed `fonts`, but a hand-compiled `font-min.pxe`,
which has no bundle manifest, could not open `fonts://`. That root exists only
when `boot.lua` declares `roots = { fonts = { package = "fonts" } }`, and boot
init resolves it once, when the space starts. Using an installed data package
from a plain program therefore needs a configuration edit and a reboot.

**Accepted direction (chronium, 2026-10-21).** A space that holds the
`packages` source gets package roots automatically. They are resolved at
lookup time from the current set, with no `boot.lua` edit and no reboot. The
decisions below are only the details.

### How it works

- **One capability per space.** Boot init gives each space that holds the
  `packages` source an exported, read-only `package_roots` view, beside its
  ordinary roots. Children inherit it like any root, so the shell's tools and
  plain programs see the same names.
- **Lookup order.** For `NAME://`, the path helpers look up an ordinary root
  named NAME first. Only when there is none do they ask `package_roots` for
  NAME. An unknown scheme with no package of that name still fails as today.
- **What a lookup returns.** The view reads the active set at that moment and
  returns that package's `store/ID/VERSION/tree` with lookup, enumerate and
  read-files rights. That is the same tree and the same rights as an explicit
  package root.
- **Where it lives.** The view follows the
  [native package-command view](../interfaces/command-search.md#live-package-command-directories):
  a kernel object over the `packages` volume that reads the set's binary
  projection, not its Lua records. `pkg` adds one record per root to
  `sets/N.commands`, with an empty target for the tree root, so a switch
  publishes both together.
- **Bundles are unchanged.** Bundle launches withhold `package_roots`, so a
  bundle reads only the packages its manifest grants (chronium, 2026-10-21,
  confirming #1079 after review found the first implementation passed the view
  on like other exported roots).

### Authority

The view grants nothing new to the space. Its holder already reads the whole
volume through its `packages` source. The view narrows that: it serves only
the active set's root trees, read-only, by package ID. The one widening is
inside the space: plain programs launched there, which today get the space's
roots but not its private `packages` source, can read those trees. That is the
purpose of the change. A space without the `packages` source gets no view.

### Explicit roots and reboots

Explicit `roots = { NAME = { package = "ID" } }` stay as they are: resolved
once at space start, fixed for the space's lifetime, and able to give a
package another name. With automatic roots they are needed only for that
renaming, for pinning a tree across switches, or for a package kind that gets
no automatic root. Changing an explicit root still applies at next boot.

### Spaces without the packages source

They cannot see the set, so no lookup can hint. Instead, `pkg install` prints
one line for each data package it adds:

```text
pkg: fonts is read as fonts:// in spaces that hold the packages source
```

### Decisions

**Accepted (chronium, 2026-10-21):** all three defaults.

1. **Which kinds get roots.** `data` packages only. Program and
   toolchain trees are reached through commands and bundle grants, and an
   explicit root, such as `toolchain = { package = "clang-hosted" }`, still
   names one when a tool needs its tree. Rejected: every kind, which puts
   program trees beside data in every lookup with no consumer asking for them.
2. **Collisions.** Ordinary roots always win, because they are looked
   up first. A package can never shadow `boot`, `tmp`, `bin`, `system`, `home`,
   `acpi` or any configured root, and an explicit root with the same name as a
   package wins for that space. The packer and `pkg` refuse package IDs equal
   to the fixed names (`boot`, `tmp`, `bin`, `system`, `home`, `acpi`,
   `packages`), so a package can't be installed only to be hidden everywhere.
   Rejected: package roots first, which would let an install change what
   `system://` means.
3. **Set switches while programs run.** Each lookup uses the set
   active at that moment. A handle already opened keeps its tree, which stays
   readable because trees are immutable and boot collection runs only at the
   next boot. So a running program that reopens `fonts://` after a switch sees
   the new version, and a program that needs one version throughout holds its
   first handle. Rejected: pinning the set at process start, which would make
   a long-running shell miss a package installed after it started, the case
   that prompted this proposal.

### Tasks

1. pyxis-os: the `package_roots` view, the root records in the set
   projection, and the format reference.
2. userland: the path helpers' fallback, boot init's grant, `pkg` writing the
   root records, refusing the fixed names, and the install hint.

Order: 1, then 2, after the one-format cleanup (#1078) and stage 2 task 4.

- [x] Implemented (pyxis-os and userland, 2026-10-21): the view, the root
  records, the path fallback in every root lookup (path helpers, generic
  opens, libc descriptors, shell redirects), boot init's grant, the fixed-name
  refusal and the install hint. The format reference describes it under
  [reaching package files](../interfaces/packages.md#reaching-package-files).

Checked in QEMU (installed disk, chronium's `boot.lua` layout): after `pkg
install fonts authcheck-sample` in a shell opened before it, `cat`, `wc`,
`head` and a `<` redirect read `fonts://` and `authcheck-sample://`
files with no reboot; an explicit `fonts` root naming another package won,
with the package still reachable as `package_roots://fonts`; after `pkg
remove`, a shell inside the tree kept it while new lookups failed; `lua`, a
bundle, could not open `fonts://`. Not run: a hand-compiled `font-min.pxe`;
`head` and `wc` stand in for a plain program.
