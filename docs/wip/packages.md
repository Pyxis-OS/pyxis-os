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
  in `boot://share/fonts/`.
- **Programs are tied to the kernel revision.** Update copies every program into
  `bin/REVISION` and keeps two complete revisions, so no program changes without
  a new kernel and ESP, and only from live media
  ([interim program revision directories](../technical-debt.md#interim-program-revision-directories),
  [system updates](../userland/system-updates.md)).
- **Toolchains are installed by hand.** The hosted Clang payload is uploaded with
  `xfer`, unpacked with `tar` and switched with `mv` under `home://toolchain/clang`
  ([clangd](../userland/clangd.md)); there is no recursive `rm`, so old trees stay
  ([everyday gaps](everyday-gaps.md)).

## What exists today

| Piece | Behaviour relevant here | Reference |
| --- | --- | --- |
| Bundles | A `.pxb` is a directory with `manifest.json` and `app/`; the manifest names entries, a stack, grants and read-only `resource_dirs` roots. A DIRECTORY grant may name a `source` and a `root` alias; only trusted configuration or shell mount ownership makes a source exist. | [program bundles](../userland/program-bundles.md) |
| Program lookup | `bin://NAME.pxe`, `bin://NAME.pxb`, an explicit development catalog, `boot://`, `home://bin/`. No search path; lookup adds no authority. A generated command index is a parked direction. | [lookup](../userland/program-bundles.md#unpacked-development-lookup), [command index](program-bundles.md#future-command-index) |
| Update | Offline, from live media: copy programs to `bin/REVISION`, sync, verify against a SHA-256 inventory, write a verified revision manifest, then rewrite fixed ESP paths. Keeps the new and previous revisions. | [system updates](../userland/system-updates.md) |
| Boot archive | Boot init, configuration, the rescue set and shared data: firmware, `sdk/`, `share/` game data and fonts, assembled by `boot/initrd.lua`. | [system layout](../userland/system-layout.md) |
| npfs | `RENAME` is atomic within one mount; callers explicitly sync to make it durable (COMMITTED). A file may replace a file; a directory may replace only an empty directory. Every live inode has exactly one namespace reference: no hard or symbolic links. | [directories](../interfaces/directories.md#atomic-rename-and-directory-moves), [npfs format](../../fs/docs/npfs-format.md) |
| Links | Lookup never follows a symbolic link; only host shares contain them, and they fail with LINK_NOT_FOLLOWED (libc `ELOOP`). Git export rejects symlinks. | [directories](../interfaces/directories.md), [libc](../userland/libc-portability.md#name-limits-and-symbolic-links), [Git](git-on-pyxis.md) |
| HTTP | Responses are completed, re-readable snapshots. RAM backing defaults to 64 MiB per provider; large bodies need private npfs backing. One 30 s deadline by default, explicit longer values allowed; no resume; one exchange at a time. | [HTTP transactions](../interfaces/http-transactions.md) |
| Download cache | `repo.internal` proxies release downloads for host builds; nothing on Pyxis uses it. | [ports](../development/ports.md) |
| Toolchain pool | A read-only `toolchain` npfs volume holds `toolchain://REV/payload`, published by hand with `tar`, `sync` and `mv`. | [hosted tools](../development/hosted-tools.md#pool-and-authority) |
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

There are no search paths. A space receives the `packages` volume as one
read-only directory source. A bundle names the package it needs in a DIRECTORY
grant with a `package` ID and its own `root` alias, so a port might read
`doom_data://DOOM.WAD`; the bundle launcher resolves the ID through the current
set at each launch. A space can also name a package as a root in its
configuration, for tools used from the shell, resolved when the space starts.
Each game port and libfont's callers then read from their declared root instead
of `boot://share/`. The [format reference](../interfaces/packages.md#reaching-package-files)
has the exact rules.

### Personal and licensed data

A LAN-only repository on horse, built by a host packer from the owner's local
inputs (the same `DIABLO_DATA`, `DUKE3D_DATA`, `QUAKE_DATA` and `DOOM_WAD` the
personal build uses) and served over HTTP on the LAN. It is never published,
staged in shared locations, built in CI or committed. The client takes a list of
repository URLs from system configuration, so a personal repository sits beside
a public one.

### The boot archive

After slice 1, licensed game data and fonts leave the archive: about 148 MB of the
personal build, leaving about 66 MB, so three copies fit the 512 MiB ESP. The
archive keeps boot init, configuration, the rescue set, firmware and `sdk/`.
Shareware data stays until program packages, because live boots have no pool.

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

The upgrader, in stage 2, uses the same formats to move the installed set to a
release's set, then writes the kernel and boot archive through task 5's owned
ESP view and immutable attempts. Upgrading a running system needs the program
lifetime contract that Update deliberately avoids today.

### Native links (prerequisite milestone)

The separate [native npfs hard-links milestone](npfs-hard-links.md) proposes the
implementation tasks and remaining decisions; the accepted kind/feature choice
below is unchanged. Implementation follows explicit durability task 2b.

npfs has neither link kind: each inode has one namespace reference, and lookup
never follows a link. The format leaves room for both without breaking existing
pools (`include/pyxis_fs/npfs.h`, fs `2533799`): the pool header has
`compatible`, `read_only_compatible` and `required` feature words checked by
`npfs_features_check()`; inodes are 256 bytes with a 64-byte reserve that is
written zero and ignored when decoding; the 16-bit kind has only FREE, FILE and
DIRECTORY; and two of the 32 flag bits are used. A file's parent is zero today
and a directory has exactly one (`format/record.c`).

- **Hard links:** more names for one file inode, in one volume. A link count
  goes in the inode reserve, fsck learns shared references, and reclamation waits
  for zero. A `read_only_compatible` feature bit marks pools that contain one:
  older readers still read them, and older writers refuse them, because they
  would reclaim a shared inode at its first removal. There is no resolution
  step, so capabilities are unchanged: a link is reachable only through a
  directory the caller can already look up. Files only, no directory links.
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
new name, then rename it over `libc.pxl`. Accepted: hard links first, with
symbolic links (store, read, explicit follow) as a later step for Git and ports.

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
3. **Native links:** [hard links first](npfs-hard-links.md), as a separate
   prerequisite milestone in the same fs code as [explicit durability](explicit-durability.md), after its task
   2. Stage 4's stable `.pxl` names depend on it; slices 1 and 2 do not. Symbolic
   links follow later.
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
   shareware stays in the archive for live boots.

### Tasks

Sizes are rough changed lines: S under about 300, M about 300–800, L above 800.

| # | Task | Size | Repository |
| --- | --- | --- | --- |
| 1 | Formats and host packer: the index, manifest and file inventory in [package formats](../interfaces/packages.md); `tools/pxpkg.py` publishes packages from recipes and checks a repository. | M | pyxis-os |
| 2 | The `packages` volume: Install creates it and Update creates it when missing; spaces receive it as one read-only `packages` source; bundles name `"package": "ID"` grants resolved at launch, spaces name package roots, and boot init reports a damaged set. | M | userland |
| 3 | The `pkg` client: index and archive fetch through the HTTP provider, SHA-256 verification, a bounded USTAR reader of its own (`tar` is GPL BusyBox), staging, the store move, set records, the switch, rollback and cleanup after boot. | L | userland |
| 4 | Consumers: the `doom`, `chocolate-doom`, `quake`, `chocolate-quake`, `devilutionx` and `eduke32` ports declare their data source and read that root instead of `boot://share/`; the toolchain becomes a package root, and Neovim's `lsp/clangd.lua` default follows it. | M | ports, userland |
| 5 | The archive cut: the licensed data inputs become packer inputs, and `share/fonts` moves to a package. | S | pyxis-os |
| 6 | The repository on horse: the packer run, the two trees and the dufs user unit, with the owner's enable and ufw steps. | S | host |
| 7 | Qualification in QEMU, then on vela: each game, fonts, the toolchain with clangd, rollback, and `stick-deploy` fitting again. | — | records |

Order: 1, then 2 and 3 in parallel, then 4 and 5, then 6 and 7.

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

### Later ideas

- A read-only web view of the repository beyond dufs's directory listing.

