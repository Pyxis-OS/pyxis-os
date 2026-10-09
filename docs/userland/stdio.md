# File, terminal and pipe I/O

The freestanding C library exposes a `FILE` subset, with unbuffered output and
file/pipe input read-ahead, in
[`stdio.h`](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/libc/include/stdio.h). It uses native file, directory,
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
such as `tmp://notes.txt` and the initial working-directory chain for relative
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
EMFILE. Public open/read/write/close use this same ownership/cursor model.
`fileno` returns a stream's current descriptor, or -1 with EBADF once the
stream or its descriptor is closed; the stream still owns it, so close it with
`fclose`. Reading or seeking through that descriptor bypasses the stream's
pushback and read-ahead; `fstat` is unaffected. fdopen and duplication are not
exposed.

Append currently performs separate SIZE and WRITE calls. Concurrent appenders
can choose the same end and overwrite one another. This is explicitly not an
atomic append guarantee; see [technical debt](../technical-debt.md#non-atomic-stdio-append).

## Descriptor I/O

`fcntl.h` declares `open(path, flags, ...)` and defines `O_RDONLY` as zero.
O_WRONLY selects write-only access and O_RDWR selects both reading and writing.
O_CREAT and O_TRUNC may be combined with either writable mode to create an
absent file and/or truncate an existing file. O_CREAT|O_EXCL uses native exclusive
creation and returns -1/EEXIST for an authorized attempt at an existing name,
without opening or truncating it, including with O_TRUNC. It has no concurrent
creator fallback. Combined access modes, read-only mutation, unknown flags and
O_EXCL without O_CREAT return -1/EINVAL before lookup or examining varargs.
Open returns the lowest free descriptor through the same resolver as fopen.
It creates no FILE wrapper and grants no additional authority. Descriptor
storage and path workspace are reserved before exclusive creation or truncation,
with no fallible descriptor publication afterward. Read/write requests require
both native rights. lseek uses the same private descriptor position as stdio;
public append, pread/pwrite and descriptor duplication remain absent.

With O_CREAT, the third argument has type mode_t (unsigned int in sys/types.h).
Only 0666 is accepted, meaning native creation policy rather than Unix permission
bits. Other modes return -1/ENOTSUP before lookup, even when the file exists;
invalid flags are rejected before examining that argument. No mode argument is
read without O_CREAT. Existing native capability rights, backend creation policy
and host restrictions remain authoritative. Virtio-fs still requests 0644.
See the [temporary creation-mode policy](../technical-debt.md#public-open-creation-mode).

`unistd.h` declares `read`, `write`, `close`, `lseek`, `ftruncate`, `fsync` and
`unlink`, and defines STDIN_FILENO,
STDOUT_FILENO and STDERR_FILENO as 0, 1 and 2. `sys/types.h` defines ssize_t as
signed long on the LP64 target; `limits.h` defines SSIZE_MAX as LONG_MAX.

Read/write check descriptor and access validity first (-1/EBADF), then reject
counts above SSIZE_MAX (-1/EINVAL). A valid zero-count request returns zero
without touching the buffer or backend. Nonempty calls perform one backend
transfer and return the confirmed byte count, including short progress, or -1
with the translated errno. They do not fill a buffer or retry the remainder. A
read first returns bytes the associated FILE read ahead, without a backend call;
read itself never reads ahead.
Successful zero reads report EOF, including independent terminal input;
nonempty zero writes report EIO. Native denial remains EACCES, unsupported
operations remain ENOTSUP and pipe writes with no remaining reader report EPIPE.

Descriptor I/O shares the cursor with an associated FILE but never reads or
changes its EOF/error indicators. A prior FILE EOF does not suppress read;
successful descriptor I/O does not clear that indicator. Closing descriptor 1
invalidates stdout before native release. Later reuse of 1 does not reconnect
stdout, and fclose of that stale wrapper cannot close the replacement.

Close invalidates its descriptor even on release failure and never retries.
Success returns zero without changing errno; invalid descriptors return
-1/EBADF. Other failures follow the [close contract](libc-portability.md#close-failure-and-cleanup),
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

## Directory creation

`mkdir(path, mode)` from `<sys/stat.h>` creates one directory through the
parent's CREATE right, using the same startup roots and initial directory chain
as fopen. `mode` has no effect: native directories carry no permission bits. A
trailing slash is accepted. An existing name of either kind fails with EEXIST,
as does a root or a final `.` or `..` that names an existing directory. A
missing parent fails with ENOENT; parents are not created. The new directory's handle is closed before return.

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
offset. Native file calls transfer at most 4,088 bytes per read or 4,080 bytes per
write; `fread`/`fwrite` continue across those boundaries using their existing loops.
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
element counts and fill-request behavior are unchanged. It returns bytes already
read ahead first and never reads ahead itself; there is no extra handle.

Private descriptor transfers return one backend result: progress or an error.
FILE alone updates EOF/error indicators and implements the fread/fwrite loops.
A FILE's sticky EOF does not suppress a descriptor transfer. Selected access
mode and native rights remain separate: invalid entries and wrong modes report
EBADF, while native authority denial remains EACCES and unsupported operations
remain ENOTSUP. A successful short transfer remains successful progress.

### Input read-ahead

Each descriptor entry owns an optional BUFSIZ read-ahead buffer beside its handle
and logical position. `fread`, and the `fgetc`/`getc`/`getchar`, `fgets` and
`getline` input built on it, may fill that buffer with one backend transfer from
a file or pipe and return the excess on later reads. Requests of at least BUFSIZ
bytes read directly into the caller's memory. A file fill stays below LONG_MAX,
the stdio position range, which also keeps it inside `host://`'s signed offset
range. At or past that boundary, or when the request itself does not fit below
it, the read is exact, so speculation never turns a valid read into an error
such as EOVERFLOW. A fill happens only while the buffer is empty: EOF or a
backend error reaches the request that caused it, after every buffered byte.
Consoles are never read ahead: their input is shared with the parent shell, and
ISO C treats them as interactive. `read` and `fread_some` return buffered bytes
first, otherwise one exact transfer, so mixing them with stdio on one descriptor
loses no bytes and exact consumers such as head stay exact.

The buffer is allocated on the first buffered read and freed on close. If
allocation fails, the descriptor stays unbuffered without an error. Only file
read-ahead is discarded before close, because a file can fetch it again: writes
on update streams, successful seeks and input `fflush` drop it. Pipe read-ahead
survives until close. Buffered file bytes can be stale if another descriptor or
process writes the file; seeking or input `fflush` refetches them.

Read-ahead is private process memory and never accompanies a delegated stream.
A child given the same pipe sees only bytes not yet fetched; a child given a
file-backed stream starts at offset 0, as before. Read a stream you will
delegate only with `read` or `fread_some`.

#### Read-ahead validation

Measured with QEMU 10.2.2 on q35 with KVM, four CPUs, 256 MiB, virtio-fs
(virtiofsd 1.14.0) and virtio-net, inside the development VM, through the
remote terminal's machine client. The final image used userland `f24e9d9`;
the debugger check used the preceding pin `c99301f`, which differs only in the
file fill cap. Probe programs were uncommitted.

- Uniq: `uniq -c` on the 36,009-byte `host://` input from the
  [uniq validation](uniq.md#validation-evidence), which took 20.4 s without
  read-ahead, took 0.04 s in each of five runs, measured from host submission
  to the completion event. Output was byte-identical to host `LC_ALL=C uniq -c`,
  as was the output through `cat | uniq -c` and `uniq -c <`.
- A debugger stop around `fseek(stdin, 0, SEEK_SET)` on a pipe, with 3,999 of
  4,000 fetched bytes buffered, showed a -1/ESPIPE result. The entry's
  position, buffer offsets and all 4,000 buffer bytes, and both FILE
  indicators, were unchanged. Later reads returned every byte.
- Offset limits: from LONG_MAX - 8193 through UINT64_MAX, `fgetc` on an empty
  file reported the same result as a one-byte `fread_some`, on `host://` and
  `home://`. That is EOF up to the backend's limit (below LONG_MAX on
  `host://`, below UINT64_MAX on `tmp://`) and EOVERFLOW past it. Before the
  LONG_MAX cap, `fgetc` at LONG_MAX - 1 on `host://` failed with EOVERFLOW.
- Head printed exact lines from pipes, and its producer reported EPIPE. A
  guest-TCC program reading one line from console stdin, and `head -n 1`, left
  the typed-ahead next shell command intact.
- Sha256sum `-c` with manifests from a file, a redirect and a pipe, and stdin
  hashing, matched the host. Update streams followed the contract above:
  `w+` write/seek/read, `r+` write after read-ahead without `fseek`, refetch
  after `fseek` and input `fflush`, and `a+` append. TCC compile/run, a
  Bucharest `date`, and Kilo open/edit/save on `host://` also worked.

`fgetc`/`getc`/`getchar`, `fgets`, `fputc`/`putc`/`putchar`, `fputs` and `puts` are
provided. `fgets` retains a newline and terminates successful input. Capacity one
produces an empty string without consuming input; a nonpositive capacity fails.
After a read error the destination may contain a partial, unterminated prefix.
Fgets decides EOF from the current call, as getline does: an error indicator left
by an earlier call does not turn a final unterminated line into NULL, and the
indicator stays set until cleared.
Terminal input is raw and blocking: fread waits for the requested bytes or an
error; fgets stops at newline/capacity. Neither echoes or edits. Interactive line
editing remains an explicit [libterm](terminal.md) operation. Do not read from
stdin while a foreground child or another reader owns that input stream.

`getline` reads through the next newline into a caller-owned buffer, growing it
with realloc from 128 bytes by doubling. Its count includes the newline and
excludes the added NUL; embedded NULs are data. A final unterminated line is
returned before EOF, and EOF before any byte returns -1. Null arguments
(EINVAL), read errors, allocation failure (ENOMEM) and lines beyond SSIZE_MAX
(EOVERFLOW) return -1 and set errno and the error indicator. After failure the
caller's pointer and capacity describe its current allocation, which it still
owns. It reads through `fgetc`, so file and pipe input is fetched in blocks,
while console input remains one native read per byte; see
[console line input](../technical-debt.md#console-line-input).
There is no `getdelim`.

A successful zero-byte read for a nonempty request sets EOF. Merely reading
exactly to the end does not set it until a later read attempts more. Independent
terminal sessions drain input before EOF; framebuffer consoles have no EOF
operation. Input loss sets EIO and the error indicator. `feof` and `ferror`
remain set until cleared: `clearerr` clears both, successful `fseek` clears EOF,
and `rewind` clears both. Indicators do not reset errno. An error does not itself
prevent retrying I/O; EOF suppresses reads until cleared or repositioned.

`fseek` supports SET/CUR/END on files, including past the current end. Subsequent
writes can create zero-filled gaps through the native file operation. Negative
resulting positions are rejected. `ftell` fails with EOVERFLOW if the current
offset cannot fit in long. Terminal and pipe seeks fail with ESPIPE. `ftell`
reports the logical position, excluding read-ahead. A seek is validated before
it drops read-ahead; a failed seek, including ESPIPE, keeps buffered bytes, the
position and both indicators. A write on an update stream drops read-ahead
first, so no `fseek` is needed between reading and writing. ISO C requires one;
omitting it is a Pyxis guarantee, not portable behavior.

## Pushback and scanning

`ungetc` keeps one byte per FILE, returned before any further input, including
read-ahead. A second `ungetc` before a read fails, and `EOF` is never pushed.
It clears the EOF indicator, and `ftell` reports one less (not below zero).
A successful `fseek` (a `SEEK_CUR` offset counts from the position before the
pushback), input `fflush` and any write discard it. Pushback belongs to the
FILE: descriptor reads never see it.

`fscanf`, `scanf`, `sscanf` and their `v` forms read through `fgetc` with that
one byte of lookahead. They support whitespace and ordinary-character
directives, `%%`, and the narrow conversions `d i u o x X p`, `a e f g` (and
capitals), `s`, `c`, `[` and `n`, with `*` suppression, a nonzero width and the
lengths `hh h l ll j z t L`. Scansets accept a leading `^`, a leading `]` and
`a-z` ranges. Integers convert through `strtoll`/`strtoull` (overflow saturates)
and floats through `strtof`/`strtod`/`strtold`. A field that is only a prefix
of a number, such as `0x` or `1e+`, is consumed and fails the directive. Numeric
fields stop after 511 characters, as if limited by a width.

The result counts assignments; it is EOF if input ends or a read fails before
the first conversion completes. A malformed or unsupported conversion, including
wide `%ls`, `%lc` and `%l[`, stops with EINVAL and the same result rule. Input is
locale-free ASCII.

## Standard streams, formatting and exit

Startup supplies independent stdin, stdout and stderr bindings. Each declares
`PROTOCOL_CONSOLE`, `PROTOCOL_FILE` or `PROTOCOL_PIPE` and owns a distinct child
handle with only READ authority for stdin or WRITE authority for stdout/stderr.
An exported FILE additionally retains CALL transport; native streams retain zero.
The shared [provider bridge](../interfaces/file-providers.md) supports ordinary opens and stdin
redirection without changing stdio operations or per-descriptor positions.
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
sequential and cannot seek. [Pipes](../interfaces/pipes.md) use reference-based EOF and report
EPIPE when their last reader closes.

`startup_stream(index)` borrows the handle owned by the corresponding descriptor;
the immutable snapshot retains no reference, and native code must not close its
handle independently. After the owner closes, the snapshot is stale and must
neither be used nor forwarded to a child. Descriptor-number reuse does not
refresh it. Before owner close, native code may explicitly copy a borrowed handle
when it needs a separately owned reference, but must close that copy itself.
For native launch adapters, `<pyxis/stdio.h>` provides
`pyxis_stdio_stream(FILE *, struct startup_stream *)`. It checks the current FILE
registry and descriptor association, returning a borrowed protocol/handle.
Closed or absent standard wrappers return NONE; descriptor-number reuse never
reconnects them. Success preserves errno, indicators, cursor, pushback and
read-ahead, and acquires no reference. The caller must not close the handle or
use it after owner close. This lets [Lua](lua.md) delegate live C streams without
reusing the stale startup snapshot. Child files begin at offset zero; buffered
pipe bytes and append state are not transferred.

Named `input`/`output` console grants, plus `keyboard`, remain separate terminal
resources; libc never uses them to fill a missing standard-stream binding.

Output is unbuffered, so `fflush` has no pending bytes to write. On an input
file stream it drops read-ahead so later reads refetch; on a pipe it keeps them.
ISO C leaves input `fflush` undefined, so this is a Pyxis guarantee.
`fflush(NULL)` does not touch input, and no flush clears an earlier error
indicator.
Normal exit calls it, closes each live descriptor once, then disposes of FILE
metadata, including invalid associations. Cleanup errors do not replace the
requested exit status. Exit handlers and `.fini_array` run before this cleanup,
so they can still write to the streams; see
[startup and exit](../development/sdk.md#startup-exit-and-layout). `_Exit` and
fatal faults bypass libc cleanup; the kernel still reclaims process resources.
There are no buffering controls, wide I/O or fdopen. `fseeko` and `ftello`
behave exactly like `fseek` and `ftell`, because `off_t` is `long`. Scanning
and one-byte pushback are described above.

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
[the close-failure contract](libc-portability.md#close-failure-and-cleanup)
and [its cleanup limit](../technical-debt.md#unexpected-native-close-failures).

`printf`/`fprintf` and their va_list forms share the existing snprintf formatter
and its supported conversions. Floating conversions `f/F/e/E/g/G/a/A` support
double (including `l`) and long double (`L`), width, precision, signs, alternate
form, zero padding, infinities, NaNs and signed zero. The radix is always `.`;
there is no locale state. Conversion uses the pinned musl algorithm with an
approximately 8 KiB stack workspace, honors the active FP rounding mode, and
does not allocate. Large padding on a bounded destination is counted without
iterating through discarded bytes. Kernel formatting remains integer-only.

`sprintf` and `vsprintf` use the same formatter without a destination bound;
the caller's buffer must hold the whole result and its NUL. Prefer `snprintf`.

`asprintf` and `vasprintf` use the same formatter and INT_MAX result-count limit.
Success returns the character count excluding NUL and transfers malloc-owned,
NUL-terminated storage to the caller, including for an empty result; release it
with `free`. Failure returns -1, sets errno and leaves the output pointer NULL.
Unsupported conversions such as `%n` return EINVAL, oversized results EOVERFLOW,
and allocation failure ENOMEM. `vasprintf` copies and preserves the supplied
va_list. If the second formatting pass fails, the unpublished buffer is freed
and its error preserved; a length mismatch returns EINVAL.

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

## Temporary files

`tmpfile()` returns a real read/write FILE in the space's `tmp://` root. It
preflights creation, removal and read/write authority, allocates its wrapper and
reserves its descriptor before creating an exclusive random name. The retained
parent removes that name immediately, without further allocation or path
resolution; the file handle survives removal. Clock/random grants are required.
Failure returns NULL with errno and unwinds local ownership. Failed or uncertain
creation/removal, or abrupt death between those calls, can leave a named file;
no automatic stale-file cleanup runs.

`mkstemp(char *)` in `<stdlib.h>` is the exclusive named-file extension. Its
mutable template must end in six `X` characters. It substitutes a native-random
suffix, retries collisions a bounded number of times and returns a read/write
descriptor; it never opens an existing name. Paths follow the same roots/cwd
policy as `fopen`. The caller closes the descriptor and removes the reserved
name. There is no ISO C `tmpnam`: Lua's `os.tmpname` deliberately reserves a file
instead of returning an unreserved name.
