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

Manifest validation follows the existing [format-1 contract](../userland/program-bundles.md#manifest-and-stack):
strict UTF-8 JSON, unique decoded keys, unsigned integer numbers, known fields,
native name/path rules, command/resource/grant budgets and binding collisions.
The source of truth is `lib/bundle.c` in the userland repository (the `userspace`
submodule), with JSON syntax/depth rules in `lib/bundle_json.c`. Stack bounds
come from `LAUNCH_INITIAL_STACK_MIN_SIZE` and `LAUNCH_INITIAL_STACK_MAX_SIZE` in
[include/abi/launcher.h](../../include/abi/launcher.h). The host validator copies
these rules, limits and the grant table; changes to either implementation must
update both together.
The stack request, when present, must be page aligned and between 1 and 8 MiB;
omission keeps the native 1 MiB default. Every entry and command must resolve
to a regular `app/` file with the `PXE1FLAT` prefix; each resource path must
resolve to a directory. Unknown resources/rights are rejected even for optional
grants. No host permission or ZIP attribute grants native authority.

Grant requests include the current native keyboard, pointer and private `job_work`
classes, plus external `directory`, `mount` and mount-only `disks` classes. The
external classes require a named `source`; ordinary classes reject that field.
Only a directory request with `lookup` may add a distinct `root` alias. Aliases
must not collide with `app`, bundle resource roots, grant names or another alias.
Grant names beginning with the trusted `source.` prefix are reserved. The native
19-request budget also applies here.

The host can validate declarations and bundle-local collisions, but has no
startup capability inventory. Source availability, actual protocol/rights,
inherited root/resource collisions and the final launch-root budget remain native
checks before publication. A manifest request never authorizes FAT admission or
adds a trusted source. The `disks` table contains enumeration and mount-only
rights, with no raw disk-open right.

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

The manifest validator also admits the declared scoped `dns_udp/open` and
`net_registry/read` roles from the multiple-interface public cut; neither role
adds configuration or policy authority.

## Manifest alignment qualification, 2026-10-13

The refreshed host validator follows native userland `1835b7a`'s grant table,
source fields and alias rules. Python 3.14.8 packed and checked a small fixture
using an existing built native `echo.pxe`, one resource directory and 19 requests,
including keyboard, pointer, private work, external directory/controller and
mount-only inventory declarations. With the directory alias, the archive had
six records, 117,703 file bytes and 676 host metadata/name bytes. The same fixture
without that optional alias also passed packing and checking.

Python `zipfile.testzip()` independently verified every CRC, and its reader
confirmed stored encoding, UTF-8 flags and every source payload byte. Nineteen
literal invalid manifest variants rejected through both `pack` and `check`:
missing or invalid sources, a source on an ordinary class, invalid alias types or
placement, missing directory lookup, app/resource/grant/alias collisions,
reserved source binding names, raw or unknown rights, duplicate rights and a
twentieth request. Every failed pack preserved the existing output and removed
its temporary file. These are host declaration/encoding checks; native startup
grant delivery, FAT admission and ZIP launch remain outside this qualification.
