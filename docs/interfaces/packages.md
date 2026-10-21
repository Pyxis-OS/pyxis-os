# Package formats

Format 2 of the Pyxis package repository: the index, the package manifest, the
file inventory and the archive, as accepted for
[packages slice 1](../wip/packages.md#slice-1-data-packages) and extended with
program packages that record the ABI they need
([stage 2](../wip/packages.md#binding-a-build-to-a-system) and the
[package ABI](../wip/packages.md#the-abi-identifier)), and the installed
`packages` volume that programs read them from.

**Transition:** `tools/pxpkg.py` writes and checks format 2 only. The native
`pkg` client still reads only format 1 repositories until stage 2 task 2, so a
format 2 tree is for that client and for host checks until then. The volume
formats (set records, `current`, `previous`) are unchanged. The host tool `tools/pxpkg.py`
writes and checks the repository; the native `pkg` bundle installs and manages
selected sets. The [slice 1 task record](../wip/packages.md#task-3-details-accepted-owner-2026-10-19)
tracks its delivery and qualification.

There are no signatures. Every file here is untrusted input that only SHA-256
ties together: it catches truncation and corruption, not a hostile repository.

## Literal data

`index.lua` and `package.lua` are Lua files returning one table, parsed with
the literal parser in libpyxis (`userspace/lib/literal.c`), which never runs
the source. Accepted values are tables, strings, finite numbers and booleans;
comments and long strings are allowed. Functions, `nil`, names, calls, operators,
duplicate keys and anything after the returned table are refused. Limits: 1 MiB
of source, 32 nested tables, 8,192 values, 8,192 entries, 8,192 comments and 16 Mi
work units. An index entry without `abi` costs 14 values and one with it 16,
so one index holds 584 unbound or 511 bound package versions; the packer
refuses the next one before publishing anything.

The schemas below accept only the value types they name. `format`, `size`,
`files` and `bytes` are integer literals: `1.0`, `true` or a string is refused,
and so is a positional key in a field table or a named key in an array.

`pxpkg.py` carries a Python port of that grammar, its limits and its work
accounting, including the duplicate-key charge. On 2026-10-18 it agreed with the C
parser, built on the host against stub Lua headers, on every input of two corpora:
the author's 34 cases and the reviewer's 111 cases plus the work-limit tables
(2,600 to 4,090 keys), comparing the parsed values as well as acceptance.

## Names

| Field | Rule |
| --- | --- |
| Package ID | `[a-z0-9][a-z0-9.-]*`, at most 64 bytes |
| Version | `[0-9A-Za-z][0-9A-Za-z.+-]*`, at most 64 bytes; opaque, never compared |
| Kind | `data`, `program` or `toolchain` |
| ABI | `[0-9a-z][0-9a-z.-]*`, at most 64 bytes; for now the kernel's 12-hex `build_revision` |
| File path | relative UTF-8 without NUL or backslash; components not empty, `.` or `..`, at most 255 bytes each and 250 in total; the path and each of its directories must have a USTAR split (below) |

IDs and versions become store directory names, so both start with a letter or
digit. An ID offered by two configured repositories is refused rather than chosen.
A path may not be both a file and a directory: `a` and `a/b` in one package are
refused.

## Repository

```text
index.lua
packages/ID/VERSION.tar
```

```lua
return {
  format = 2,
  packages = {
    {
      id = "fonts-sample",
      version = "1.0.0-1",
      kind = "data",
      archive = "packages/fonts-sample/1.0.0-1.tar",
      size = 8704,
      sha256 = "ae1c9bc93c164a38851d5843497df62093bc89c084afae8fb59c4b2f5629d1c6",
    },
  },
}
```

Every field shown is required and the only other field allowed is `abi`.
`abi` names the one ABI the build requires: it is required on `program` and
`toolchain` and refused on `data`. **For now** it is the build revision of the
system the package was built for; with dynamic linking it becomes a system
library's ABI name, which several releases can provide. Two entries for one ID
may name different ABIs. A client accepts an entry whose `abi` is in the list
the running system provides, by exact string equality, and never parses the
version or compares a release name. `archive` must be exactly
`packages/ID/VERSION.tar`, `size` and `sha256` describe that file, and an ID and
version appear once. Versions of one ID are listed in publication order;
installing an ID without a version takes the last one. Published archives are
never replaced. Only the index, the `packages/` tree and the packer's `.lock`
belong in a repository.

## Archive

A USTAR archive of 512-byte blocks holding regular files and directories only, in
this exact order:

1. `package.lua`, the manifest;
2. `package.files`, the inventory;
3. `tree/`, then for each inventory line in order its missing ancestor
   directories and then the file, as `tree/PATH`.

There are no links, devices, extended or GNU headers, and no members beyond these.
Directories are exactly the ancestors of listed files, so empty directories are
unsupported. Every byte outside headers and member data is zero, and the archive
ends with at least two zero blocks and nothing else.

Because the inventory is sorted by bytes, each directory's contents are
contiguous, so a streaming reader keeps only the current directory stack: a
file's missing ancestors are its directory components that differ from the
previous file's. A reader extracting into a fresh directory also catches a name
that is both a file and a directory, since creating the second one fails.

### Headers

Each header has exactly one encoding, the one the packer writes:

| Bytes | Field | Rule |
| --- | --- | --- |
| 0–99 | name | never empty; NUL-padded |
| 100–147 | mode, uid, gid, size, mtime | zero-padded octal filling the field except a final NUL |
| 148–155 | checksum | six octal digits, NUL, space; the byte sum with this field as spaces |
| 156 | type | `0` for a file, `5` for a directory |
| 157–256 | link name | zero |
| 257–264 | magic, version | `ustar` NUL `00` |
| 265–328 | owner names | zero |
| 329–344 | device numbers | octal as above |
| 345–499 | prefix | NUL-padded |
| 500–511 | padding | zero |

The member name is `prefix/name` when the prefix is not empty, and `name`
otherwise. A full name of at most 100 bytes has an empty prefix; a longer one is
split at the first slash that leaves a prefix of at most 155 bytes and a name of
1–100 bytes. A name with no such split cannot be stored, so the packer refuses
the path. Directory names end in `/` and have size 0; file names don't end in
`/`. Mode, owner, time and device values are not part of the format: the packer
writes 0644 or 0755, owner 0, time 0 and device 0, so packing the same inputs
twice gives the same bytes.

The installed store keeps the same shape in `store/ID/VERSION/`, and a package's
directory source is rooted at its `tree/`, so metadata never mixes with content.

### Manifest

```lua
return {
  format = 2,
  id = "fonts-sample",
  version = "1.0.0-1",
  kind = "data",
  depends = {},
  files = 4,
  bytes = 30,
  inventory_sha256 = "259058ee91d9c377059efc6ca6262da1dec7f0ac247332138085a0a0348e1aca",
}
```

`depends` lists exact `{ id = "...", version = "..." }` pairs, without
duplicates or the package itself. A dependency may come from another configured
repository, so the packer doesn't resolve it; a client refuses an install whose
dependency no configured repository offers. `files` and
`bytes` are the inventory's line count and total size, and `inventory_sha256` is
the SHA-256 of `package.files`. ID, version, kind and `abi` must equal the
index entry, and `abi` follows the same kind rules.

The optional `commands` map explicitly exports command names. Missing or empty
means no exports; neither the packer nor the client discovers them by scanning.
Each value is `{ path = "bin/tool.pxe" }` for a native file, or
`{ path = "tools.pxb", command = "tool" }` for a declared command in an unpacked
bundle. Names use the bundle command rules: 1–255 bytes, neither `.` nor `..`,
with no ASCII control bytes, spaces, slash, colon or backslash. Paths follow the
file-path confinement and USTAR rules above and cannot traverse a `.pxb`
ancestor. Plain paths end in `.pxe`; bundle paths end in `.pxb` and retain the
bundle's complete launch policy. Publication refuses missing or invalid targets,
undeclared bundle commands and duplicate exported names across the selected set.
For a bundle export, the map key equals `command`, preserving that declared
manifest binding rather than adding or remapping a bundle alias.
The host tool checks each package's targets; set publication checks collisions.

### Inventory

One line per file, sorted by path bytes without duplicates, each ending in LF:

```text
8ee9720aa307b726ad9be236c82e83593acd60f7804f7b97ded48bd69715ce18 4 LICENSE
13b81e7e4c9ccc437eff38a44756953701581daf2294ec75d57869bce7f3e8f9 11 sample/Bold.ttf
```

The fields are 64 lowercase hex digits, the size in decimal without leading
zeros, and the path, separated by single spaces. A line is at most 512 bytes
including its LF, a file at most 8 GiB − 1 bytes (USTAR's size field), and a
package at most 65,536 files. A reader streams the inventory line by line; it
never needs the whole list in memory.

## Packages volume

Install creates an npfs volume named `packages` on the pool, and Update creates
it when an older system lacks it. Like `home`, it follows the pool when the
installer rebinds the archive configuration to the pool's actual partition, as
on a coexistence install. `pkg` and trusted boot maintenance manage it through
explicit writable directory authority:

```text
store/ID/VERSION/     package.lua, package.files and tree/, as in the archive
sets/N.lua           immutable set record, N from 1
sets/N.previous      immutable predecessor record for N
sets/N.commands      verified immutable command projection for N
current              the active set's number
current.new          an unpublished replacement pointer during a switch
staging/             exclusive unpacking attempts
.writer-lock         exclusive mutation lock directory
```

`current` and the set records are literal data under the same parser:

```lua
-- current
return { format = 1, set = 7 }

-- sets/7.lua
return {
  format = 1,
  packages = {
    { id = "fonts-sample", version = "1.0.0-1" },
  },
}

-- sets/7.previous
return { format = 1, previous = 6 }
```

Each record has exactly its shown fields. `set` is an integer of at least 1, and
`packages` is a plain array of `{ id, version }` tables following the name rules
above, each ID once. `current` is at most 256 bytes. A missing `current` is the
empty set, as on a new volume. `previous` is a nonnegative integer smaller than
the new set number; zero names the initial empty set. A nonzero predecessor
names an existing set when published. Only the active set's sidecar determines
retention; historical sidecars may name history already collected.

Install, remove and rollback each publish a fresh number above all existing
records. Verified staged trees move to `store/ID/VERSION` with NO_REPLACE and
are synchronized; an existing version must verify against the downloaded
metadata and inventory before reuse. Published trees and set records are never
rewritten. The client validates declared command targets and rejects duplicate exported
names across the proposed set, then writes, verifies and syncs its command
projection alongside the set and predecessor records. Only after these names
are synchronized does it write and sync `current.new`, renames it over `current`, and syncs
the volume directory. See [persistent mutation durability](filesystem-mutations.md#native-persistent-backing).
A switch or final sync failure reports activation as uncertain and retains the
writer lock and old/new evidence for recovery. An unpublished `current.new` or
set record is never automatically activated.

### Command projection and bootstrap

Each new set has `sets/N.commands`, prepared from its explicitly declared
exports and verified targets before `current` switches. Duplicate exported
names refuse the entire switch. The bounded little-endian projection has a
32-byte header (`PXCMDS1` plus NUL, set, count, record size, zero reserved) and
904-byte records sorted by command name. It carries kind, name, package
ID/version, confined tree-relative target and optional bundle command; strings
have zero-padded tails and reserved bytes are zero. No trailing data is allowed.
The [ABI](../../include/abi/package_commands.h) defines the field layout; current
admission is 256 exports per set. It is data, not executable configuration.

Old nonempty sets do not silently acquire exports. Explicit `pkg reindex`, with
management authority and the writer lock, verifies the active packages and
creates only its missing projection. It neither switches `current` nor advances
retention. An existing index, including a damaged one, refuses replacement;
recovery uses an explicitly verified set through rollback. `pkg verify` checks
that the index exactly matches regenerated verified declarations. The boot-only
collector recognizes projections and retains the active/predecessor records.

Consumers can hold a [native package-command view](command-search.md#live-package-command-directories)
without receiving the store root or management authority. Capture selects one
set, including an absent-current empty set. Missing/damaged active projections
refuse; retained selections survive a later switch. Ordinary launch defaults
still use the existing resolver until command-search task 3.

### Reaching package files

Package consumers receive read-only trees. A space receives the volume as
an ordinary read-only directory source named `packages`, which counts against
its eight sources:

```lua
grants = {
  packages = { volume = "packages", kind = "directory",
               access = "read-only", optional = true },
},
```

A bundle names the package it needs in a directory grant, with `package` in
place of `source`, and may give it a root alias. Rights are at most `lookup`,
`enumerate` and `read-files`:

```json
{"name": "doom_data", "resource": "directory", "package": "doom-data",
 "root": "doom_data", "rights": ["lookup", "read-files"], "required": true}
```

The bundle launcher resolves it through the launching process's
`source.packages`: it reads `current`, then the set record, and opens
`store/ID/VERSION/tree` with exactly the requested rights. The set is read once
per launch and serves all of its package grants, so one launch never mixes two
sets; a switch is seen by the next launch, while running programs keep the trees
they were given. A required package that the set doesn't list, whose tree is
missing, or that cannot be resolved because `current` or the set record is
damaged fails the launch, naming the package ID as the missing resource; boot
init's line gives the cause of damage. An optional one is absent. Manifests
cannot name the consumer `packages` source itself: package content is reached
per ID. The client's separate inspection and management sources are described
below.

A space can also name a package as a URI root, for programs that are not
bundles, such as the shell's tools:

```lua
roots = { toolchain = { package = "clang-hosted", optional = true } },
```

Boot init resolves package roots once, when the space starts, from one load of
the set, through the volume named `packages`, which must be an npfs volume.
Package roots are read-only; `access` may only be `"read-only"`, and no root may
be named `acpi`, which `acpi_tables` uses. A required package root that cannot
be resolved stops that space with the reason; an optional one is absent.

After mounting the volume, boot init checks `current` and its set record. A
damaged one never stops boot: boot init prints one line naming the problem, and
every lookup then fails with the same cause until `pkg rollback SET` names a valid
set. There is no automatic fallback to an older set.

## Package client

```text
pkg status
pkg list [--available]
pkg [--timeout SECONDS] install ID[@VERSION]...
pkg remove ID...
pkg rollback [SET]
pkg verify
pkg reindex
pkg unlock
```

`status` and plain `list` print the sampled active set and its entries; `verify`
also checks stored metadata, inventories, file hashes and exact dependencies.
These commands sample `current` once and take no writer lock. `list --available`
reads every configured repository index. Install selects the index's last
version when VERSION is omitted, adds exact dependencies, and refuses conflicting
versions or absent dependencies. Remove refuses a set that breaks a remaining
package's dependencies. Rollback verifies its target locally and publishes a
fresh set containing those entries, so the rollback itself can be reversed.
No package code or install scripts execute.

Mutations require a selected writable npfs `package_management` directory;
local inspection uses the explicit read-only `package_inspect` source. Both
include filesystem-info authority so the client checks the actual npfs volume.
The space-level `package_management = true` gate supplies management and
read-only `package_config` sources; the latter is rooted at `system/config`.
The shipped installed `pyxis` space enables the gate. These sources survive
trusted session, mux and remote handoffs, and ordinary bundles receive them
only through declared grants. The grant holder is trusted to manage the volume;
the executable name does not constrain independently delegated authority.

The client declares optional HTTP and HTTPS
[GET-only provider grants](../userland/program-bundles.md#explicit-provider-grants).
Missing provider authority refuses network commands; local status, verification,
removal and rollback need no network. Missing management authority refuses
mutations. Package consumers receive no raw disk, mount-control or provider
backing authority.

### Repositories and fetches

`package_config/packages.lua`, corresponding to
`system://config/packages.lua`, is strict literal data with exactly this field:

```lua
return { repositories = { "http://horse:8000/public/" } }
```

The shipped list is empty. At most eight distinct HTTP/HTTPS base URLs are
accepted, each at most 2,048 bytes, ending in `/`, without credentials, query or
fragment. `pkg repository add URL` and `pkg repository remove URL` accept the
final `/` optionally, appending it before validation, comparison and storage.
The 2,048-byte bound includes that slash. Duplicate adds refuse without changing
configuration; removal matches either spelling. Add fetches `URL/index.lua`
and applies the existing strict index validation before saving. Unreachable
hosts, HTTP errors and invalid indexes refuse without changing configuration;
there is no offline add. Remove needs no network request. The literal and HTTP
URL parser bounds apply. There is no environment URL override or implicit
repository. Every `index.lua` must pass the existing
1 MiB/schema limits; a failed index or an ID offered by multiple repositories
refuses the command instead of using a partial catalog. Fetch errors name the
full index/archive URL and include the provider detail (including the HTTP
status when a response supplied one).

Fetches are serial GETs through a captured provider root and a configured
[HTTP endpoint](http-transactions.md#configured-endpoints). Each index defaults
to 30 seconds; an archive of `size` bytes gets
`30 + ceil(size / 262144)` seconds. A derived duration above 86,400 seconds
refuses before fetching. The global `--timeout SECONDS` option, before the
command, accepts 1–86,400 and overrides the duration for each fetch, including
indexes. One deadline covers redirects and completed snapshot creation; no
retry, alternate downloader or successful partial archive is used.

The completed READ-only FILE remains held through archive verification and
extraction, and is closed before the next fetch. Installed HTTP uses a separate
private npfs `http-work` volume, created by Install/Update and provisioned only
to trusted provider startup; live HTTP keeps its RAM backing. Large archives are
not allocated whole in client memory or written to a client-visible download
path. Storage exhaustion, provider loss or expiry leaves the active set alone.

Install shows three phases per package: fetch, whole-archive verification and
extraction. When standard output is a console, each is one line redrawn in place
and kept when it ends. Fetch shows the indexed size while the transfer runs
(the provider reports nothing before the body is complete) and the measured size,
time and rate afterwards. Verify shows bytes hashed of the archive and extract
shows files written of the manifest count, each with a percent. Otherwise each
phase prints plain lines once, with no carriage returns. No remaining time is
estimated, and there is no option to change the display.

### Review and recovery

Install, remove and rollback acquire `.writer-lock` by exclusive directory
creation; a concurrent writer refuses immediately. Before switching, the client
prints the proposed additions, selections and removals and requires an
interactive console review with a `[y/N]` prompt. Only exactly `y` or `Y`
accepts; empty input or any other answer cancels without switching the active
set. There is no non-interactive confirmation flag. Archive sizes are printed
before their fetches. Declining leaves any prepared trees inactive. Normal foreground
interruption applies during preparation; the confirmed switch is guarded until
its actual result settles.

Bare `rollback` reads the active set's predecessor. Damaged current/predecessor
metadata requires explicit `rollback SET`, and the selected set is fully
verified before activation. A failed rollback or a command that cannot load the active selection lists
candidate record names. If the prior selection
is unreadable, the verified recovery target becomes the new predecessor, with
an explicit notice that the prior selection is unavailable. A missing `current`
retains its ordinary empty-set meaning.

After an interrupted writer has stopped, `pkg unlock` reports staging entries
and explicitly clears an abandoned lock. It leaves staging contents inactive
and does not cancel a live writer, infer liveness or steal locks by age. Boot
maintenance leaves the lock for this explicit recovery.

### Boot collection

Trusted init collects only before providers or package consumers start, without
network access. It retains the active set, its predecessor, both records'
sidecars and every store version either set references. It deletes other set
records, unreferenced store versions and abandoned staging entries. Missing or
damaged retention evidence skips package collection; it never selects another
set. Historical predecessors are not followed recursively.

Previous boots' private HTTP backing entries are cleaned independently before
package retention checks. Maintenance has a shared five-second budget checked
between operations; native storage waits retain their existing bounds, so this
is not a hard boot deadline. Unsupported entry kinds, cleanup errors, clock
failure or budget expiry stop cleanup and report the reason while boot
continues. Remaining garbage waits for a later boot. Runtime collection is
absent because older running consumers may still hold earlier trees.

## Verification order

A reader accepts a package version only after, in this order:

1. the index parses and passes the rules above;
2. the archive's size and SHA-256 equal its index entry;
3. the first two headers pass and name `package.lua` and `package.files`;
4. `package.lua` parses, matches the index entry, and its `inventory_sha256`
   equals the hash of the `package.files` bytes;
5. reading the inventory and the later members side by side, each line parses,
   the next headers pass and equal the expected ancestors and file, and each
   file's SHA-256 equals its line;
6. after the last line, `files` and `bytes` equal the totals, and only the zero
   end blocks remain.

`pxpkg.py check` applies all six to every index entry and also refuses anything
else in the repository, such as an unlisted archive or a temporary file left by
an interrupted pack.

## Host tool

```sh
tools/pxpkg.py pack RECIPE REPOSITORY              # publish one package version
tools/pxpkg.py pack --build DIR RECIPE REPOSITORY  # a program or toolchain package for DIR's ABI
tools/pxpkg.py prune REPOSITORY --keep-abi ABI...
tools/pxpkg.py check REPOSITORY                    # verify the index and every archive
tools/pxpkg.py check --abi ABI REPOSITORY          # and require a build for ABI
```

A recipe is trusted host input in the same literal format, with the shape of
`boot/initrd.lua` entries. Relative paths start at the recipe's directory, and
`at` is the path inside `tree/` (empty for a tree's root):

```lua
return {
  id = "fonts-sample", version = "1.0.0-1", kind = "data", depends = {},
  entries = {
    { tree = "../src", at = "sample" },
    { file = "../src/notices/OFL.txt", at = "LICENSE" },
  },
}
```

Recipes accept the same optional `commands` map and copy it into the manifest.

`program` and `toolchain` recipes need `--build DIR`, the build directory of
the system they are for. Their entries may also be
`{ build = "PATH", at = "..." }`, a file or directory inside that build
directory. A `program` recipe names a `variant` in place of `version`:

```lua
return {
  id = "doom", variant = "shareware-1.9", kind = "program", depends = {},
  commands = { doom = { path = "doom.pxb", command = "doom" } },
  entries = {
    { build = "ports/doom/stage/bin/doom.pxb", at = "doom.pxb" },
    { file = "../third_party/doom-shareware/doom1.wad", at = "DOOM.WAD" },
  },
}
```

- **ABI:** for now taken from `DIR/kernel-build-revision.h`, the revision the
  kernel embeds. A build outside Git has none and is refused.
- **Checkout:** the Git checkout containing `DIR` must be at that revision and
  have no local changes, untracked files or changed submodules, so that `abi`
  names the commit the bytes came from. A toolchain payload is packed from the
  checkout that produced it; the packer does not read the payload's own
  provenance record.
- **Version:** a program's is `VARIANT+LABEL`. The label is the tag at that
  revision, or the revision when there is none; several tags are refused. A
  variant has no `+`. Packing the same build again refuses as an already
  published version. A toolchain keeps its recipe's `version`.
- `--build` with a data recipe, and `build` entries without `--build`, are
  refused.

`check --abi ABI` also refuses when an ID that has entries with `abi` has none
for `ABI`, listing those IDs: a release-tied package that was not rebuilt. When
it passes it says how many bound IDs it covered.
It is a step of each release.

`pack` refuses symbolic links, special files, unreadable or empty directories, an
empty tree, two sources for one path, a path and directory clash, a full index
and an ID and version that is already published.

Publication, under an exclusive `flock` on `.lock` held for the whole run:

1. remove temporary files and empty `packages/ID/` directories left by an
   interrupted pack;
2. write the archive to a temporary file (mode 0644), fsync it and check it;
3. build and check the new index, so a full index fails before anything is
   visible;
4. hard-link the archive to `packages/ID/VERSION.tar`, which fails rather than
   replacing an existing file, and sync the directory;
5. write the index to a temporary file, fsync it, rename it over `index.lua` and
   sync the directory.

A reader of a served repository therefore sees only whole archives and whole
indexes, and never an index naming a missing archive.

`prune` is the only way to remove published archives. Under the same lock it
removes every entry with an `abi` outside `--keep-abi`, keeping all
unbound entries: it writes and renames the new index and syncs the directory
first, then deletes the archives and empty `packages/ID/` directories. It also
deletes archives the index does not list, which only an interrupted pack or
prune leaves. It refuses, before changing anything, a kept ABI with no
builds and a prune that would remove every build of an ID, so a typo or a prune
before repacking cannot empty the tree. A client holding the older index can
then get HTTP 404 for a pruned archive. If step 5 fails, the new
link is removed. `check` takes a shared lock when `.lock` exists and needs no
write access. The 2026-10-18 toolchain payload (8,701 files, a 370,544,128-byte
archive) packed in 2.6 s and checked in 0.6 s on the shared VM with its sources in
page cache, and GNU tar extracted it identical to the source tree.
