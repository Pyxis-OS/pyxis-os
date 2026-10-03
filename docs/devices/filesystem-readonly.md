# Native filesystem format and host tools

The pinned filesystem repository supplies npfs format-only codecs and Linux
formatter/checker/inspector tools. Caelum owns the running inode/cache/writer engine
through the [kernel adapter](filesystem-native-adapter.md). The former persistent
principal/COW format and its shared traversal core are superseded for mounting.

The authoritative [encoding contract](../../fs/docs/npfs-format.md) and
[host-tool guide](../../fs/docs/npfs-host-tools.md) define record offsets,
compatibility, recovery and commands. Initialize `fs` and run `make -j16 fs-tools`
to build `libnpfs-format.a`, `mkfs.npfs`, `fsck.npfs` and
`npfs-inspect` in `build/fs-tools/`. No compiler rebuild is required.

## Repository and platform boundary

pyxis-fs owns freestanding GNU C23 encoding/decoding, checksums, geometry,
timestamp conversion and pointer arithmetic. It supplies no I/O, allocator,
cache, mount state, writer or permission policy. Consumers define its two memory
symbols at link time. Host tools own regular-file descriptors, source traversal,
random identities and advisory locks. Caelum owns capabilities, scheduling,
allocation, device I/O, persistent inode/cache state and journal commits.

## Identity, names and namespace bindings

Pool and volume IDs are nonzero 16-byte identities, without authority. Names are
case-sensitive UTF-8, compared as exact bytes, 1–255 bytes, without NUL, slash,
`.` or `..`. Volume names are unique within a pool. Directory entries name inodes
in that volume; linked directories have an internal parent number. There are
regular files and directories, with no hard links, symlinks or special files.
Namespace schemes and binding names remain userspace data outside the format.

## Ownership and acquisition policy

There are no on-disk principals, owners, users or permission grants. Trusted init
receives mount authority for its configured disk and delegates attenuated file and
directory capabilities. Mount WRITE and OBSERVE independently authorize mutation
and observation requests on roots. IDs and parent fields cannot recover authority
withheld by those grants.

## Physical encoding and committed roots

The format uses 4 KiB blocks, little-endian 64-bit block pointers, a shared bitmap,
64 fixed volume slots, growable inode files and linear directory records. Headers
are checked copies at the first and last complete pool blocks. One pool-wide redo
journal contains whole replacement metadata blocks, with alternating controls.
Ordered data and payload precede durable COMMITTED; home checkpoint and durable
EMPTY precede reuse. Checksums cover headers/journal, not home metadata or contents.

Read-only tools/mounts refuse COMMITTED. Writable `fsck --replay` validates the full
log before home writes, checkpoints and clears it durably, then checks the result.
Unknown required features refuse all opening; unknown read-only-compatible features
forbid writes including recovery; unknown compatible features are ignored. Fsck
checks ownership, names, parent backlinks and cleanup membership. It neither
reclaims pending cleanup nor repairs arbitrary corruption.

## Allocation and capacity accounting

Volumes share the pool without fixed block ranges or quotas. Inodes and file
mappings grow within format limits and free space. Persistent cleanup lists record
orphan/shrink work, and cleared pointers record completed reclamation. Kernel
allocation policy and admission bounds do not belong in format validators.
FILESYSTEM_INFO exposes shared capacity, identities and journal sequence, without
usage/free/charged-byte counters or guarantees. Capacity includes fixed metadata
and reserves; it is not a per-volume allowance.

## Validation

The tool guide records task-2 host validation. Task-3 kernel runtime validation is
pending; the [measurement record](../development/experiments/native-filesystem-task3/README.md)
contains an obsolete-adapter baseline, not a writer result. Successful builds or
structural checks do not establish power-loss behavior or physical SSD wear.
