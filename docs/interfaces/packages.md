# Package formats

Format 1 of the Pyxis package repository: the index, the package manifest, the
file inventory and the archive, as accepted for
[packages slice 1](../wip/packages.md#slice-1-data-packages), and the installed
`packages` volume that programs read them from. The host tool `tools/pxpkg.py`
writes and checks the repository; the `pkg` client that installs it follows in a
later slice 1 task.

There are no signatures. Every file here is untrusted input that only SHA-256
ties together: it catches truncation and corruption, not a hostile repository.

## Literal data

`index.lua` and `package.lua` are Lua files returning one table, parsed with
the literal parser in libpyxis (`userspace/lib/literal.c`), which never runs
the source. Accepted values are tables, strings, finite numbers and booleans;
comments and long strings are allowed. Functions, `nil`, names, calls, operators,
duplicate keys and anything after the returned table are refused. Limits: 1 MiB
of source, 32 nested tables, 8,192 values, 8,192 entries, 8,192 comments and 16 Mi
work units. Each index entry costs 14 values, so one index holds 584 package
versions; the packer refuses the next one before publishing anything.

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
| Kind | `data` or `toolchain` |
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
  format = 1,
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

Every field is required and no other field is allowed. `archive` must be exactly
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
  format = 1,
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
the SHA-256 of `package.files`. ID, version and kind must equal the index entry.

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
on a coexistence install. Only `pkg` writes it:

```text
store/ID/VERSION/     package.lua, package.files and tree/, as in the archive
sets/N.lua            one set record per installed set, N from 1
current               the active set's number
staging/              downloads and unpacking in progress
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
```

Both have exactly these fields. `set` is an integer of at least 1, and
`packages` is a plain array of `{ id, version }` tables following the name rules
above, each ID once. `current` is at most 256 bytes. A missing `current` is the
empty set, as on a new volume. Set records are written once and never changed;
switching sets replaces `current` by rename.

### Reaching package files

Packages are read-only for everyone except `pkg`. A space receives the volume as
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
cannot name the `packages` source itself: the volume, with its set records and
staging area, is reached only per package ID.

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
every lookup then fails with the same cause until `pkg rollback` names a valid
set. There is no automatic fallback to an older set.

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
tools/pxpkg.py pack RECIPE REPOSITORY   # publish one package version
tools/pxpkg.py check REPOSITORY         # verify the index and every archive
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
indexes, and never an index naming a missing archive. If step 5 fails, the new
link is removed. `check` takes a shared lock when `.lock` exists and needs no
write access. The 2026-10-18 toolchain payload (8,701 files, a 370,544,128-byte
archive) packed in 2.6 s and checked in 0.6 s on the shared VM with its sources in
page cache, and GNU tar extracted it identical to the source tree.
