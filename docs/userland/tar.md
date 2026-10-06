# tar

BusyBox tar is packaged at `boot://tar.pxe`, with the shared GPL-2.0-only license
at `boot://share/licenses/busybox/LICENSE`. The shell resolves `tar` to it.
The [recipe's tar notes](../../ports/busybox/TAR.md) identify the upstream
algorithms, adapter and exact format limits.

```text
tar tf boot://share/manuals.tar
cd system://
tar xf tmp://manuals.tar
tar cf tmp://backup.tar system://project
```

The supported forms are `tf ARCHIVE`, `xf ARCHIVE` and `cf ARCHIVE PATH...`,
with an optional leading dash on the option word. Extraction uses the current
directory. There is no compression, `-C`, member selection or archive
stdin/stdout. Host archives must use `tar --format=ustar`.

Archive and source paths use libc scheme resolution and only the caller's
grants. A scheme creation operand stores the path beneath its root:
`system://project/a.c` becomes `project/a.c`. A bare scheme root is refused;
name a file/directory beneath it, or use `.` from the selected working directory.
The reader accepts the ustar prefix field; creation member names are limited
to 99 bytes, plus the trailing slash on directories.

## Validation before extraction

Only regular files and directories are supported. Links, devices, FIFOs,
absolute paths, `..` components, scheme-qualified names and unsupported archive
extensions fail clearly. Header checksums, octal fields, member data and padding,
two end blocks and zero trailing blocks are checked. Conflicting file/directory
member paths also fail.

The entire archive is captured and validated before any extraction write, and
extraction uses that same byte snapshot. An unsafe member after valid files
therefore leaves the destination unchanged. A read-only destination fails on
its first required mutation without creating partial files.

Extraction ignores owners, permissions and timestamps. Owner decision,
2026-10-06: creation writes uid/gid zero, mode 0644 for files or 0755 for
directories, and mtime zero; extraction ignores times. These archive values
create no native owners or permission bits.

Extraction overwrites existing files. Later storage errors or differing rights
inside a destination can leave earlier entries or a partial current file;
there is no archive-wide rollback. Creation captures all source bytes before
opening/truncating its output, so unsupported/unreadable sources fail before
that write. Output writes are not atomic. Source enumeration is live, not a
filesystem-wide snapshot. Memory holds the archive or all source file contents
plus bookkeeping, and creation traverses directories recursively. See
[technical debt](../technical-debt.md#tar-archive-limits).

## Validation

On 2026-10-06, `make -j16 image` and interactive QEMU used nested KVM, four CPUs,
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
