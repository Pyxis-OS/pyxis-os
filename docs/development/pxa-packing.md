# Packing stored Pyxis archives

`tools/pxa.py` is a standalone Linux host tool using Python 3.9 or newer and its standard library.
It packages an unpacked `.pxb` directory and checks the first, stored-only `.pxa`
profile. It is not part of image assembly. Native `.pxa` installation and launch
are not implemented; see the [bundle milestone](../wip/program-bundles.md#zip-tasks-and-owner-assignment).

From the repository root:

```sh
python3 tools/pxa.py pack path/to/example.pxb path/to/example.pxa
python3 tools/pxa.py check path/to/example.pxa
unzip -t path/to/example.pxa
unzip -v path/to/example.pxa
```

Both operations return nonzero with an error on rejection. Packing writes a
seekable temporary file beside the destination, checks it, then replaces the
destination. Failures before replacement preserve an existing destination and
remove the temporary file. This is publication of a checked file, not a promise
of crash durability; the tool does not call `fsync`.

## Source layout and manifest

The source must contain `manifest.json` and an `app/` directory. The complete
source tree is included, including empty directories and extra top-level
metadata such as notices. Keep the tree quiescent while packing: file identity,
size and modification checks detect changes, but do not establish an atomic
snapshot of multiple files or detect every directory addition/removal.

Source symlinks, special files and names that cannot be represented by the
profile are rejected. Descendant opens are anchored to the source directory and
do not follow symlinks. Hardlinks are ordinary source files and are stored as
separate byte sequences. The destination must be outside the source tree.

Manifest validation follows the existing [format-1 contract](../wip/program-bundles.md#manifest-and-stack):
strict UTF-8 JSON, unique decoded keys, unsigned integer numbers, known fields,
native name/path rules, command/resource/grant budgets and binding collisions.
The stack request, when present, must be page aligned and between 1 and 8 MiB;
omission keeps the native 1 MiB default. Every entry and command must resolve
to a regular `app/` file with the `PXE1FLAT` prefix; each resource path must
resolve to a directory. Unknown resources/rights are rejected even for optional
grants. No host permission or ZIP attribute grants native authority.

Prefix recognition is not full P1F validation. The native loader remains
responsible for segments, the independent 256 MiB mapped span, stack collisions
and eager-load cleanup. The shared 128 MiB selected-image capture limit and
available startup grants are runtime checks, not whole-archive limits or a
promise made by this tool.

## Stored-only encoding

The packer writes consistent local/central records with explicit sizes and
CRCs, method 0 and the UTF-8 flag on every name, including ASCII names. It emits
no extra fields, comments, data descriptors or ZIP64. Records are contiguous
from byte zero, followed by the central directory and one end record. The
checker independently parses those records and streams every payload to verify
its CRC; it does not use Python's `zipfile` implementation. Record layouts
follow [PKWARE's ZIP specification](https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT).

Names are case sensitive UTF-8 without normalization or code-page fallback.
Reject duplicate records, file/directory or inferred-parent collisions, absolute
or drive paths, NUL, backslash, empty components, `.` and `..`. Explicit
directory records may match inferred parents. Components are at most 255 UTF-8
bytes. Reject encryption, unsupported flags/methods/versions, split archives,
overlaps, gaps, prefixes/trailing bytes, unknown attributes and symlink/special
Unix types. Ordinary DOS/Unix file attributes are accepted without deriving
native permissions or ownership. Generic ZIP defaults may not comply.

Output is deterministic for the same quiescent tree. `manifest.json` comes
first; remaining records sort by UTF-8 name bytes. Fixed DOS timestamps are
1980-01-01; permissions are ordinary file/directory attributes, independent of
source mode. These timestamps do not establish future native modification
metadata. Deflate, signatures and revision caching are outside this slice.

## Host admission limits

The tool applies the proposal's draft settings: 512 MiB archive bytes and total
file bytes, 16,384 actual records and inferred tree nodes, and a 64 KiB manifest.
Source traversal is incremental; only the admitted inventory is sorted. File
copy and CRC checking use chunks of at most 1 MiB.

The reported 4 MiB host metadata/name budget counts local/central record bytes,
the end record and retained UTF-8 inferred paths. It is not a bound on Python
object memory or proof of the native adapter's future 4 MiB retained index.
Task 2b must define, enforce and qualify that native representation separately.
OS file-descriptor or memory exhaustion is also an error.

## Qualification, 2026-10-11

On the standalone task branch based on main `a617bcd7` and the refreshed ZIP
plan `a10e3c57`, Python 3.14.8 and Info-ZIP UnZip 6.00 checked a sample containing
a built native `echo.pxe` entry, two command aliases, an 8 MiB stack request,
resource directories, a pipe grant, empty files/directories, top-level notices
and `app/data/café-日本.txt`. Packing/checking produced nine records, 78,358 file
bytes and 1,017 host metadata/name bytes. Repacking produced identical bytes.

Python `zipfile.testzip()` and `unzip -t` verified every CRC. Python's independent
reader confirmed method 0, UTF-8 flags, sizes, names and byte-for-byte source
contents; `unzip -v` independently displayed stored records and CRCs. A separate
`zipfile` writer, with the UTF-8 flag explicitly enabled, produced an accepted
archive with inferred parent directories.

Manual rejected-input qualification covered malformed paths/names, duplicate
records, inferred-directory collisions, symlink attributes, deflate, extra
fields/comments, unsupported flags, local/central mismatches, bad payload CRC,
split/count/extent errors, truncation and prefix/trailing bytes; invalid manifest
types/keys/fields/rights, missing entries/resources and stack bounds; source
symlinks/FIFO/socket, invalid UTF-8 names, output inside the source, an oversized
sparse source, the host metadata/name ceiling and more than 16,384 source entries. Invalid manifests rejected
after temporary-file creation and failed final replacement left the previous
output intact and no temporary file behind. These are host checks; no QEMU,
native launch or performance result is claimed.
