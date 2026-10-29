# File, terminal and pipe I/O

The freestanding C library exposes a `FILE` subset, with unbuffered output by
default, explicit output buffering and file/pipe input read-ahead, in
[`stdio.h`](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/libc/include/stdio.h). It uses native file, directory,
console and pipe capabilities; there is no kernel descriptor table or POSIX
syscall layer.
Libpyxis still returns native statuses. Libc translates failures into `errno`.

## Opening and ownership

For local files, `fopen` accepts `r`, `w`, `a` and their `+` forms. Optional `b`
is accepted before or after `+` and has no effect. A final C11 `x` with `w`
requests exclusive creation: existing names fail without truncation. Other
extensions and duplicated mode letters are rejected. `r` requires an existing readable file; `w` creates or truncates a
writable file; `a` creates if absent and chooses the current end before each
write. `+` requests both read and write access through the caller's existing
grants; it creates no authority. `a+` starts reading at offset zero;
`a` starts at the existing end. Seeking never disables append-on-write.

Paths use the existing [capability path rules](paths.md), including scheme roots
such as `tmp://notes.txt` and libc's retained [current working chain](process-state.md)
for relative paths. `chdir` affects later path operations, not already open
streams. Lookup adds no authority; roots are borrowed and cwd handles retained.
An open stream owns its handle independently.

[`realpath`](paths.md#proved-realpath) proves a bounded native spelling while
original and candidate references are held; it does not collapse scheme aliases.
Providers, missing components and unavailable identity fail. Descriptive cwd text
alone is not proof, and a successful result freezes no later lookup or contents.

Only the final file may be created; intermediate directories must exist. Lookup
of an existing writable file does not require CREATE. A competing creator after a missing lookup reports EEXIST; it is not reopened. Temporary path storage is allocated
from the path length and initial chain depth, without a fixed path/depth limit.
The FILE wrapper, open object and descriptor storage are reserved before opening
or truncating the file. Path workspace allocation and authority resolution also
precede truncation. Publishing a successfully truncated stream requires no allocation;
failure releases the wrapper, object and slot reservation, and attempts to release
any opened file handle under the close policy below.

A process-local descriptor slot references an open object owning the native
handle, access, append policy, cursor and input read-ahead. `dup` selects the
lowest free slot and shares that object; only its final close releases the
native handle. Each slot has at most one invalidatable FILE association, with
its own selected access and EOF/error/pushback state. Closing a slot invalidates
its FILE before number reuse; the old wrapper cannot affect a later occupant.
Closing one alias leaves other aliases and unread read-ahead alive. Children
still receive explicit native grants, not the parent's shared cursor or buffers.

`fdopen` accepts the same modes except exclusive `x` (EINVAL, descriptor retained),
validates the mode and allocates the
wrapper before association. Failure leaves the descriptor unchanged and with
its caller; success transfers that slot's close responsibility to `fclose`.
Invalid descriptors or unavailable access report EBADF; an otherwise compatible
existing association reports EBUSY. Position and read-ahead stay intact; `w` does not truncate or create. FILE access is narrowed:
`r` cannot write even when the underlying descriptor is read/write. `a` enables
shared append for every alias after a zero APPEND support probe, without moving
the current cursor; other modes never clear the descriptor's append policy. `freopen` remains
unsupported; output buffering is selected separately before stream I/O.

`dup2(source, target)` shares the source object at the requested target number.
It validates the source and reserves storage before closing the target; those
failures preserve the target. An existing target FILE association and its
selected access remain, while pushback and EOF are cleared on success. The
error indicator remains. Selected access grants no new rights: a readable FILE redirected to a write-only object
later reads with EBADF. Source cursor and read-ahead remain shared. Pending
ordinary target FILE output rejects replacement with EBUSY; flush it first. A pending
transaction target stages its buffer before replacement and its final alias
commits once; a producer error abandons it and reports failure. A failed
native target close leaves the target invalidated and reports its error without
retrying release. Source equal to target validates and returns unchanged.

Descriptors use the lowest free number, including absent or closed standard
slots. The first three entries use static storage; later growth uses the heap.
Storage exhaustion reports ENOMEM, and descriptor-number exhaustion reports
EMFILE. Public open/read/write/close use this same ownership/cursor model.
`fileno` returns a stream's current descriptor, or -1 with EBADF once the
stream or its descriptor is closed; the stream still owns it, so close it with
`fclose`. Descriptor reads bypass FILE pushback but consume descriptor read-ahead;
seeking discards that read-ahead. Pread bypasses both without consuming them;
pwrite discards descriptor read-ahead before a nonempty native write and leaves
FILE pushback/indicators unchanged. `fstat` is unaffected.

Append uses native FILE_APPEND: EOF selection and mutation are atomic per
operation of at most 4,080 bytes, including aliases/hard links. Larger writes,
flushes and formatted lines may interleave between operations. HOST append is
ENOTSUP, including `fdopen("a")`, without taking descriptor ownership; PIPE,
CONSOLE, STREAM and TRANSACTION append remain unsupported. A zero support probe
reserves no future success. Append adds no durability or append-only authority.

After an uncertain native FILE WRITE, APPEND or pwrite, subsequent data writes
through that shared descriptor fail with EIO, including buffered stdio and dup
aliases. `clearerr` does not reset this latch. Reads and ownership release remain
available; close still releases the descriptor after a failed flush. A fresh open
is a new intent. Known non-uncertain errors retain their ordinary recovery.

Descriptor readiness and shared nonblocking status use
[`poll`/`select` and `fcntl`](libc-readiness.md). They preserve descriptor
read-ahead ownership and the existing native FILE-value latency boundary.

## Typed resources and transactions

`fopen` dispatches on the attested result of [typed OPEN](../interfaces/file-providers.md).
FILE keeps positioned I/O; STREAM is forward-only; native PIPE and CONSOLE use
their existing paths. DIRECTORY fails with EISDIR and unsupported types with
ENOTSUP. Name lookup captures one environment entry; an existing held FILE may
be truncated without inventing CREATE authority. Generic OPEN never falls back
to HTTP URI handling.

A [transaction](../interfaces/transactions.md) provides independently delegated
STAGE, COMMIT and ABORT rights. Writable open obtains a staging FILE, with no
submission. `fflush(f)` on that exact request drains successful output and
commits once. `w+` then reads the same FILE's reply; `w` consumes/discards the
reply and reports a failed result. Later writes fail with EBADF. Reads before
commit fail with EBUSY and set the error indicator; `clearerr` permits recovery
from a local argument/read refusal, while native staging failures remain latched.
A lost delivered commit is uncertain and is never replayed.

Implicit full-buffer drains, `fflush(NULL)` and launch flush helpers only stage.
The final owning descriptor-alias close stages and commits a clean transaction;
earlier aliases only stage. Normal exit stages all FILE buffers before finalizing
transactions. Finalization failure changes exit zero to EXIT_FAILURE, preserves
an already nonzero status, and reports through available stderr. FILE errors
block submission until explicitly cleared. `_Exit`, crash and abort never
submit. An abandoned transaction fails; closing is not a success-shaped abort.

`<pyxis/resource.h>` adds explicit failure inspection. Ordinary `fopen` returns
NULL/errno and releases provisional handles. `pyxis_fopen(path, mode, &result)`
uses a fresh zeroed `pyxis_open_result` to retain a failure description/body;
`pyxis_open_result_close` releases it. `pyxis_open_error_body(&result)` and
`pyxis_error_body(FILE*)` return independent read FILEs over immutable copied
bytes, without another OPEN. Close those FILEs normally. `pyxis_describe(FILE*,
&description)` queries metadata without moving the cursor, changing indicators
or capturing a body handle. Unknown description fields stay unknown; a failed
commit retains its result detail and copied body through the stream's lifetime.

First-party `cat` and shell resource redirection/transaction errors print the
retained bounded detail before releasing it. This adds no libc automatic output
or last-error string: standard `fopen`/`errno`/`strerror`/`perror` behavior is
unchanged. Other callers select the explicit inspection interfaces above.

| Resource outcome | errno |
| --- | --- |
| not-found | ENOENT |
| denied | EACCES |
| I/O failure, incomplete/deadline failure | EIO |
| too-large | EFBIG |
| aborted | ECANCELED |
| uncertain | EIO; inspect description to distinguish it |

STREAM READ uses at most 4,088 bytes per native call. Only a final cached
DESCRIBE with COMPLETE and an OK outcome establishes EOF. A failed transfer sets
`ferror`/errno and retains its terminal failure; `clearerr` cannot turn it into
EOF or restart it. Seek, positioned I/O and byte/wide pushback fail with ESPIPE;
SIZE is unavailable. A received prefix is not a complete result.

## HTTP resources

HTTP uses the same typed OPEN and [generic transaction rules](../interfaces/transactions.md)
as other providers; libc does not dispatch on URI spelling or link TLS. READ
returns a completed re-readable FILE. WRITE/READ|WRITE stages a transaction;
explicit flush, final owning close and clean exit commit under the rules above.
Final non-2xx, framing, transport and deadline failures are native resource
failures, with bounded immutable error bytes available through `pyxis_fopen` and
`pyxis_error_body`. Generic libc does not interpret HTTP codes.

Configure headers, method and duration with a captured
[`http prepare`](http-fetch.md#reusable-endpoints) capability. Redirects, trust,
storage budgets and TLS remain provider-owned. There is no HTTP-only libc option
adapter, status companion or response-metadata API. Navigation consumers use the
explicit [framed representation](../interfaces/http-transactions.md#navigation-representation);
generic describe remains URI-free.

Lua uses ordinary `io.open`, `f:write`, `f:flush`, `f:read` and explicit `f:close`.
GC and error-unwind cleanup abandon pending transactions without submission;
explicit close and clean shutdown keep normal commit behavior. Cleanup errors
are reported where possible; an otherwise successful standalone exit fails.

`pyxis_fclose_abandon(FILE*)` in `<pyxis/resource.h>` discards pending output,
requests ABORT under the held right and releases the FILE without COMMIT.
Successful abandonment/release returns zero; a real failure returns EOF/errno.
Other aliases retain the failure. Earlier uncertain outcomes are not relabeled
as known cancellation. Nonpending FILEs close normally. This adds no authority
or retry.

## Concurrent operations

The [libc thread profile](threads.md) serializes each whole FILE operation and
the cursor/read-ahead shared by descriptor aliases. Blocking pipe/console input
and output use separate direction guards. An admitted operation holds stable
libc storage across blocking calls, without copying native grants. Ordinary native CLOSE invalidates authority promptly;
transaction finalization holds FILE/cursor exclusion through staging and commit. Raw close invalidates associations before number reuse.
Callers exclude new use during FILE destruction; already admitted operations
retain storage until return. `dup2` refuses an active associated FILE with EBUSY before replacement; pending
transaction buffers are staged under the retained FILE exclusion. Global fflush retains a registry snapshot and
performs I/O after releasing registry exclusion.

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
both native rights. lseek uses the same process-local open-object position as
stdio and every duplicate. O_APPEND selects the current EOF through native
APPEND, as fopen's "a" does; pwrite remains explicitly positioned.

With O_CREAT, the third argument has type mode_t (unsigned int in sys/types.h).
Only 0666 is accepted, meaning native creation policy rather than Unix permission
bits. Other modes return -1/ENOTSUP before lookup, even when the file exists;
invalid flags are rejected before examining that argument. No mode argument is
read without O_CREAT. Existing native capability rights, backend creation policy
and host restrictions remain authoritative. Virtio-fs still requests 0644.
See the [temporary creation-mode policy](https://git.internal/PyxisOS/pyxis-os/issues/1369).

`unistd.h` declares `read`, `write`, `close`, `lseek`, `ftruncate`, `fsync`,
`unlink`, `pread`, `pwrite`, `dup`, `dup2`, `access`, `rmdir` and `isatty`, and defines
STDIN_FILENO, STDOUT_FILENO and STDERR_FILENO as 0, 1 and 2. `isatty` returns 1
for a console stream, the startup binding to the space's terminal, and 0 with
ENOTTY for files and pipes; `fstat` reports the same console as a character
device. `sys/types.h` defines ssize_t as
signed long on the LP64 target; `limits.h` defines SSIZE_MAX as LONG_MAX.

Read/write check descriptor and access validity first (-1/EBADF), then reject
counts above SSIZE_MAX (-1/EINVAL). A valid zero-count request returns zero
without touching the buffer or backend. A nonempty write performs one backend
transfer and returns the confirmed byte count, including short progress, or -1
with the translated errno; it does not retry the remainder. A nonempty read of a
regular file repeats native transfers (each under 4 KiB) until the count is
filled or the file ends, so callers get the whole count as on other systems. If
an error follows some progress, read returns the bytes already read and the error
recurs on the next call. A pipe or console read returns one transfer, including
short progress. A read first returns bytes the associated FILE read ahead,
without a backend call; read itself never reads ahead.

`pread` and `pwrite` take an explicit off_t file offset and leave the descriptor's
private position unchanged. They call native FILE operations directly, never
seek/restore. Descriptor/access and count checks precede negative-offset EINVAL,
then nonfile ESPIPE; even zero-count requests must name a file and a nonnegative
offset. A valid zero-count request touches no buffer/backend or read-ahead.
Pread does not consume or refill read-ahead. Nonempty pwrite discards it before
dispatch, including an uncertain mutation outcome; later reads refetch at the
same private position. Short progress and EOF retain the read/write contract,
without altering FILE indicators or pushback. Internal FILE append policy does
not override pwrite's offset, and neither does O_APPEND. See
[positioned-I/O qualification](libc-portability.md#positioned-file-io-and-qualification).

Successful zero reads report EOF, including independent terminal input;
nonempty zero writes report EIO. Native denial remains EACCES, unsupported
operations remain ENOTSUP and pipe writes with no remaining reader report EPIPE.

Ordinary read/write and lseek use the private cursor shared with an associated
FILE; pread/pwrite leave it unchanged. Descriptor calls never read or change
FILE EOF/error indicators. A prior FILE EOF does not suppress read;
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
same root bindings and current working-directory chain as fopen. Selection and
removal happen in one kernel operation, without first probing the child type.
Existing FILE streams keep their original object after removal; reopening the
name fails unless another entry has been created there. Roots and final `.` or
`..` are rejected, and a trailing slash requires a directory.

Nonempty directories report ENOTEMPTY. `unlink` selects only files, while
`rmdir` selects only directories in the same native removal operation.
Rmdir reports ENOTDIR for a wrong type, ENOENT for a missing name and EACCES
for denied authority. There is no recursive behavior or POSIX kernel syscall
adapter. See [path qualification](libc-portability.md#path-and-byte-order-qualification).

## Directory creation

`mkdir(path, mode)` from `<sys/stat.h>` creates one directory through the
parent's CREATE right, using the same root bindings and current directory chain
as fopen. `mode` has no effect: native directories carry no permission bits. A
trailing slash is accepted. An existing name of either kind fails with EEXIST,
as does a root or a final `.` or `..` that names an existing directory. A
missing parent fails with ENOENT; parents are not created. The new directory's handle is closed before return.

## Rename

`rename(old_path, new_path)` calls native replacement rename for files and
RAM/npfs directories. Same-kind empty directory replacement is allowed;
nonempty victims return ENOTEMPTY. A destination is the exact new name, including
an existing directory; libc never treats it as a container. Trailing separators
require directory operands atomically. Roots and final `.`/`..` refuse.

Open handles, delegated roots and cwd chains keep their identities and rights;
`getcwd` spelling may become stale. Cycles return EINVAL. Cross-store/mount and
FAT32/host directory moves return ENOTSUP; no copy/delete fallback is attempted.
Read-only backing returns EROFS after capability checks. Generic native kind
mismatch currently maps to EINVAL, not exact POSIX EISDIR/ENOTDIR; EXDEV is not
claimed. IO or uncertain completion returns EIO without an automatic retry.
See [native rename](../interfaces/directories.md#atomic-rename-and-directory-moves).

## Transfers and positions

`fread` and `fwrite` return complete element counts and check size/count overflow
before I/O. A partial final element may have consumed or queued bytes even though
it is not included in that count. Unbuffered `fwrite` counts confirmed backend
progress; buffered `fwrite` counts bytes accepted into FILE ownership, including
retained bytes after a flush error. Check `ferror` and `fflush` even for a full
return count. File position belongs to the shared open object; the underlying
native capability has no seek position. File writes use that offset. Native file calls transfer at most 4,088 bytes per read or 4,080 bytes per
write; `fread`/`fwrite` continue across those boundaries using their existing loops.
File, terminal and pipe output continue positive short writes until complete or
an error, submitting only the remaining suffix. Zero progress or an excessive
count is rejected; file position advances only for confirmed bytes. On a later
failure, the position includes confirmed bytes of any partial final element,
while `ferror`/`errno` retain the failure. Neither `fwrite` nor formatting helpers
automatically retry an error. Append uses each operation's returned start and
confirmed count to update the cursor; it does not issue a preceding SIZE call.
An informational tell of buffered append output can still sample SIZE; that is
not a mutation or a reservation.

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

Descriptor writes, and reads of pipes and consoles, return one backend result:
progress or an error. A regular-file `read` repeats its transfers as described
above, and `fread_some` stays a single transfer. FILE alone updates EOF/error indicators and implements the fread/fwrite loops.
A FILE's sticky EOF does not suppress a descriptor transfer. Selected access
mode and native rights remain separate: invalid entries and wrong modes report
EBADF, while native authority denial remains EACCES and unsupported operations
remain ENOTSUP. A successful short transfer remains successful progress.

### Output buffering

Output defaults to `_IONBF`. `setvbuf` before stream I/O selects `_IOFBF` to
flush a full output buffer, `_IOLBF` to additionally flush through each newline,
or `_IONBF` for immediate output and exact future input reads by that FILE.
Existing shared input read-ahead is still consumed. Full/line buffering on an
input-only FILE fails with ENOTSUP; writable update streams retain the existing
input read-ahead policy unless `_IONBF` is selected.

A supplied output buffer is borrowed until `fclose` and needs nonzero size;
NULL allocates the requested size, or BUFSIZ when size is zero. Allocation or
configuration failure preserves the previous selection. Invalid modes, late
configuration and zero-sized supplied buffered storage report EINVAL.
`setbuf(stream, buffer)` selects full buffering with BUFSIZ bytes;
`setbuf(stream, NULL)` selects `_IONBF`.

Queued output belongs to FILE, separately from the shared descriptor object.
Flush removes only confirmed prefixes and retains the unconfirmed suffix on
failure. A previously failed queue must drain before a later `fwrite` accepts
new bytes, including after `clearerr`; a failed drain accepts zero new bytes.
Recover queued output with `fflush`, rather than repeating bytes already accepted.
Flush before direct descriptor I/O, close/replacement or stream delegation.
`fflush` drains output without a native SYNC or durability promise; uncertain
native mutation outcomes still have the EIO limitation described above.

### Input read-ahead

Each open object owns an optional BUFSIZ read-ahead buffer beside its handle
and logical position, shared by all descriptor aliases. `fread`, and the character
and line input built on it, may fill that buffer with one backend transfer from
a file or pipe and return the excess on later reads. Requests of at least BUFSIZ
bytes read directly into the caller's memory. A file fill stays below LONG_MAX,
the stdio position range, which also keeps it inside `host://`'s signed offset
range. At or past that boundary, or when the request itself does not fit below
it, the read is exact, so speculation never turns a valid read into an error
such as EOVERFLOW. A fill happens only while the buffer is empty: EOF or a
backend error reaches the request that caused it, after every buffered byte.
Consoles are never read ahead: their input is shared with the parent shell, and
ISO C treats them as interactive. `read` and `fread_some` consume buffered bytes
first and never read ahead, so mixing them with stdio through aliases
loses no bytes and exact consumers such as head stay exact.

The buffer is allocated on the first buffered read and freed on final descriptor
close. If allocation fails, the open object stays unbuffered without an error.
File writes, successful seeks and input `fflush` discard shared file read-ahead
because files can refetch it at the logical position. Pipe read-ahead survives
until consumed or the final alias closes. Buffered file bytes can be stale if an
independently opened descriptor or another process writes the file; seeking or
input `fflush` refetches them.

Read-ahead is private process memory and never accompanies a delegated stream.
A child given the same pipe sees only bytes not yet fetched; a child given a
file-backed stream starts at offset 0, as before. Read a stream you will
delegate only with `read`, `fread_some`, or FILE input selected with `_IONBF`
before any read-ahead occurs.

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
  after `fseek` and input `fflush`, and `a+` append. TCC compile/run and a
  Bucharest `date` also worked.

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
[console line input](https://git.internal/PyxisOS/pyxis-os/issues/1371).
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
offset plus queued output cannot fit in long. Terminal and pipe positions fail
with ESPIPE. `ftell` excludes input read-ahead and includes queued output;
pending append output uses the held file's current size rather than its read or
seek cursor. Size-query or overflow failure changes neither queue nor cursor.
Without pending output it reports the current cursor.

`fseek` flushes queued output before moving. A flush failure prevents the seek;
a later seek failure can still follow successful output delivery and cursor
advancement. The descriptor seek itself validates before discarding input
read-ahead, so failed input-only seeks preserve read-ahead and indicators.
A write on an update stream drops file read-ahead first, so no `fseek` is needed
between reading and writing. ISO C requires one; omitting it is a Pyxis
guarantee, not portable behavior.

## Pushback and scanning

`ungetc` keeps one byte per console/file/pipe FILE, returned before further
input, including read-ahead. Forward-only STREAMs reject byte and wide pushback
with ESPIPE; this leaves EOF and consumption unchanged. Completed HTTP FILE
snapshots follow the ordinary file pushback and seek rules. A second `ungetc` before a read fails, and `EOF` is never pushed.
It clears the EOF indicator, and `ftell` reports one less (not below zero).
A successful `fseek` (a `SEEK_CUR` offset counts from the position before the
pushback), input `fflush` and a stdio write discard it. Pushback belongs to the
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

The process environment supplies independent `stdin`, `stdout` and `stderr`
capability entries. Each exposes
`PROTOCOL_CONSOLE`, `PROTOCOL_FILE` or `PROTOCOL_PIPE` and owns a distinct child
handle with only READ authority for stdin or WRITE authority for stdout/stderr.
An exported FILE additionally retains CALL transport; native streams retain zero.
FILE output can pair with an exact TEXT `stdout.append=1` or `stderr.append=1`
entry from that same startup snapshot. Libc adopts native APPEND policy;
malformed policy or a failed zero APPEND support probe is an error. Stream-export and
launch helpers preserve it and omit stale companions when replacing streams.
The shell declares it for redirected append-capable FILE output after truncating,
so a program that launches commands passes append on to them. Unsupported plain
HOST output uses a positioned byte relay instead. There is no stdin append
companion or extra authority implied by the text.
HTTP input is a completed READ-only FILE snapshot, with no HTTP-specific
standard-stream companion. Generic STREAM inputs retain their completion and
failure contract. Native and exported FILE opens use the shared
[typed provider route](../interfaces/file-providers.md).
Runtime captures the three streams from one immutable environment snapshot and
adopts its owned LOOKUP grants into descriptors 0, 1 and 2 before heap
initialization. The temporary snapshot closes before constructors/main. Closing
or replacing an original descriptor conditionally removes its matching environment
publication identities; newer entries and duplicate descriptors remain intact.
There is no hidden startup stream owner.
Closing stdout cannot close stderr or a named terminal grant. Normal boot binds
all three to the space console.

An absent entry makes that stream unavailable without
preventing the process from running. Its first nonempty I/O fails with EBADF and
sets the error indicator; missing stdin is not EOF. There is no fallback to a
terminal, another standard stream or the kernel log. Zero-size transfers remain
no-ops. Later allocation of the absent stream's descriptor number does not make
its FILE available. Unknown protocols and malformed bindings are rejected during
launch/startup. A runtime setup failure completes as `PROCESS_START_FAILED`;
the parent can report the stream and native error even before stderr exists.
It does not acquire another output capability or write through the kernel log.

The [shell](shell.md#file-redirection-and-stdin) can supply these bindings through
foreground file redirects and [pipelines](shell.md#foreground-pipelines).
The [stream reference](shell-streams.md) describes delegation and lifetime;
[head](shell.md#bounded-input-with-head) provides exact bounded consumption.
File-backed standard streams start at offset zero, with independent per-descriptor
positions. Adoption does not open, truncate or append
to the file. Two output streams backed by the same object can overwrite one
another because their positions are independent. Console and pipe streams remain
sequential and cannot seek. Completed HTTP FILE snapshots support positioned
reads and independent child cursors.
[Pipes](../interfaces/pipes.md) use reference-based EOF and report EPIPE when
their last reader closes.

For native launch adapters, `<pyxis/stdio.h>` provides
`pyxis_stdio_stream(FILE *, struct pyxis_stdio_binding *)`. It checks the current
FILE registry and descriptor association, returning a borrowed protocol/handle
and, for HTTP responses, the declared observation protocol/handle.
Closed or absent standard wrappers return an empty binding; descriptor-number reuse never
reconnects them. Success preserves errno, indicators, cursor, pushback and
read-ahead, and acquires no reference. The caller must not close either handle or
use it after owner close or descriptor replacement. Flush queued output before
delegating. This lets [Lua](lua.md) delegate live C streams without reusing the
original environment binding. HTTP FILE children retain the same frozen backing
and begin at offset zero; explicitly supplied observation companions retain only
STATUS authority. Buffered bytes, queued output and the parent's cursor are not
transferred. Append policy uses its separately declared companion.

Named `input`/`output` console grants, plus `keyboard`, remain separate terminal
resources; libc never uses them to fill a missing standard-stream binding.

`fflush(stream)` drains queued output, then drops FILE pushback and file
read-ahead so later reads refetch; pipe read-ahead remains. ISO C leaves input
`fflush` undefined, so this is a Pyxis guarantee. `fflush(NULL)` attempts all
registered output queues, reports the first error after trying the others, and
leaves input alone. No flush clears an earlier error indicator.
Normal exit stages all buffers, closes each live descriptor once (committing final
clean transactions), then disposes of FILE metadata, including invalid associations.
Cleanup failure changes requested exit zero to EXIT_FAILURE; a nonzero status
is preserved. Exit handlers and `.fini_array` run before this cleanup,
so they can still write to the streams; see
[startup and exit](../development/sdk.md#startup-exit-and-layout). `_Exit` and
fatal faults bypass libc cleanup; the kernel still reclaims process resources.
There is no wide I/O. `fseeko` and `ftello`
behave exactly like `fseek` and `ftell`, because `off_t` is `long`. Scanning
and one-byte pushback are described above.

`fclose` attempts to flush, then invalidates its association and closes the
descriptor even after a flush failure. It preserves the first error, frees owned
output storage and disposes of the wrapper; caller storage remains caller owned.
Other aliases retain the open object; only its final reference makes one native
close attempt. Success returns zero without changing errno. Failure returns EOF
with errno; queued output cannot be recovered from that closed FILE. Static
standard wrappers remain invalid.
Closing a stale FILE returns EOF/EBADF without touching a reused descriptor.
Native CLOSE currently returns only success or BAD_HANDLE; neither leaves an
owned native entry. Other native failures retain their errno translation, while
an unknown status or malformed reply reports EIO. In those unexpected cases a
surviving native reference may remain until process exit and delay pipe peer
closure. There is no hidden copy or retry list. Open rollback uses the same
single-attempt release policy while preserving the open failure's errno. See
[the close-failure contract](libc-portability.md#close-failure-and-cleanup)
and [its cleanup limit](https://git.internal/PyxisOS/pyxis-os/issues/1375).

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
| BAD_REQUEST, WRONG_TYPE | EINVAL, except EILSEQ when BAD_REQUEST answers a path that is not valid UTF-8 |
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
| NAME_TOO_LONG | ENAMETOOLONG |
| LINK_NOT_FOLLOWED | ELOOP |
| NOT_FOUND | ENOENT |
| ALREADY_EXISTS | EEXIST |
| READ_ONLY | EROFS |
| NOT_EMPTY | ENOTEMPTY |
| IO, OUTCOME_UNKNOWN, INPUT_LOST, unrecognized failure | EIO |

Access and rmdir map WRONG_TYPE to ENOTDIR because either a required directory
component or rmdir's target has the wrong kind. Other callers retain EINVAL;
the native result does not distinguish final from intermediate mismatches.
Permission checks
may reject a mutation before the backing reports READ_ONLY. Code should inspect
native statuses directly when it needs the original protocol detail.

## Memory output streams

`open_memstream(&buffer, &size)` creates a byte-oriented, write-only FILE over
private growable heap storage. It needs no filesystem capability or descriptor;
`fileno` fails with EBADF. Reads fail with EBADF and wide output with EINVAL.
Ordinary buffering, error indicators and locking apply. Successful flush/close
updates the caller's pointer and size. After close the caller owns the allocation
and frees it; until then the published pointer is borrowed and may change on output.

The terminator stays at the full buffer's high-water end. The published size is
the smaller of that length and the current position; backward seeks retain later
bytes. SEEK_END uses the high-water length. Seeking beyond it allocates nothing
until a write, which zero-fills the gap. Negative/overflow positions fail without
moving it. Allocation and write-overflow failures set the FILE error; a failed
close still transfers the confirmed buffer and returns EOF. No typed capability
handoff or descriptor alias exists for this process-private stream.

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
name. `mkdtemp(char *)` creates a new directory the same way and returns the
template; the caller removes it. There is no ISO C `tmpnam`: Lua's `os.tmpname`
deliberately reserves a file instead of returning an unreserved name.

## Wide FILE I/O

A FILE starts unoriented; `fwide` or its first actual byte/wide operation selects
orientation. Empty string/formatted byte operations select byte orientation too;
zero-element fread/fwrite do not. Mixing byte and wide operations refuses with
EINVAL and the stream error indicator. Successful seeks preserve orientation but
reset decoder/pushback state; failed seeks preserve it. Descriptor I/O remains a
byte interface and shares the original cursor/read-ahead independently of FILE
orientation.

`fgetwc/getwc`, `fputwc/putwc` and `ungetwc` use UTF-8 and the existing native
read-ahead/output buffering. One whole scalar may be pushed back, with its encoded
byte width reflected in ftell; this is independent of byte pushback. Malformed or
truncated input sets EILSEQ/ferror. Incomplete input survives a native I/O error;
EOF in an incomplete scalar resets it and reports an error. Wide writes reject
invalid scalars before output and return WEOF for native or forced-buffer-flush
failure. Wide scanning, full wide stdio and freopen remain unavailable.
