# File and terminal stdio

The freestanding C library exposes an unbuffered `FILE` subset in
[`stdio.h`](https://git.internal/chronium/pyxis-userland/src/branch/main/libc/include/stdio.h). It uses native file, directory and
console capabilities; there is no kernel descriptor table or POSIX syscall layer.
Libpyxis still returns native statuses. Libc translates failures into `errno`.

## Opening and ownership

`fopen` accepts `r`, `w`, `a` and their `+` forms. Optional `b` is accepted before
or after `+` and has no effect. Other extensions and duplicated mode letters are
rejected. `r` requires an existing readable file; `w` creates or truncates a
writable file; `a` creates if absent and chooses the current end before each
write. `+` adds both read and write authority. `a+` starts reading at offset zero;
`a` starts at the existing end. Seeking never disables append-on-write.

Paths use the existing [capability path rules](paths.md), including scheme roots
such as `home://notes.txt` and the initial working-directory chain for relative
paths. No process-global chdir or implicit filesystem authority is added. The
startup roots/chain are borrowed during lookup: explicitly closing those native
grants can make later fopen calls fail. An already open stream retains its own
handle independently.

Only the final file may be created; intermediate directories must exist. Lookup
of an existing writable file does not require CREATE. A competing creator is
handled by opening the entry it published. Temporary path storage is allocated
from the path length and initial chain depth, without a fixed path/depth limit.
Allocation and authority resolution precede truncation. Closing a FILE releases
its owned handle and metadata, not the file's directory entry.

Append currently performs separate SIZE and WRITE calls. Concurrent appenders
can choose the same end and overwrite one another. This is explicitly not an
atomic append guarantee; see [technical debt](technical-debt.md#non-atomic-stdio-append).

## Removal

`remove(path)` removes a file or empty directory through the native parent
capability, returning zero on success or -1 with errno on failure. It uses the
same startup roots and initial working-directory chain as fopen. Selection and
removal happen in one kernel operation, without first probing the child type.
Existing FILE streams keep their original object after removal; reopening the
name fails unless another entry has been created there. Roots and final `.` or
`..` are rejected, and a trailing slash requires a directory.

Nonempty directories report ENOTEMPTY. This is nonrecursive removal, without
unlink/rmdir syscall adapters or a process-wide file descriptor table. Persistent
storage remains separate work.

## Rename

`rename(old_path, new_path)` performs atomic file rename/replacement through the
native directory protocol. It returns zero on success or -1 with errno on
failure, using startup roots and the initial directory chain just like fopen.
The destination is an exact file path; an existing directory is not interpreted
as a request to append the old basename. Directory moves, trailing separators,
roots and final `.`/`..` are unsupported. No copy-and-delete fallback exists.

Source-parent REMOVE and destination-parent CREATE are required, with destination
REMOVE when replacing another file. Existing streams keep their original file
objects even when a destination name is replaced. A failed operation does not
remove either entry, and replacement exposes no missing-destination interval.
Renaming an existing file to itself succeeds. These are namespace guarantees
within the RAM filesystem, not disk durability or a whole-path snapshot.

## Transfers and positions

`fread` and `fwrite` return complete element counts and check size/count overflow
before I/O. A partial final element may have transferred bytes even though it
is not included in that count. File position belongs to each FILE, with no
shared seek position in the underlying capability. File writes use that offset;
terminal output completes partial console writes until done or an error occurs.

`fgetc`/`getc`/`getchar`, `fgets`, `fputc`/`putc`/`putchar`, `fputs` and `puts` are
provided. `fgets` retains a newline and terminates successful input. Capacity one
produces an empty string without consuming input; a nonpositive capacity fails.
After a read error the destination may contain a partial, unterminated prefix.
Terminal input is raw and blocking: fread waits for the requested bytes or an
error; fgets stops at newline/capacity. Neither echoes or edits. Interactive line
editing remains an explicit [libterm](terminal.md) operation. Do not read from
stdin while a foreground child or another reader owns that input stream.

A zero-byte file read for a nonempty request sets EOF. Merely reading exactly to
the end does not set it until a later read attempts more. Terminal input has no
EOF convention; input loss sets EIO and the error indicator. `feof` and `ferror`
remain set until cleared: `clearerr` clears both, successful `fseek` clears EOF,
and `rewind` clears both. Indicators do not reset errno. An error does not itself
prevent retrying I/O; EOF suppresses reads until cleared or repositioned.

`fseek` supports SET/CUR/END on files, including past the current end. Subsequent
writes can create zero-filled gaps through the native file operation. Negative
resulting positions are rejected. `ftell` fails with EOVERFLOW if the current
offset cannot fit in long. Terminal seeks fail with ESPIPE. Update streams have
no buffered direction state; ordinary C code can still use fseek/fflush at the
required read/write transitions.

## Standard streams, formatting and exit

Runtime initialization makes private console-handle copies: stdin from `input`,
and separate stdout/stderr copies from `output`. They need no user heap backing.
Closing stdout cannot close stderr or a native startup handle. Missing authority
leaves a stream unavailable; its first I/O reports the saved initialization
failure. This does not prevent a program without terminal I/O from running.

All streams are unbuffered. `fflush`, including `fflush(NULL)`, has no pending
bytes or read-ahead to synchronize and does not clear an earlier error indicator.
Normal exit calls it and closes all registered streams. `_Exit` and fatal faults
bypass libc cleanup; the kernel still reclaims process resources. There are no
atexit callbacks, buffering controls, pushback, scanning, wide I/O or fd adapters
in this slice.

`printf`/`fprintf` and their va_list forms share the existing snprintf formatter
and its supported conversions. Floating conversions `f/F/e/E/g/G/a/A` support
double (including `l`) and long double (`L`), width, precision, signs, alternate
form, zero padding, infinities, NaNs and signed zero. The radix is always `.`;
there is no locale state. Conversion uses the pinned musl algorithm with an
approximately 8 KiB stack workspace, honors the active FP rounding mode, and
does not allocate. Large padding on a bounded destination is counted without
iterating through discarded bytes. Kernel formatting remains integer-only.

Output is staged completely before writing: a
small stack buffer handles short results, with heap storage for larger ones.
Formatting/allocation failures produce no output; output errors can leave a
partial transfer. Failures set the stream error indicator and errno.

`strerror` returns static, nonmodifiable messages, including an unknown-error
fallback. `perror` writes an optional prefix, the saved errno message and newline
to stderr. NULL/empty prefixes omit the prefix and colon. It preserves errno even
if reporting itself fails.

## Native error translation

| Native result | errno |
| --- | --- |
| BAD_HANDLE | EBADF |
| DENIED | EACCES |
| BAD_OPERATION | ENOTSUP |
| BAD_REQUEST, WRONG_TYPE | EINVAL |
| BAD_BUFFER | EFAULT |
| UNAVAILABLE | ENODEV |
| QUEUE_FULL | EAGAIN |
| ENDPOINT_CLOSED | EPIPE |
| BUSY | EBUSY |
| NO_MEMORY | ENOMEM |
| LIMIT | EOVERFLOW |
| NOT_FOUND | ENOENT |
| ALREADY_EXISTS | EEXIST |
| READ_ONLY | EROFS |
| NOT_EMPTY | ENOTEMPTY |
| INPUT_LOST, unrecognized failure | EIO |

The native WRONG_TYPE result does not distinguish a file from an intermediate
directory mismatch, so libc does not invent that distinction. Permission checks
may reject a mutation before the backing reports READ_ONLY. Code should inspect
native statuses directly when it needs the original protocol detail.
