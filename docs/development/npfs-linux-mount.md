# Mounting Pyxis volumes on Linux

The read-only Linux mount and in-memory journal replay milestone completed on
2026-10-06. Implementation belongs to [pyxis-fs](../../fs/README.md); it changes
no kernel, public ABI or target userland code. The
[host-tool guide](../../fs/docs/npfs-host-tools.md) owns build commands, the device
access recipe and detailed opening/lookup contracts.

## Interface and source ownership

`npfs-fuse SOURCE MOUNTPOINT` mounts a regular pool image or an npfs partition
device, such as `/dev/sdb2`. Each live volume appears as a top-level directory,
for example `system/`. The mount has no concept of Pyxis spaces.

The source is opened read-only and must remain unchanged throughout the mount.
Regular images retain a shared nonblocking `flock`; devices have no external
writer exclusion. Opening validates both headers, feature admission and journal
control selection. Unknown required features refuse the mount. The mount is
private to its mounting user, whose UID/GID own the entries; directory modes are
0555 and regular-file modes are 0444. Use `cp -r --no-preserve=mode` to obtain
ordinary writable copies. The device recipe saves/restores its existing ACL and
runs FUSE as the user rather than root.

Creation/modification times are exposed as read-only `user.npfs.created_ns` and
`user.npfs.modified_ns` xattrs, containing signed nanoseconds or `unknown`.
Linux mtime uses native modification time, or zero when unknown. Linux atime/ctime
use that same mtime because npfs has no access or POSIX change timestamp.

## Committed journal recovery

A committed journal is completely validated using the same payload loader as
writable fsck, then retained in a sorted home-block RAM overlay. Publication
happens only after validation and indexing succeed. Reads of replaced homes use
the journal images; other reads use the source. The view lives until unmount,
without source writes, journal clearing or sequence increments.

RAM recovery uses read-only feature admission. Writable fsck retains its separate
writable/exclusive admission, sequence exhaustion and durable checkpoint rules.
Other image-only readers still require EMPTY unless writable fsck replay is
requested. Invalid payloads or allocation failures refuse the mount. Traversed
metadata is checked; whole-pool ownership validation remains fsck's job.

## Build and qualification

With libfuse3 development files installed, ordinary `make -j16` in pyxis-fs or
`make -j16 fs-tools` in Pyxis includes `npfs-fuse`. The standalone
`make npfs-fuse` target explicitly requires that dependency. Ordinary format tools
remain available without it. The published compiler builder includes libfuse3;
its optional FUSE compilation was directly qualified.

The [initial mount qualification](experiments/npfs-fuse-task1/README.md) records
QEMU-produced files, hashes, image/device access and source preservation. The
[RAM replay qualification](experiments/npfs-fuse-task2/README.md) records an
interrupted current-main write, recovered contents matching a writable-fsck copy,
unchanged source bytes, non-root partition access and matched lookup measurements.
On 2026-10-06 the owner reported successful physical stick mounting and copying
on Arch Linux, completing the remaining hardware step.

There is no GPT selection, automatic mounting or host writer. The source
immutability requirement and host memory costs remain in
[technical debt](../technical-debt.md).
