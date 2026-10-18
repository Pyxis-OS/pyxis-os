# Package formats

Format 1 of the Pyxis package repository: the index, the package manifest, the
file inventory and the archive, as accepted for
[packages slice 1](../wip/packages.md#slice-1-data-packages). The host tool
`tools/pxpkg.py` writes and checks them. Nothing on Pyxis reads them yet; the
client and boot init follow in later slice 1 tasks.

There are no signatures. Every file here is untrusted input that only SHA-256
ties together: it catches truncation and corruption, not a hostile repository.

## Literal data

`index.lua` and `package.lua` are Lua files returning one table, parsed with
libconfig's literal parser (`userspace/libconfig/literal.c`), which never runs
the source. Accepted values are tables, strings, finite numbers and booleans;
comments and long strings are allowed. Functions, `nil`, names, calls, operators,
duplicate keys and anything after the returned table are refused. Limits: 1 MiB
of source, 32 nested tables, 8,192 values, 8,192 entries, 8,192 comments and 16 Mi
work units. Each index entry costs about 14 values, so one index holds about 580
package versions.

`pxpkg.py` carries a Python port of that grammar and its limits. On 2026-10-18 it
agreed with the C parser, built on the host against stub Lua headers, on a
34-case corpus of accepted and refused inputs, including the value-count boundary.

## Names

| Field | Rule |
| --- | --- |
| Package ID | `[a-z0-9][a-z0-9.-]*`, at most 64 bytes |
| Version | `[0-9A-Za-z.+-]`, 1–64 bytes; opaque, never compared |
| Kind | `data` or `toolchain` |
| File path | relative UTF-8, components not empty, `.` or `..`, at most 255 bytes each and 250 in total, representable in USTAR as `tree/PATH` (name at most 100 bytes after a prefix of at most 155) |

An ID offered by two configured repositories is refused rather than chosen.

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
      size = 10240,
      sha256 = "0efb066964e3e80eca29befdf87ed90c159b7ed3c475fec3121013cc2db9b040",
    },
  },
}
```

Every field is required and no other field is allowed. `archive` must be exactly
`packages/ID/VERSION.tar`, `size` and `sha256` describe that file, and an ID and
version appear once. Versions of one ID are listed in publication order;
installing an ID without a version takes the last one. Published archives are
never replaced. The packer writes the archive first and the index last, each
through a temporary name and a rename, so a reader never sees an index that names
a missing archive.

## Archive

A USTAR archive with plain headers (`ustar\0` `00`), regular files and
directories only, in this exact order:

1. `package.lua`, the manifest;
2. `package.files`, the inventory;
3. `tree/`, then for each inventory line in order its missing ancestor
   directories and then the file, as `tree/PATH`.

There are no links, devices, extended or GNU headers, and no members beyond these.
Directories are exactly the ancestors of listed files, so empty directories are
unsupported. Every byte outside headers and member data is zero, and the archive
ends with at least two zero blocks. Modes, owners and times are not part of the
format; the packer writes 0644/0755, owner 0 and time 0, so packing the same
inputs twice gives the same bytes.

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

`depends` lists exact `{ id = "...", version = "..." }` pairs. `files` and
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

## Verification order

A reader accepts a package version only after, in this order:

1. the index parses and passes the rules above;
2. the archive's size and SHA-256 equal its index entry;
3. the headers, member order, types and zero padding pass;
4. `package.lua` parses, matches the index entry and names the inventory's hash;
5. each inventory line parses, and each member's size and SHA-256 equal its line.

`pxpkg.py check` applies all five to every index entry and also refuses an
archive under `packages/` that the index doesn't list.

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

`pack` refuses symbolic links, special files, empty directories, two sources for
one path and an ID and version that is already published. It checks the new
archive before publishing it. The 2026-10-18 toolchain payload (8,701 files, a
370,544,640-byte archive) packed in 3.2 s and checked in 0.7 s on the shared VM
with its sources in page cache.
