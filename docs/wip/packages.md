# Packages and upgrades

Status: **planned, owner-requested 2026-10-18; the three [decisions](#decisions)
were accepted the same day.** The owner direction and the accepted decisions are
settled. The rest of the [Proposal](#proposal) and the [Staged plan](#staged-plan)
remain proposals, refined per slice. [Slice 1](#slice-1-data-packages)'s three
decisions and task list were also accepted that day; each task still starts with
a short proposal of its open details.

## Problem

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
  a new kernel and ESP, and only from live media
  ([interim program revision directories](../technical-debt.md#interim-program-revision-directories),
  [system updates](../userland/system-updates.md)).
- **Toolchains are installed by hand.** The hosted Clang payload is uploaded with
  `xfer`, unpacked with `tar` and switched with `mv` under `home://toolchain/clang`
  ([clangd](../userland/clangd.md)); old trees stay until `rm -r` removes them
  ([rm](../userland/rm.md)).

## What exists today

| Piece | Behaviour relevant here | Reference |
| --- | --- | --- |
| Bundles | A `.pxb` is a directory with `manifest.json` and `app/`; the manifest names entries, a stack, grants and read-only `resource_dirs` roots. A DIRECTORY grant may name a `source` and a `root` alias; only trusted configuration or shell mount ownership makes a source exist. | [program bundles](../userland/program-bundles.md) |
| Program lookup | Held command-directory lists with live package projections and owned selections, used by the shell, Lua and libuv; no implicit boot, cwd or development-catalog search. | [command lists](../interfaces/command-search.md) |
| Update | Offline, from live media: copy programs to `bin/REVISION`, sync, verify against a SHA-256 inventory, write a verified revision manifest, then rewrite fixed ESP paths. Keeps the new and previous revisions. | [system updates](../userland/system-updates.md) |
| Boot archive | Boot init, configuration, the rescue set and shared data: firmware, `sdk/`, `share/` game data and fonts, assembled by `boot/initrd.lua`. | [system layout](../userland/system-layout.md) |
| npfs | `RENAME` is atomic within one mount; callers explicitly sync to make it durable (COMMITTED). A file may replace a file; a directory may replace only an empty directory. Regular-file hard links exist within one npfs volume; no symbolic links. | [directories](../interfaces/directories.md#atomic-rename-and-directory-moves), [npfs format](../../fs/docs/npfs-format.md) |
| Links | Lookup never follows a symbolic link; only host shares contain them, and they fail with LINK_NOT_FOLLOWED (libc `ELOOP`). Git export rejects symlinks. | [directories](../interfaces/directories.md), [libc](../userland/libc-portability.md#name-limits-and-symbolic-links), [Git](git-on-pyxis.md) |
| HTTP | Responses are completed, re-readable snapshots. RAM backing defaults to 64 MiB per provider; large bodies need private npfs backing. One 30 s deadline by default, explicit longer values allowed; no resume; one exchange at a time. | [HTTP transactions](../interfaces/http-transactions.md) |
| Download cache | `repo.internal` proxies release downloads for host builds; nothing on Pyxis uses it. | [ports](../development/ports.md) |
| Toolchain delivery | Task 4 replaces manual payload publication with the `clang-hosted` package and its declared command exports. | [hosted tools](../development/hosted-tools.md#pool-and-authority) |
| Executables | P1F is fixed-address with no symbols, loaded by the kernel image loader. The only written P2F idea is an undecided P1F superset with an optional symbol table, for backtraces. Nothing defines exported symbols, P1D or P2D. New formats go through the launcher and loader path. | [P1F](../../include/pxe/p1f.h), [backtraces](../technical-debt.md#backtraces-and-symbols), [subsystems](subsystems-per-space.md) |

## Owner direction (2026-10-18)

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
- Real native links are a prerequisite for those stable names; they also unlock
  Git and other tools.
- Order: (1) data packages: game data, fonts, toolchain payloads; (2) program
  packages still tied to a system release, rebuilt per release but delivered as
  packages; (3) a dynamic linking milestone that unties programs from kernel
  revisions.
- The upgrader is a separate program, not the installer. It upgrades through the
  package manager and also replaces the kernel and boot archive, using the owned
  ESP view being built in installer task 5 (#797).

## Proposal

### Packages and repositories

A package is an immutable directory tree with a manifest: its ID, its own
version, its kind (`data`, `program` or `toolchain`), every file's size and
SHA-256, and exact dependencies. A repository is static files over HTTP: an
index listing each package version with its archive size and SHA-256, and one
archive per version. Any HTTP server can serve it.

- **Integrity, recommended:** SHA-256 for each archive in the index and each file
  in its manifest, as Update already does for programs. This catches truncation
  and corruption. It does not authenticate anyone; signatures stay out.
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

Licensed game data never enters the archive: it is packaged only, from the
owner's local inputs. The archive keeps boot init, configuration, the rescue set,
firmware and `sdk/`. Live and tester boots use a volatile packages pool and a
configured network repository (owner direction, 2026-10-19; task-4 details below).

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
3. Until game packages exist, today's image stays as it is: the game programs ship
   in the image and require their data packages (`doom` requires `doom-data`).
   This is a [temporary exception](../technical-debt.md#game-programs-in-the-image).
   0.0.7 closes slice 1 as planned. Game packages are the first program-packages
   work ([stage 2](#staged-plan)) right after 0.0.7.

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

Scheduled after 0.0.7, as part of [stage 2](#staged-plan) alongside the game
packages. Not implemented.

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

The details are a [proposal](pkg-upgrade.md): release package format, booted-ESP
identity, the ESP-write grant, set staging, rollback, failures and qualification.

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
merged and every task 7 gate has passed. The game programs that remain in the
image are the recorded
[temporary exception](../technical-debt.md#game-programs-in-the-image) until the
stage 2 game packages.

### Accepted (owner, 2026-10-18)

1. **Formats in Lua, loaded strictly as data.** The repository index and each
   package manifest are Lua files returning one table. Because trust is out, both
   are untrusted input: they are parsed with the strict literal parser, which
   accepts only a returned table of tables, strings, finite numbers and booleans
   and never executes source ([configuration editor](../userland/configuration-editor.md#shared-validation-and-selection)).
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
`pkg repository add URL` instruction; it never invents a public endpoint.

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
still requires an explicitly configured network repository. The
game programs that remain in the image are a recorded
[temporary exception](../technical-debt.md#game-programs-in-the-image) until the
[game packages](#the-boot-archive) replace them in stage 2.

### Task 6 — horse repository (complete, 2026-10-19)

- [x] Luna prepared both repository trees and the unit from `880ff590` in
  `~/src/pyxis-pkgrepo`; the owner enabled the user unit and LAN ufw rule.
- [x] Orchestrator verified the active service at `192.168.0.213:5245`, public
  index HTTP 200, the personal index and write refusal (PUT HTTP 403).

The public tree currently offers `fonts` at
`nerd3.5.1-plexsans1.1.0-plexserif2.0.0`; the personal tree offers `doom-data`
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

- **Public package mirror (parked, not scheduled; owner idea, 2026-10-19):**
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
