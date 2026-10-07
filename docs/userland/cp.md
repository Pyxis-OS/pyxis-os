# Copying files

The native `cp` copies regular files within a root or across roots using the
caller's existing grants. Relative paths use the inherited working directory.

```text
cp boot://share/hello.txt home://note.txt
cp boot://share/hello.txt home://
mkdir home://copies
cp home://note.txt boot://share/hello.txt home://copies
cp -- -notes home://notes
```

`cp [--] source-file... destination` accepts one or more literal source files.
An existing directory destination receives each source's basename. Multiple
sources require an existing destination directory; missing parents are not
created. Otherwise the single-source destination is an exact file path.
Existing destination files are replaced; directories are not copied or
replaced as files. Success is quiet. Options other than `--`, including `-r`,
are rejected before opening paths. No modes, owners or timestamps are copied.

## Staged replacement and authority

Each copy creates an exclusive temporary file beside the destination, copies
the contents, closes its file handles and renames that completed file into
place. The destination directory needs LOOKUP, CREATE, WRITE_FILES and REMOVE.
This namespace authority can replace a file without a WRITE grant on the old
file itself. There is no direct-write fallback when staging is unavailable;
having only permission to write an existing file is insufficient.

The parent directory remains held through creation, publication and cleanup.
Those operations use its capability and single-component names, rather than
resolving the original displayed path again. Source files use READ alone and
temporary files WRITE alone. Destination enumeration and file reads are not
required. At most one source, one temporary file and the destination parent
are held at a time; path resolution uses temporary heap workspace.

Temporary names are `.cp-` followed by 16 hexadecimal digits. A copy considers
at most 64 candidates, retries only known name collisions and skips any
candidate equal to the destination leaf. It never opens/truncates a colliding
temporary file, and requires no clock or entropy grant. Each process starts at
`.cp-0000000000000000`; enough colliding names can exhaust the candidate limit.
The reservation error identifies the `.cp-` prefix. Inspect leftovers before
removing them, and leave active copies' temporary names untouched.
Transfers use a fixed
4,080-byte buffer, the smaller native read/write transfer limit, with short
read/write handling.

## Failures and limits

Before publication, copy/read/write/close failure preserves the old destination
and attempts to remove the confirmed temporary reservation. An uncertain WRITE
can have changed data, but not that name's ownership, so one cleanup unlink is
still attempted. Cleanup errors other than an already-absent name report the
possible temporary remainder.

Failed creation returns no confirmed owned file: cp reports the possible
temporary name and never removes it. After publication is attempted, failure
does not establish which name is present; cp reports the destination leaf and
temporary name, without retrying or deleting either. Such unconfirmed creation,
write, publication or cleanup outcomes stop the batch. Ordinary source/copy
failures continue with later operands, returning an aggregate failure status;
stderr failure stops processing. Close errors fail and are not retried. A batch
is not atomic: earlier completed files remain if a later source fails, and a
parent-close failure after publication does not undo the completed copy.

Accepted 2026-10-07: other writers must leave the temporary file/name alone
during the copy. Exclusive CREATE protects initial reservation, but the native
rename/removal APIs select names rather than the held file identity. Another
writer can invalidate this assumption. There is no conditional-publication or
temporary-name reservation mechanism hidden in cp.

The source is held, and copying is bounded by its initially observed size.
Subsequent growth is ignored; premature EOF fails. Concurrent overwrites can
produce mixed contents: this is not a snapshot. Source/destination aliases are
safe from truncation, including copying a file onto itself, but publication
replaces the destination object. Older held handles retain the old object.

Interruption or a crash can leave a temporary file, and cp performs no automatic
stale-file removal. Its success does not promise crash durability. Use explicit
file/parent synchronization, for example `sync home://note.txt home://`, when
durability is needed; see the [filesystem contracts](../interfaces/directories.md#atomic-file-rename).
Provider FILE sources use the normal path resolver; copying a provider URI with
no basename into a directory requires an explicit destination filename instead.

## Validation

The 2026-10-07 baseline was Pyxis `3b84214` with userland `c4bcf16` and ports
`eb648d5`. An ordinary full source `make -j16 image` used the existing GCC
builder, with the host's installed Kconfig Python modules supplied locally.
The changed sources built without warnings; unchanged third-party ports emitted
their existing warnings. No compiler rebuild was required.

Manual QEMU/KVM checks used a nested VM, q35, four CPUs, 512 MiB, OVMF,
virtio-net/rng and a temporary virtio-fs export, with no disks. Baseline
`cat boot://share/iobench.bin > home://baseline.bin` and later cp copies had
the same SHA-256. Copies within RAM and boot/RAM/HOST round trips matched;
the 262,152-byte HOST binary copy was also identical by host byte comparison.
Multiple sources, empty-file replacement, self-copy/dot aliases, relative and
180-byte names, `--`, source errors followed by a successful later source,
temporary-name collision preservation and a destination using the temporary
prefix passed. Directory sources, unsupported options, missing destination
directories and missing intermediate parents failed cleanly.

A denied HOST content read preserved the old destination's hash, and no
temporary names remained in the export. Read-only boot destinations failed
before creation, preserving existing contents. GDB observed exclusive CREATE
on parent rights `0x39` (LOOKUP/CREATE/WRITE_FILES/REMOVE), requesting child WRITE
alone. Subsequent file calls read an initrd source with rights `0x1` and wrote
the RAM temporary with rights `0x2`.

Allocation failure, all-candidate exhaustion, concurrent mutation, interruption
and uncertain creation/write/publication/cleanup paths were inspected in code,
without injected failures. Provider sources, native disk copies, durability and
owner-run ThinkPad use remain unqualified. These are functional/copy-integrity
checks, without an execution-time performance claim.
