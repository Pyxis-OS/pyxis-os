# Package formats

The Pyxis package repository: the index, the package manifest, the
file inventory and the archive, as accepted for
[packages slice 1](../wip/packages.md#slice-1-data-packages) and extended with
program packages that record the ABI they need
([stage 2](../wip/packages.md#binding-a-build-to-a-system) and the
[release ABI](abi.md)), and the installed
`packages` volume that programs read them from.

There is one current form of each file and no version field. The set reader
also accepts the previous `{ id, version }` entries as originless during the
0.0.10 transition; other earlier or malformed data is refused. The host tool
`tools/pxpkg.py`
writes and checks the repository; the native `pkg` bundle installs and manages
selected sets. The [slice 1 task record](../wip/packages.md#task-3-details-accepted-owner-2026-10-19)
tracks its delivery and qualification.

[Signed indexes](#signed-indexes) authenticate the repository index and bind
package archives through their sizes and SHA-256 hashes. All files remain
untrusted parser input. Explicitly configured unsigned development repositories
have corruption detection through those hashes, without repository authentication.

## Literal data

Indexes and `package.lua` are Lua files returning one table, parsed with
the literal parser in libpyxis (`userspace/lib/literal.c`), which never runs
the source. Accepted values are tables, strings, finite numbers and booleans;
comments and long strings are allowed. Functions, `nil`, names, calls, operators,
duplicate keys and anything after the returned table are refused. Limits: 1 MiB
of source, 32 nested tables, 8,192 values, 8,192 entries, 8,192 comments and 16 Mi
work units. An index entry without `abi` costs 14 values and one with it 16,
so one index holds 584 unbound or 511 bound package versions; the packer
refuses the next one before publishing anything.

The schemas below accept only the value types they name. `size`,
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
| Kind | `data`, `program`, `toolchain`, `dev` or `release` |
| ABI | `[0-9a-z][0-9a-z.-]*`, at most 64 bytes; a release name, or a development build's 12-hex revision ([ABI](abi.md)) |
| File path | relative UTF-8 without NUL or backslash; components not empty, `.` or `..`, at most 255 bytes each and 250 in total; the path and each of its directories must have a USTAR split (below) |

IDs and versions become store directory names, so both start with a letter or
digit. An ID offered by two configured repositories is refused rather than chosen.
A path may not be both a file and a directory: `a` and `a/b` in one package are
refused.

## Repository

```text
current                  the serial of the published index, then a newline
index/N.lua              published index N, never rewritten
index/N.lua.sig          its signature, in a signed repository
SIGNER.pem               the certificate a signed index names
packages/ID/VERSION.tar
index.lua                pxpkg's working list; clients never read it
```

Clients read `current`, then `index/N.lua`. A publish writes the next index and
then replaces `current`, so `current` is the only file a publish changes and
every other published file is immutable.

```lua
return {
  repository = "public",
  serial = 4,
  signed = "2026-10-23T12:00:00Z",
  signer = "signer-2026.pem",
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

`repository` is the name the repository is configured under, `serial` its
publish count from 1, `current`'s value, and `signed` the UTC publish time,
`YYYY-MM-DDTHH:MM:SSZ`. `signer`, present only in a signed index, names a
certificate file beside `current`. pxpkg's working `index.lua` holds only
`packages`.

In an entry, every field shown is required and the only other field allowed is `abi`.
`abi` names the one ABI the build requires: it is required on `program`,
`toolchain` and `dev` and refused on `data`. It is the release the package was built for,
or, for a package built from a development tree, that build's revision
([ABI](abi.md)). Two entries for one ID
may name different ABIs. A client accepts an entry whose `abi` is in the list
the running system provides, by exact string equality, and never parses the
version or compares a release name. `archive` must be exactly
`packages/ID/VERSION.tar`, `size` and `sha256` describe that file, and an ID and
version appear once. Versions of one ID are listed in publication order;
installing an ID without a version takes the last one. Published archives are
never replaced. Only the files above and the packer's `.lock` belong in a
repository.

## Signed indexes

A signed repository ([decision 71](../wip/pkg-upgrade.md#decided)) carries
`index/N.lua.sig`, a DER ECDSA P-256 signature over the SHA-256 of
`index/N.lua`'s exact bytes, made by the certificate `signer` names. Archives
are covered by the sizes and hashes in the signed index. pkg accepts the index
only when:

1. the signer certificate chains to the Pyxis Software Signing CA, which the
   core archive ships at `boot://share/pyxis/trust/software-signing-ca.pem` and
   which is the only certificate trusted (not the root that issued it);
2. the signer is CA:FALSE, with Digital Signature and Code Signing;
3. both certificates are valid at the index's `signed` time, not the system
   clock, so a machine with a wrong clock still verifies;
4. the signature verifies and `repository` equals the configured name;
5. `serial` is not below, and `signed` not before, the last index this packages
   volume accepted from that repository, and an equal serial has identical
   bytes. Stock signed `public` and `releases` roots also have an independently
   reviewed floor in `boot://share/pyxis/trust/repository-floors.lua`, keyed by
   name, canonical URL and signed policy. A lower serial or earlier signed time
   refuses; an equal serial must have the exact recorded SHA-256. The table is
   a strict version-1 literal with at most eight entries and 16 KiB of source.
   Without its floor, first contact with a stock signed root refuses. A
   user-added signed root absent from that table still uses `signed_floor`,
   the build commit time in `boot://os-release`, at first contact; unreadable
   `os-release` refuses. Explicitly unsigned development roots use neither
   signed floor.

Step 5 stops a replayed index below the reviewed publish state even on a fresh
packages volume. A writer records the accepted serial, time and hash in
`packages/repositories/NAME`, with the repository's URL; another URL under the
name starts from the image's floor again. Any failure names its step and
makes the repository unusable for that command; nothing falls back to
reading it unsigned. The private key never leaves the owner's vault, and only
`tools/sign-repository.sh` on the owner's machine signs.

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
  id = "fonts-sample",
  version = "1.0.0-1",
  kind = "data",
  depends = {},
  files = 4,
  bytes = 30,
  inventory_sha256 = "259058ee91d9c377059efc6ca6262da1dec7f0ac247332138085a0a0348e1aca",
}
```

`depends` lists exact `{ id = "...", version = "..." }` pairs, or `{ id = "..." }`
alone, which any version of that ID satisfies, without duplicates or the
package itself. A dependency may come from another configured
repository, so the packer doesn't resolve it; a client refuses an install whose
dependency no configured repository offers. `files` and
`bytes` are the inventory's line count and total size, and `inventory_sha256` is
the SHA-256 of `package.files`. A package may have no files: a meta-package such
as `base` is only a dependency list, with an empty `package.files` and an empty
`tree/`. A package without files has no `pkg://ID`. ID, version, kind and `abi` must equal the
index entry, and `abi` follows the same kind rules.

The optional `commands` map explicitly exports command names; only `program`
and `toolchain` packages may have one, since commands are native programs that
need an `abi`. A `dev` package names an `abi` but exports no commands. Missing
or empty means no exports; neither the packer nor the client discovers them by
scanning.
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
sets/N.commands      verified immutable command projection for N
current              the active set's number
current.new          an unpublished replacement pointer during a switch
staging/             exclusive unpacking attempts
.writer-lock         exclusive mutation lock directory
```

`current` and the set records are literal data under the same parser:

```lua
-- current
return { set = 7 }

-- sets/7.lua
return {
  packages = {
    { id = "fonts-sample", version = "1.0.0-1", origin = "public",
      origin_hash = "1e8cfe1964de98f1c62772171d0f16e777f6825ccfa8fdf8423c60018ca92b3e" },
  },
}
```

Each record has exactly its shown fields. `set` is an integer of at least 1, and
`packages` is a plain array of package tables following the name rules above,
each ID once. `origin` is the configured repository name; `origin_hash` is the
lowercase SHA-256 of `pkg-origin-v1\nURL\nPOLICY\n`, where URL is the stored
base URL including `/` and POLICY is `signed` or `unsigned`. The pair refuses
silent name reuse after repository reconfiguration. Legacy `{ id, version }`
entries load without an origin until reviewed migration. The example hash uses
`https://packages.pyxis.nexus/public/` and `signed`. `current` is at most
256 bytes. A missing `current` is the empty set, as on a new volume. Older
`.previous` sidecars are ignored and removed by boot collection.

Install, update and remove each publish a fresh number above all existing
records. Verified staged trees move to `store/ID/VERSION` with NO_REPLACE and
are synchronized; a stored version that a set adds back must verify against the
downloaded metadata and inventory before reuse. Published trees and set records
are never rewritten, so a member carried over unchanged from the current set is
not hashed again: install and remove read its stored manifest and check its
identity, ABI and dependencies, never its content. Only added or
changed members are verified. npfs keeps no content checksums, so damage to a
carried-over tree, from the medium or from a write through a read-write
`packages` root, passes into new sets unnoticed until `pkg verify`, which
hashes every member. Launches never hash trees either. The client validates
declared command targets and rejects duplicate exported names across the
proposed set, then writes, verifies and syncs its command projection alongside
the set record. Only after these names are synchronized does it write and sync
`current.new`, rename it over `current`, and sync the volume directory. See
[persistent mutation durability](filesystem-mutations.md#native-persistent-backing).
A switch or final sync failure reports activation as uncertain and retains the
writer lock and old/new evidence for recovery. On a boot-marked installation,
the confirmed switch first synchronizes `.upgrade-pending` with the old and new
set numbers and the unchanged boot identity. It then switches `current`,
publishes `.boot-selection` for the new set, and removes the pending record,
synchronizing each publication. That pending record is durable authorization
for boot-init to finish the confirmed change after interruption, including an
interruption before `current` switches. A new set or `current.new` without this
pending authorization is never automatically activated. Malformed proof, an
existing pending transition, or proof naming a different current set refuses
mutation. An unmarked installer or legacy volume remains unmarked; ordinary
package changes never invent boot evidence or publish ESP files.

### Command projection and bootstrap

Each new set has `sets/N.commands`, prepared from its explicitly declared
exports and verified targets before `current` switches. Duplicate exported
names refuse the entire switch. The bounded little-endian projection has a
32-byte header (`PXCMDS` padded with zeros, set, command count, record size,
root count, zero reserved), then the command records and then the root records,
968 bytes each. Command records are sorted by command name and carry kind,
name, package ID/version, confined tree-relative target, optional bundle command
and the package's `abi`. Root records, one per package with files in the set,
of any kind, are sorted by ID and carry only the directory kind, the ID as name,
and the version; the two lists are separate, so a root may share a command's
name. Strings have zero-padded tails and reserved bytes are zero. No trailing
data is allowed. A command view refuses any other header as a damaged
projection. At most 256 commands are admitted per set. The number of root
records is not fixed: the whole file is at most
`PACKAGE_COMMAND_INDEX_MAX_BYTES` (1 MiB), a quarter of the 4 MiB kernel budget
that package views and their captured indexes share, and `pkg` refuses to
publish a larger set (decisions 56 and 58 of
[the `sdk://` view](../wip/sdk-view.md)).

Each new set also has `sets/N.sdk`, the index of the [`sdk://`
view](#reaching-package-files), built from the inventories of the set's `dev`
packages (decisions 44–46). A file enters the view at its own tree path if it
is under `include/`, directly in `lib/` or directly in `lib/pkgconfig/`; other
files, such as `share/` or `lib/cmake/`, stay at `pkg://ID` only. A path that two
packages provide, or that is a file in one package and a directory in another,
refuses the switch and names both packages:

```text
pkg: dup-dev and pyxis-dev both provide sdk://lib/libc.a
```

The little-endian index has a 32-byte header (`PXSDK` padded with zeros, set,
package count, file count, string bytes, zero reserved), then 136-byte package
records (ID and version, sorted by ID), 16-byte file records (path offset,
path length, package index, zero reserved) sorted by path, and the paths, each
followed by NUL, in record order. Directories are implied by the paths. The
file is at most `PACKAGE_SDK_INDEX_MAX_BYTES` (1 MiB), from the same budget as
the command index, and `pkg` refuses to publish a larger set. The kernel refuses
any other shape as a damaged index, including a duplicate path or a path that
is both a file and a directory; `pkg` refuses those conflicts first, naming the
packages ([layout](../../include/abi/package_sdk.h)).

**ABI at launch.** The kernel carries `abi` in the lookup reply and never
compares it. Command search compares it with the ABIs the running system
provides, read from the launching process's `boot://os-release`
([ABI](abi.md)), and refuses a target whose `abi` it does not provide before
anything runs: the kernel's lookup has already
opened a read handle to the target, which the check closes. The refusal names
the package, both sides and the remedy:

```text
shell: doom: doom shareware-1.9+05fc52104a2c needs ABI 05fc52104a2c; this system provides f107a7adc816; run pkg install doom
```

A launcher that cannot read `boot://os-release` refuses such targets, saying
so. Only
`program` and `toolchain` packages may export commands, so every command record
carries an `abi`. The resolver returns `CALL_DENIED`;
`command_search_resolve_reason`, `program_open_search_reason` and
`program_open_reason` also fill a `COMMAND_REFUSAL_MAX` buffer with the line,
which the shell prints. The Lua and libuv launchers (Lua jobs, Neovim's `:!` and
`clangd`) still call the plain forms and report only the status until they
adopt these.
The [ABI](../../include/abi/package_commands.h) defines the field layout; current
admission is 256 exports per set. It is data, not executable configuration.

Old nonempty sets do not silently acquire exports. Explicit `pkg reindex`, with
management authority and the writer lock, verifies the active packages and
creates only its missing indexes, `sets/N.commands` or `sets/N.sdk`; a set
published before `sdk://` gains its sdk index this way. It neither switches `current` nor advances
retention. An existing index, including a damaged one, refuses replacement;
recovery requires repair or reinstall through rescue. `pkg verify` checks
that the index exactly matches regenerated verified declarations. The boot-only
collector recognizes projections and retains the active record.

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

Every installed package with files is read as `pkg://ID/...` through one live,
read-only view of the volume (`package_roots_open`); there are no per-package
`ID://` roots ([decisions 30 and 39](../wip/sdk-view.md)). Each lookup reads the
`current` selector active at that moment and returns the package's tree with
lookup, enumerate and read-files. The worker shares one validated immutable
projection keyed by mounted-volume domain and set; it still reads `current`
for every fresh live operation. See [cache visibility and capture lifetimes](command-search.md#live-package-command-directories),
including delayed diagnostics for unsupported same-set index edits. The
[QEMU record](../development/experiments/package-projection-cache.md) measures
small through near-limit projections.

**Spaces** get the view as a root in `boot.lua`, only where the `packages`
volume is defined (decision 47):

```lua
roots = { pkg = { view = "pkg", optional = true } },
```

The root must be named `pkg`. The shell and plain programs inherit it like any
root. So
`pkg install fonts` makes `pkg://fonts` readable in shells and programs already
running, and `ls pkg://` lists the installed packages. A handle already opened
keeps its tree after a switch or removal; trees are immutable and collected
only at the next boot.

**`sdk://`** is the development view: the `include/`, `lib/` and
`lib/pkgconfig/` files of every installed `dev` package, merged into one tree
(decisions 32 and 45), such as `sdk://include/stdio.h` from `pyxis-dev` and
`sdk://include/c++/v1/vector` from `libcxx-dev`. A space gets it as a root
named `sdk`, from `DIRECTORY_PACKAGE_SDK` (`package_sdk_open`) on the volume named
`packages`; the kernel refuses the view on any other volume:

```lua
roots = { pkg = { view = "pkg", optional = true },
          sdk = { view = "sdk", optional = true } },
```

The view is live: each lookup reads `current`, so a `dev` package is in
`sdk://` as soon as its set publishes. The kernel keeps one validated index,
that of the current set it last read, for the whole system, and reads
`sets/N.sdk` again only when `current` names another set; a bundle's capture of
an older set reads that set's index without replacing it. A directory or file
opened through the view belongs to the set read at that lookup, and an open
directory, such as a shell's current directory in `sdk://include`, keeps that
whole index in memory until it is closed. Without `dev` packages, `sdk://`
is empty; live boots have no SDK until `pkg install pyxis-dev`
(decision 54). The shipped installed and live configurations give both views to
every space that holds the `packages` source; `pkg` and `sdk` name only these
view roots.

The views' own directories, `pkg://`, a command view such as `bin://`, and
`sdk://` and each directory under it, are synthetic, with identity in one
package-view domain, so `stat` and `realpath` work on them and hosted Clang
can search them ([file metadata](file-metadata.md#identity-and-lifetime),
decision 62). Each view root opened is an object of its own. A directory under
`sdk://` has the same identity on every lookup through the same index, so
across lookups that read the same set. Files, and the package trees `pkg://ID`
returns, keep their store identity.

**Bundles** inherit neither the space's `pkg://` nor the `packages` source.
A bundle's manifest lists the packages it reads, and its `pkg://` holds only
those (decision 48):

```json
"packages": ["doom"],
"sdk": true,
```

`"sdk": true` gives a bundle a read-only `sdk://` of the same captured set, and
`"sdk": "optional"` launches it without one when none is available. A bundle
launched by another bundle, which holds no packages source, receives that
bundle's own `sdk://`, read-only, so it sees the set captured when its
launcher started (decision 59).

The launcher captures the set once per launch through the launching process's
`source.packages`, so one launch never mixes two sets, and selects each listed
package from that capture (`package_roots_select`). A package-selected command
keeps its own captured set, bound to an independently held `source.packages`
from the same volume. A listed package the set doesn't have fails the launch,
naming that package ID; a damaged `current` or set record fails it naming the
first listed ID. Manifests
cannot name the consumer `packages` source itself, and no other grant or
resource directory may be named or aliased `pkg` or `sdk`. The client's separate inspection and management sources
are described below.

After mounting the volume, boot init checks `current` and its set record. A
damaged one never stops boot: boot init prints one line naming the problem, and
every lookup then fails with the same cause until repaired. There is no
automatic fallback to an older set.

## Package client

```text
pkg status
pkg list [--available]
pkg [--timeout SECONDS] install [REPO/]ID[@VERSION]...
pkg remove ID...
pkg verify
pkg reindex
pkg unlock
pkg upgrade [VERSION]
pkg repository list
pkg repository add [--unsigned] NAME URL
pkg repository remove NAME
```

`status` and plain `list` print the sampled active set and its entries; `verify`
also checks stored metadata, inventories, file hashes and exact dependencies.
These commands sample `current` once and take no writer lock. `list --available`
reads every configured repository index and prints each offer as
`REPO/ID@VERSION`. Install takes, from each repository, the last build its
index lists for this system, at VERSION when given, adds exact dependencies, and refuses conflicting
versions or absent dependencies. A dependency by ID alone keeps the installed
version, or takes the last build offered for this system when the ID is absent,
so installing or upgrading one member never conflicts with it. Remove refuses a
set that breaks a remaining package's dependencies, naming both packages: while
`base` is installed its members stay, and removing `base` first lets them go.
No package code or install scripts execute.

**Choosing a repository.** `REPO/ID` and `REPO/ID@VERSION` look only in the
named repository. A plain ID offered for this system by one repository is taken
from it without a question. Offered by several, the console shows a numbered
choice before anything is fetched:

```text
pkg: notes is offered by 2 repositories
  1  alpha/notes@1.0
  2  beta/notes@2.0
Choose 1-2, or press Enter to cancel:
```

Enter or any other answer cancels, with nothing fetched or changed. Without a
console, `pkg` prints the same list and refuses, asking for `REPO/ID`, or
`REPO/ID@VERSION` when a version is known.

A dependency is found only in the manifest of a package already fetched. A
dependency the store already holds at its exact version is hashed in place,
not fetched again. Otherwise it follows the same rules, and its choice comes
when it is reached, as `pkg: dependency ID@VERSION is offered by N
repositories`; cancelling there leaves the trees fetched so far inactive and
the active set unchanged. The review names the repository of every
package the operation fetches, for example `add notes@2.0 (beta)`. Which
repository a package came from is not yet recorded in the set; until it is,
reinstalling an ID offered by several repositories asks again.

**ABI.** `pkg` reads the ABIs the running system provides from
`boot://os-release`: its release ABI list and its own build revision
([ABI](abi.md)). A
`program`, `toolchain` or `dev` entry whose `abi` the system does not provide
is not offered: `list --available` omits it and ends with
`N builds for other systems hidden`, and installing it refuses as not available
for this system, counting that ID's hidden builds. Before verifying anything,
`pkg` reads every stored manifest in the set it would keep, so `install`,
`remove` and `verify` refuse a set with stale builds once, naming
all of them and one command of each kind that clears them together:

```text
pkg: 2 installed packages are not built for this system, which provides f107a7adc816: doom (needs ABI 05fc52104a2c), quake (needs ABI 05fc52104a2c). Replace with: pkg install doom quake. Or drop with: pkg remove doom quake
```

IDs the same install fetches anew are not counted. Without a readable
`boot://os-release`, every bound package is refused, saying the running ABI is
unreadable rather than that builds are for other systems.

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
removal needs no network. Missing management authority refuses
mutations. Package consumers receive no raw disk, mount-control or provider
backing authority.

### Repositories and fetches

`package_config/packages.lua`, corresponding to
`system://config/packages.lua`, is strict literal data with exactly this field:

```lua
return {
  repositories = {
    { name = "public", url = "http://192.168.0.213:5245/public/" },
    { name = "personal", url = "http://192.168.0.213:5245/personal/" },
  },
}
```

The image's list holds `public` at the build's `PACKAGE_REPOSITORY`
([boot archive](../development/boot-archive.md)); Install seeds it into the
installed system. At most eight entries are accepted, each a table
holding exactly `name` and `url`, plus `signed = false` for an unsigned
development repository ([decision 72](../wip/pkg-upgrade.md#decided)).
Only development images write `signed = false`: the build writes it unless
`PACKAGE_REPOSITORY_SIGNED=1`. An index without a signature is refused unless
its entry says so; the entry is the only way to read one. Names follow the package ID rules, so
`REPO/ID` parses without ambiguity; they are chosen by the user, never derived
from the URL, so a server's path layout does not become a contract. URLs are
HTTP/HTTPS base URLs of at most 2,048 bytes, ending in `/`, without credentials,
query or fragment. Names and URLs are each distinct. Any other form, including
the earlier list of bare URLs, is refused as invalid and named.

`pkg repository add NAME URL` accepts the final `/` optionally, appending it
before validation, comparison and storage; the 2,048-byte bound includes that
slash. Adding a name or URL that is already configured refuses without changing
configuration, naming the existing entry. `pkg repository remove NAME` removes
the entry; installed packages stay usable, because the store is local. Add reads
the repository as every later command will, verifying its signature, or for
`pkg repository add --unsigned NAME URL` without one, and saves nothing on
failure. Unreachable
hosts, HTTP errors and invalid indexes refuse without changing configuration;
there is no offline add. Remove needs no network request. The literal and HTTP
URL parser bounds apply. There is no environment URL override or implicit
repository. Every index must pass the existing
1 MiB/schema limits; a failed index refuses the command instead of using a
partial catalog. Fetch errors name the
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

Install, update and remove acquire `.writer-lock` by exclusive directory
creation; a concurrent writer refuses immediately. Before switching, the client
prints the proposed additions, selections and removals and requires an
interactive console review with a `[y/N]` prompt. Only exactly `y` or `Y`
accepts; empty input or any other answer cancels without switching the active
set. `--yes` answers only this final switch prompt; it never chooses between
repositories or approves a removal proposed by update. Archive sizes are printed
before their fetches. Declining leaves any prepared trees inactive. Normal foreground
interruption applies during preparation; the confirmed switch is guarded until
its actual result settles.

A damaged active selection refuses mutation and needs repair or reinstall
through rescue. A missing `current` retains its ordinary empty-set meaning.

After an interrupted writer has stopped, `pkg unlock` reports staging entries
and explicitly clears an abandoned lock. It leaves staging contents inactive
and does not cancel a live writer, infer liveness or steal locks by age. Boot
maintenance leaves the lock for this explicit recovery.

### Boot collection

Trusted init collects only before providers or package consumers start, without
network access. It retains the active set and every store version it references.
It deletes other set records, unreferenced store versions and abandoned staging
entries. Missing or
damaged retention evidence skips package collection; it never selects another
set. Legacy predecessor sidecars are deleted without reading their contents.

Previous boots' private HTTP backing entries are cleaned independently before
package retention checks. Maintenance has a shared five-second budget checked
between operations; native storage waits retain their existing bounds, so this
is not a hard boot deadline. Unsupported entry kinds, cleanup errors, clock
failure or budget expiry stop cleanup and report the reason while boot
continues. Remaining garbage waits for a later boot. Runtime collection is
absent because older running consumers may still hold earlier trees.

### Release upgrade

A `release` entry ([decision 73](../wip/pkg-upgrade.md#decided)) carries no
`abi` and is the only kind with `provides`, the non-empty list of ABIs that
release runs:

```lua
{
  id = "pyxis", version = "0.0.8", kind = "release",
  archive = "packages/pyxis/0.0.8.tar", size = 35545088, sha256 = "...",
  provides = { "0.0.8" },
},
```

Its archive holds `caelum.elf`, `initrd.cpio` and `os-release` exactly as a
tagged build produced them. Release entries are never offered to `install`,
and only `pkg upgrade` reads the repository configured as `releases`, which is
always verified ([decision 74](../wip/pkg-upgrade.md#decided)).

`pkg upgrade [VERSION]` refuses without the space's boot ESP grant and boot
evidence ([init](../userland/init.md)), so live media, rescue boots and spaces
without `package_upgrade` cannot upgrade. Under the writer lock it then:

1. reads `releases` and takes its last `pyxis` entry, or VERSION, refusing the
   release already running;
2. reviews the running and new release, the archive size, `provides`, and the
   installed packages whose `abi` the new release does not provide, then asks
   `[y/N]` (`--yes` skips the question);
3. fetches and verifies the archive into `packages/.upgrade/`, checks that its
   `os-release` names the release, and copies the kernel and archive into a
   RAM workspace in `tmp://`, since the ESP takes RAM transfer sources only;
4. admits the boot ESP once through `UPGRADE_BOOT_ESP`, checks that its boot
   record is still the one this boot read, and publishes the new attempt with
   the shared boot publisher, the boot entry's `mount.pool` from the evidence;
   the current selection is replaced; the static rescue entry remains independent;
5. removes the staging and prints `upgrade staged: reboot to run VERSION`.

Nothing is written before step 4. The admission is single-use per boot, so a
failure from step 4 on needs a reboot before the next attempt; inspect the
reported publication phase and boot selection before rebooting. Packages upgrade separately, as
ordinary packages.

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
tools/pxpkg.py pack --build DIR RECIPE REPOSITORY  # a program, toolchain, dev or variant package for DIR
tools/pxpkg.py prune REPOSITORY --keep-abi ABI...
tools/pxpkg.py publish --repository NAME [--signed-at TIME] [--signer CERT.pem] REPOSITORY
tools/pxpkg.py check REPOSITORY                    # verify the indexes and every archive
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

`program`, `toolchain` and `dev` recipes need `--build DIR`, the build
directory of the system they are for. Their entries may also be
`{ build = "PATH", at = "..." }`, a file or directory inside that build
directory. A `tree` or directory `build` entry may add `exclude = { "NAME" }` to
leave out entries at its top level, as `packages/dev/pyxis-dev.lua` leaves
`c++` to `libcxx-dev`. A `program` recipe names a `variant` in place of
`version`:

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

- **ABI:** taken from `DIR/os-release` ([ABI](abi.md)): for a tagged release
  build, the release's ABI name, which must be in its own `abi` list (0.0.8a
  names 0.0.8); otherwise the build revision. A build outside Git has no
  revision and is refused.
- **Checkout:** the Git checkout containing `DIR` must be at that revision and
  have no local changes, untracked files or changed submodules, so that `abi`
  names the commit the bytes came from. A toolchain payload is packed from the
  checkout that produced it; the packer does not read the payload's own
  provenance record.
- **Version:** a program's is `VARIANT+LABEL`. The label is the release tag,
  such as `0.0.8a`, or the revision. HEAD must have at most one tag, an ABI
  name, matching `os-release`'s `release`, or the build is refused. A
  variant has no `+`. Packing the same build again refuses as an already
  published version. A toolchain keeps its recipe's `version`; a `dev` recipe
  names either, as `pyxis-dev` and `libcxx-dev` name `variant = "1"`.
- **Variants of one build:** a `data` recipe may also name a `variant`. It then
  needs `--build DIR` and is versioned `VARIANT+LABEL` like a program, without
  an `abi`. `packages/base.lua` does this, and names its members by ID alone
  ([program packages](#program-packages)).
- `--build` with a data recipe that names a version, and `build` entries without
  `--build`, are refused. A toolchain names its version.

### Program packages

`packages/programs/` holds one recipe per program built in this tree (variant
`1`), with the licenses of the third-party code it carries; a bundle that carries
its own licenses needs nothing more. `packages/base.lua` names the base set
([pkg upgrade](../wip/pkg-upgrade.md#base-package-set)); every other recipe there
is a development program. From a build directory:

```sh
make base-packages REPOSITORY=DIR         # every base program and base
make development-packages REPOSITORY=DIR  # every other program
make release-package REPOSITORY=DIR       # this tagged build's release
make release-image IMAGES=DIR             # this tagged build's install ISO
```

`release-package` packs `packages/release.lua` from a clean build of a release
tag: version the tag, `provides` the build's `os-release` ABI list. It does not
publish; `tools/sign-repository.sh releases DIR` does.

`release-image` (`pxpkg image`) copies the same build's `pyxis.iso` to
`DIR/pyxis-VERSION.iso` and adds its line to `DIR/SHA256SUMS`, keeping the
lines of earlier releases ([decision 75](../wip/pkg-upgrade.md#decided)). A
name already listed with other bytes is refused. `tools/sign-images.sh DIR`
then signs `SHA256SUMS`
([signing](../../packages/signing/README.md#signing-the-install-isos-and-images)).

Both targets refuse an untagged build. They also refuse a build whose
`packages.lua` lists an unsigned repository, so a release image is built with
`PACKAGE_REPOSITORY=https://packages.pyxis.nexus/public/` and
`PACKAGE_REPOSITORY_SIGNED=1`.

`check --abi ABI` also refuses when an ID that has entries with `abi` has none
for `ABI`, listing those IDs: a release-tied package that was not rebuilt. When
it passes it says how many bound IDs it covered.
It is a step of each release.

`pack` refuses symbolic links, special files, unreadable or empty directories,
two sources for one path, a path and directory clash, a full index
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
indexes, and never an index naming a missing archive. If step 5 fails, the new
link is removed. `check` takes a shared lock when `.lock` exists and needs no
write access. The 2026-10-18 toolchain payload (8,701 files, a 370,544,128-byte
archive) packed in 2.6 s and checked in 0.6 s on the shared VM with its sources in
page cache, and GNU tar extracted it identical to the source tree.

`publish` makes the working index visible to clients: under the same lock it
writes `index/N.lua` with the next serial, the given `--repository` name and
`--signed-at` (default now, never before the previous index's), then replaces
`current`. `--signer` copies that certificate beside `current`, refusing a
different file under a published name, and names it in the index, which
`tools/sign-repository.sh` then signs before anything is uploaded. Without
`--signer` the index is unsigned, for development repositories. `make
base-packages`, `development-packages` and `game-packages` end with an unsigned
publish as `REPOSITORY_NAME` (default `public`); an empty name skips it, for a
tree the signer script will publish. `check` also verifies that the published
index lists only archives present with the indexed size and hash, so a `prune`
must be followed by a `publish`.

`prune` is the only way to remove published archives. Under the same lock it
removes every entry with an `abi` outside `--keep-abi`, keeping all
unbound entries: it writes and renames the new index and syncs the directory
first, then deletes the archives and empty `packages/ID/` directories. It also
deletes archives the index does not list, which only an interrupted pack or
prune leaves. It refuses, before changing anything, a kept ABI with no
builds and a prune that would remove every build of an ID, so a typo or a prune
before repacking cannot empty the tree. A client holding the older index can
then get HTTP 404 for a pruned archive.
