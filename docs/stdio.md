# File, terminal and pipe I/O

The freestanding C library exposes an unbuffered `FILE` subset in
[`stdio.h`](https://git.internal/chronium/pyxis-userland/src/branch/main/libc/include/stdio.h). It uses native file, directory,
console and pipe capabilities; there is no kernel descriptor table or POSIX
syscall layer.
Libpyxis still returns native statuses. Libc translates failures into `errno`.

## Opening and ownership

`fopen` accepts `r`, `w`, `a` and their `+` forms. Optional `b` is accepted before
or after `+` and has no effect. Other extensions and duplicated mode letters are
rejected. `r` requires an existing readable file; `w` creates or truncates a
writable file; `a` creates if absent and chooses the current end before each
write. `+` requests both read and write access through the caller's existing
grants; it creates no authority. `a+` starts reading at offset zero;
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
The FILE wrapper and descriptor storage are reserved before opening or truncating
the file. Path workspace allocation and authority resolution also precede
truncation. Publishing a successfully truncated stream requires no allocation;
failure releases the wrapper and slot reservation, and attempts to release any
opened file handle under the close policy below.

A private process-local descriptor entry owns the native handle, access mode,
append policy and file cursor. FILE has a non-owning, invalidatable association
with that entry and owns its EOF/error indicators. The association adds no native
reference. Closing the entry invalidates the FILE before releasing the handle or
reusing its number; the old FILE cannot access or close a later occupant. Closing
a FILE releases its live descriptor and wrapper metadata, not the directory entry.

Descriptors use the lowest free number, including absent or closed standard
slots. The first three entries use static storage; later growth uses the heap.
Storage exhaustion reports ENOMEM, and descriptor-number exhaustion reports
EMFILE. Public open/read/write/close use this same ownership/cursor model;
fdopen, fileno and duplication are not exposed.

Append currently performs separate SIZE and WRITE calls. Concurrent appenders
can choose the same end and overwrite one another. This is explicitly not an
atomic append guarantee; see [technical debt](technical-debt.md#non-atomic-stdio-append).

## Descriptor I/O

`fcntl.h` declares `open(path, flags, ...)` and defines `O_RDONLY` as zero.
Only that flag value is supported; every other value returns -1/EINVAL before
allocation or path lookup. Open returns the lowest free descriptor for an
existing readable file through the same capability path resolver as fopen.
It creates no FILE wrapper and grants no additional authority. Public writable
opens, creation-mode policy and seeking remain deferred; writable fopen and
its existing seeks remain available.

`unistd.h` declares `read`, `write` and `close`, and defines STDIN_FILENO,
STDOUT_FILENO and STDERR_FILENO as 0, 1 and 2. `sys/types.h` defines ssize_t as
signed long on the LP64 target; `limits.h` defines SSIZE_MAX as LONG_MAX.

Read/write check descriptor and access validity first (-1/EBADF), then reject
counts above SSIZE_MAX (-1/EINVAL). A valid zero-count request returns zero
without touching the buffer or backend. Nonempty calls perform one backend
transfer and return the confirmed byte count, including short progress, or -1
with the translated errno. They do not fill a buffer or retry the remainder.
File and pipe zero reads report EOF; unexpected console zero progress and
nonempty zero writes report EIO. Native denial remains EACCES, unsupported
operations remain ENOTSUP and pipe writes with no remaining reader report EPIPE.

Descriptor I/O shares the cursor with an associated FILE but never reads or
changes its EOF/error indicators. A prior FILE EOF does not suppress read;
successful descriptor I/O does not clear that indicator. Closing descriptor 1
invalidates stdout before native release. Later reuse of 1 does not reconnect
stdout, and fclose of that stale wrapper cannot close the replacement.

Close invalidates its descriptor even on release failure and never retries.
Success returns zero without changing errno; invalid descriptors return
-1/EBADF. Other failures follow the [close contract](wip/libc-portability.md#close-failure-and-cleanup),
including uncertain native release surviving until process exit. Normal exit
closes remaining descriptors, including opens without FILE wrappers.

## Removal

`remove(path)` removes a file or empty directory through the native parent
capability, returning zero on success or -1 with errno on failure. It uses the
same startup roots and initial working-directory chain as fopen. Selection and
removal happen in one kernel operation, without first probing the child type.
Existing FILE streams keep their original object after removal; reopening the
name fails unless another entry has been created there. Roots and final `.` or
`..` are rejected, and a trailing slash requires a directory.

Nonempty directories report ENOTEMPTY. This is nonrecursive removal, without
unlink/rmdir syscall adapters. Persistent storage remains separate work.

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
is not included in that count. File position belongs to the associated descriptor,
with no shared seek position in the underlying capability. File writes use that
offset.
File, terminal and pipe output continue positive short writes until complete or
an error, submitting only the remaining suffix. Zero progress or an excessive
count is rejected; file position advances only for confirmed bytes. On a later
failure, the position includes any partial final element, the return counts
only whole elements, and `ferror`/`errno` retain the failure. Neither `fwrite`
nor formatting helpers automatically retry an error. Append re-queries the end
before each native write and remains non-atomic across all these calls.

OUTCOME_UNKNOWN maps to EIO: the current native mutation may have taken effect
without a trustworthy reply. The stream can account for earlier confirmed
writes only. Applications needing that distinction must use the native status;
retrying the same bytes after EIO is not guaranteed safe. Zero-size/count
`fwrite` remains a C no-op; it does not submit a native zero-byte write.

`fread_some(buffer, capacity, stream)` is a Pyxis byte-oriented extension. It
returns after one successful backend transfer, up to capacity and the backend's
per-call limit; it does not keep reading to fill a short result. Empty terminal
or pipe input waits for initial data, EOF where supported, or error. File reads
retain bulk transfers and advance their descriptor offset by the confirmed byte
count.
Zero capacity is a no-op. A nonempty zero result reports backend EOF through
`feof`, or failure through `ferror` and errno; unavailable input is EBADF, and
an unexpected zero-byte terminal result is EIO. A positive short result does not
establish EOF. Existing EOF suppresses backend reads until cleared. The helper
shares backend dispatch and indicator handling with `fread`, whose complete
element counts and fill-request behavior are unchanged. There is no extra handle or read-ahead.

Private descriptor transfers return one backend result: progress or an error.
FILE alone updates EOF/error indicators and implements the fread/fwrite loops.
A FILE's sticky EOF does not suppress a descriptor transfer. Selected access
mode and native rights remain separate: invalid entries and wrong modes report
EBADF, while native authority denial remains EACCES and unsupported operations
remain ENOTSUP. A successful short transfer remains successful progress.

`fgetc`/`getc`/`getchar`, `fgets`, `fputc`/`putc`/`putchar`, `fputs` and `puts` are
provided. `fgets` retains a newline and terminates successful input. Capacity one
produces an empty string without consuming input; a nonpositive capacity fails.
After a read error the destination may contain a partial, unterminated prefix.
Terminal input is raw and blocking: fread waits for the requested bytes or an
error; fgets stops at newline/capacity. Neither echoes or edits. Interactive line
editing remains an explicit [libterm](terminal.md) operation. Do not read from
stdin while a foreground child or another reader owns that input stream.

A successful zero-byte file or pipe read for a nonempty request sets EOF. Merely
reading exactly to the end does not set it until a later read attempts more. Terminal input has no
EOF convention; input loss sets EIO and the error indicator. `feof` and `ferror`
remain set until cleared: `clearerr` clears both, successful `fseek` clears EOF,
and `rewind` clears both. Indicators do not reset errno. An error does not itself
prevent retrying I/O; EOF suppresses reads until cleared or repositioned.

`fseek` supports SET/CUR/END on files, including past the current end. Subsequent
writes can create zero-filled gaps through the native file operation. Negative
resulting positions are rejected. `ftell` fails with EOVERFLOW if the current
offset cannot fit in long. Terminal and pipe seeks fail with ESPIPE. Update
streams have no buffered direction state; ordinary C code can still use fseek/fflush at the
required read/write transitions.

## Standard streams, formatting and exit

Startup supplies independent stdin, stdout and stderr bindings. Each declares
`PROTOCOL_CONSOLE`, `PROTOCOL_FILE` or `PROTOCOL_PIPE` and owns a distinct child
handle with only READ authority for stdin or WRITE authority for stdout/stderr.
Runtime adopts these handles into descriptors 0, 1 and 2 before heap
initialization, using static storage. It retains no hidden startup copy.
Closing stdout cannot close stderr or a named terminal grant. Normal boot binds
all three to the space console.

`STARTUP_STREAM_NONE` with `HANDLE_INVALID` makes that stream unavailable without
preventing the process from running. Its first nonempty I/O fails with EBADF and
sets the error indicator; missing stdin is not EOF. There is no fallback to a
terminal, another standard stream or the kernel log. Zero-size transfers remain
no-ops. Later allocation of the absent stream's descriptor number does not make
its FILE available. Unknown protocols and malformed bindings are rejected during
launch/startup.

The [shell](shell.md#file-redirection-and-stdin) can supply these bindings through
foreground file redirects and [pipelines](shell.md#foreground-pipelines).
The [stream reference](shell-streams.md) describes delegation and lifetime;
[head](shell.md#bounded-input-with-head) provides exact bounded consumption.
File-backed standard streams start at offset zero, with independent per-descriptor
positions. Adoption does not open, truncate or append
to the file. Two output streams backed by the same object can overwrite one
another because their positions are independent. Console and pipe streams remain
sequential and cannot seek. [Pipes](pipes.md) use reference-based EOF and report
EPIPE when their last reader closes.

`startup_stream(index)` borrows the handle owned by the corresponding descriptor;
the immutable snapshot retains no reference, and native code must not close its
handle independently. After the owner closes, the snapshot is stale and must
neither be used nor forwarded to a child. Descriptor-number reuse does not
refresh it. Before owner close, native code may explicitly copy a borrowed handle
when it needs a separately owned reference, but must close that copy itself.
Named `input`/`output` console grants, plus `keyboard`, remain separate terminal
resources; libc never uses them to fill a missing standard-stream binding.

All streams are unbuffered. `fflush`, including `fflush(NULL)`, has no pending
bytes or read-ahead to synchronize and does not clear an earlier error indicator.
Normal exit calls it, closes each live descriptor once, then disposes of FILE
metadata, including invalid associations. Cleanup errors do not replace the
requested exit status. `_Exit` and fatal faults bypass libc cleanup; the kernel
still reclaims process resources. There are no
atexit callbacks, buffering controls, pushback, scanning, wide I/O or fdopen/fileno
in this slice.

`fclose` invalidates the association and makes one native close attempt. Success
returns zero without changing errno. Failure returns EOF with the translated
errno and still disposes of the wrapper; static standard wrappers remain invalid.
Closing a stale FILE returns EOF/EBADF without touching a reused descriptor.
Native CLOSE currently returns only success or BAD_HANDLE; neither leaves an
owned native entry. Other native failures retain their errno translation, while
an unknown status or malformed reply reports EIO. In those unexpected cases a
surviving native reference may remain until process exit and delay pipe peer
closure. There is no hidden copy or retry list. Open rollback uses the same
single-attempt release policy while preserving the open failure's errno. See
[the close-failure contract](wip/libc-portability.md#close-failure-and-cleanup)
and [its cleanup limit](technical-debt.md#unexpected-native-close-failures).

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
| NO_SPACE | ENOSPC |
| QUOTA | EDQUOT |
| FILE_TOO_LARGE | EFBIG |
| LIMIT | EOVERFLOW |
| NOT_FOUND | ENOENT |
| ALREADY_EXISTS | EEXIST |
| READ_ONLY | EROFS |
| NOT_EMPTY | ENOTEMPTY |
| IO, OUTCOME_UNKNOWN, INPUT_LOST, unrecognized failure | EIO |

The native WRONG_TYPE result does not distinguish a file from an intermediate
directory mismatch, so libc does not invent that distinction. Permission checks
may reject a mutation before the backing reports READ_ONLY. Code should inspect
native statuses directly when it needs the original protocol detail.
