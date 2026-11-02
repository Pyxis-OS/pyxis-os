# tar

The `tar` package provides BusyBox tar, with the shared GPL-2.0-only license at
`pkg://tar/licenses/busybox/LICENSE`. The shell resolves `tar` to it.
The [recipe's tar notes](../../ports/busybox/TAR.md) identify the upstream
algorithms, adapter and exact format limits.

```text
tar tf boot://share/manuals.tar
cd system://
tar xf tmp://manuals.tar
tar cf tmp://backup.tar system://project
```

The forms are `tf ARCHIVE`, `xf ARCHIVE` and `cf ARCHIVE PATH...`, with an
optional leading dash on the option word, `-f ARCHIVE`, `-C DIR`, `-v`, `-O`,
`-k` and member operands. Archive `-` is stdin/stdout. Extraction uses the
current directory or `-C DIR`. There is no compression or long option. Host
archives must use `tar --format=ustar`.

Archive and source paths use libc scheme resolution and only the caller's
grants. A scheme creation operand stores the path beneath its root:
`system://project/a.c` becomes `project/a.c`. A bare scheme root is refused;
name a file/directory beneath it, or use `.` from the selected working directory.
The reader accepts the ustar prefix field. A member name or link target over
99 bytes is written as a GNU `././@LongLink` record and read back the same way,
so no name length limits creation.

## Members and extraction

Regular files, directories and hardlinks are supported. Symlinks, devices and
FIFOs cannot be created on native filesystems, and PAX extended headers are
skipped. After BusyBox strips unsafe prefixes, a remaining absolute name,
scheme-qualified name, empty name or `..` component is refused before any
filesystem mutation. Header parsing and error handling follow upstream BusyBox
([notes](../../ports/busybox/TAR.md)).

Listing and extraction stream the archive; there is no snapshot or preflight.
An unsafe or malformed member after valid ones leaves the earlier files
extracted and can leave a partially written current file. A read-only
destination fails on its first required mutation.

Extraction ignores owners, permissions and timestamps. Creation writes uid/gid
zero and mode 0644 for files or 0755 for directories; the header time is the
file's native modification time when valid, otherwise zero. These archive values
create no native owners or permission bits.

Extraction overwrites existing files (`-k` keeps them). Later storage errors or
differing rights inside a destination can leave earlier entries; there is no
archive-wide rollback. Creation streams each source through a bounded copy
buffer, so a source that fails or shrinks after output has started leaves partial
output; use a separate output path, because there is no pre-truncation alias
guarantee. Output writes are not atomic. Source enumeration is live, not a
filesystem-wide snapshot, and creation traverses directories recursively. See
[technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1409).

## Validation

On 2026-10-06, for the first implementation (a snapshot design since replaced
by the streaming BusyBox adaptation described above), `make -j16 image` and
interactive QEMU used nested KVM, four CPUs,
512 MiB, patched QEMU 10.2.2, virtio-net and virtio-fs. A Linux
`tar --format=ustar` archive with an empty file, text file, binary file and
nested directories listed/extracted into `tmp://`. Its SHA-256 sums matched
the host. A Pyxis-created archive listed/extracted on Linux with the same sums,
uid/gid zero, 0644/0755 modes and mtime zero.

Archives containing a leading valid file then a symlink, absolute path, `..`
or scheme-qualified path failed before creating that first file. Extraction
into `boot://` reported permission denial and created no tree. Packaged bare
`tar` also extracted, created and listed archives through `host://` and
`tmp://`, and the GPL license was readable from the image. The executable is
65,151 bytes in that build.

A second boot used an npfs `system://` volume on a disposable `/dev/shm` GPT
image (64 MiB pool, 1 MiB journal) and the unchanged installed boot configuration.
Tar extracted `system://linux.tar`, produced the same hashes, created
`system://pyxis-native.tar` and synchronized the root explicitly. After stopping
QEMU, the extracted pool passed host fsck; the inspector exported that archive,
and Linux extracted it with the same hashes. No host block device was used.
General storage/close failures and allocation failure were checked by code
inspection, not injected.
