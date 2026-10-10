# Libc portability over the native ABI

Libc targets ISO C, plus the [proven extensions](#which-standard-functions-belong-in-libc)
below: standard headers and functions follow their standard contract, and gaps
are recorded rather than approximated. The descriptor layer
(`open`, `read`, `write`, `close`) is a bounded portability surface for ported
software, not Unix emulation. The kernel, libpyxis and native interfaces remain
capability-based and deliberately non-Unix; libc adapts them rather than
reshaping them. Behavior that ISO C leaves undefined may be given a documented
Pyxis guarantee, which is not portable behavior.

The descriptor portability milestone is complete: libc provides process-local
open/read/write/close over native file, console and pipe capabilities, shared
with existing stdio. The image packages upstream sbase cksum and a restricted
tee as consumers. This is a supported subset, not full ISO C or POSIX conformance
or binary compatibility with another OS.

Libc runs with the caller's grants. A descriptor number is neither a kernel
handle nor authority; conventional library calls do not require a kernel
POSIX syscall layer or a global Unix filesystem namespace. Native libpyxis
interfaces remain available for explicit capability operations.

Ordinary library behavior belongs in shared libc adapters where possible.
Application patches cover platform integration and explicitly restricted
features; missing semantics are recorded as limitations rather than successful
stubs. Existing ports are not retroactively rewritten by this milestone.

## Which standard functions belong in libc

Owner decision, 2026-10-04: libc provides ISO C plus extensions that have proven
useful, such as POSIX `mkdir`, implemented with native constructs. A port that
needs such a function gets it added here, resolving paths through startup roots
and the working directory like `fopen`, `remove` and `rename`. The port then
calls the standard function rather than its own capability code.

The test is whether the function maps onto objects and operations that already
exist. Creating a directory does; POSIX threads, `fork` and signals do not. Those
need a native design first. No kernel mechanism is added only to satisfy a POSIX
contract. Arguments with no native meaning are documented as such. For example,
`mkdir`'s `mode` has no effect because npfs has no permissions. Missing authority
or support returns a real error, never a successful stub.

## Ownership and stdio integration

Userland's private `libc/descriptor.c` stores the native handle, backend kind,
selected access mode, append policy, cursor and any
[input read-ahead](stdio.md#input-read-ahead) directly in each entry. The
lowest free slot is reserved for an open, including closed or absent standard
slots. Descriptors 0–2 use static startup storage; later growth uses the heap.
Allocation failure reports ENOMEM and descriptor-number exhaustion EMFILE.

FILE stores a descriptor number, never a pointer into the relocatable table.
Each entry has at most one non-owning FILE back-pointer. The association adds
no native reference: close releases the handle and invalidates the association
before the number can be reused. After `close(1)`, nonempty output through the
old stdout fails with EBADF; neither later output nor fclose of that wrapper
can affect a new occupant of descriptor 1. Public open creates no FILE wrapper.

Fopen allocates its wrapper and reserves descriptor capacity before opening or
truncating. Path workspace allocation and authority resolution also precede
truncation. Publication after successful truncation requires no allocation;
failure releases reservations and attempts to release opened handles under the
close policy below. Existing r/w/a modes, update forms, seeking and non-atomic
append use this same ownership/cursor machinery.

Backend operations report one transfer or an error. FILE alone owns sticky
EOF/error indicators and the fread/fwrite loops. Descriptor reads remain usable
after a FILE observes EOF. `read` of a regular file repeats native transfers to
fill the request or reach the end, while pipe and console reads stay one
transfer; both return bytes a FILE read ahead before any backend transfer.
Supported operations, selected mode and granted native rights remain separate;
an adapter preserves authority and reports denied or unsupported operations.

Startup entries adopt the exclusive standard-stream handles before heap
initialization without copying or changing their rights. Missing bindings have
no fallback authority. `startup_stream()` is an immutable borrowed snapshot:
it retains no reference, becomes stale when its owning descriptor closes, and
must then neither be used nor forwarded to a child. Number reuse does not
refresh it. See [stdio](stdio.md) and [stream delegation](shell-streams.md).

## Temporary files and live stream delegation

`mkstemp` maps exclusive named creation onto the SDK's `path_create_file`;
existing names fail, without the open-or-create fallback used by `fopen`.
Templates end in six `X` characters and require clock/random capabilities.
`tmpfile` preflights removal authority and immediately unlinks its exclusive
`tmp://` file, leaving the real held file object alive. See
[temporary-file ownership](stdio.md#temporary-files). No fake `tmpnam` or
buffer-control functions are exported.

The native `<pyxis/stdio.h>` accessor borrows a current FILE-owned stream for
launch delegation. It adds no descriptor alias or native reference and preserves
private stdio state. Closed standard streams are NONE even after descriptor
reuse; allocated invalid streams fail with EBADF. See
[live stream delegation](stdio.md).

## Regular expressions and UTF-8 conversion

The SDK exports `regex.h` with `regcomp`, `regexec`, `regerror` and `regfree`,
using the existing musl 1.2.5 TRE-derived engine in libc. BRE is the default;
`REG_EXTENDED` selects ERE. Compilation supports `REG_ICASE`, `REG_NOSUB` and
`REG_NEWLINE`; execution supports `REG_NOTBOL` and `REG_NOTEOL`. Captures use
signed byte offsets in `regmatch_t`, with `-1` for unmatched subexpressions.
`regcomp` returns an error code directly. A successfully compiled `regex_t` owns
heap storage: do not copy it, and release it with `regfree` after use.
`regerror` returns readable English messages and the required buffer size
including NUL, supports a zero-size query, and terminates truncated output.

`stdlib.h` provides stateless UTF-8 `mbtowc`. `MB_CUR_MAX` and `MB_LEN_MAX` are 4.
The decoder accepts Unicode scalar values, returns bytes consumed (zero for
NUL), and rejects incomplete, overlong, surrogate and out-of-range sequences
with `-1` and `EILSEQ`. A NULL input resets the stateless decoder and returns
zero. A NULL output discards the decoded value; errors leave output unchanged.
`wchar.h` provides the compiler's 32-bit `wchar_t`, unsigned `wint_t` and `WEOF`.
No restartable conversion, wide I/O or locale state is added.

`wctype.h` provides all twelve standard `isw*` classifications, `wctype`,
`iswctype`, `towlower` and `towupper`. Classification and case conversion are
exact for ASCII only. Every non-ASCII value is outside every class and folds
to itself; Unicode tables are absent. The regex engine still matches non-ASCII
literals and code-point ranges, and `.` consumes one whole UTF-8 code point.
Negated ASCII classes can match non-ASCII characters. Collating symbols and
equivalence classes are unsupported (`REG_ECOLLATE`).

Owner decision, 2026-10-07: preserve the pinned engine's back-reference limits.
BRE back-references compare bytes without case folding even with `REG_ICASE`;
UTF-8 back-reference matching and offsets are unreliable because the backtracking
path assumes single-byte lookahead and does not fully restore decoder stride.
Malformed UTF-8 patterns are rejected (usually `REG_BADPAT`, or `REG_ERANGE` for
an invalid range endpoint). Encountered invalid subject UTF-8 yields
`REG_NOMATCH`, but validation is lazy: the engine may return before reading the
whole subject. See [regex limits](../technical-debt.md#regex-character-classes-and-back-references).
vi and less use this interface for BRE search; vi also uses it for substitution.
Their display and bounded-input limits remain in the respective port docs.

The 2026-10-07 validation used an uncommitted 37-check program, cross-compiled
against the exported SDK and uploaded to `tmp://` in a four-CPU QEMU/KVM guest
(512 MiB, OVMF, virtio-net/rng, no raw disks). It exited successfully with zero
failures. BRE capture/back-reference/repetition, ERE alternation and leftmost-longest
selection, named classes/ranges, every listed flag, ASCII folding and UTF-8
literal/range/dot matching passed. In the subject `é€😀`, `^.(.).$` captured byte
offsets `[2,5)` and matched `[0,9)`. Malformed brackets, parentheses, class names,
repetition and UTF-8 returned readable errors; `regerror` size queries and
truncated buffers passed. The decoder rejected incomplete, overlong, surrogate
and out-of-range encodings; non-ASCII class exclusion and identity folding passed.
The accepted case-insensitive back-reference limit was also observed.
Read-only GDB at `regexec` confirmed one explicit capture and two engine
submatches for the BRE capture case. The libc/SDK rebuild had no warnings;
the full source image build passed with warnings in unchanged third-party ports.

## Public descriptor interface

The SDK exports nested libc headers, including `sys/types.h`. Its current
x86-64 LP64 interface is:

| Header | Supported interface |
| --- | --- |
| `fcntl.h` | `open(path, flags, ...)`; O_RDONLY = 0, O_WRONLY = 1, O_RDWR = 2, O_CREAT = 0x100, O_TRUNC = 0x200, O_EXCL = 0x400, O_APPEND = 0x800 |
| `unistd.h` | read, write, pread, pwrite, close, lseek, ftruncate, fsync, unlink, rmdir, access, isatty; F_OK/X_OK/W_OK/R_OK = 0/1/2/4; STDIN_FILENO/STDOUT_FILENO/STDERR_FILENO = 0/1/2 |
| `sys/stat.h` | stat, lstat, fstat, mkdir; type-only st_mode and file st_size; independently valid st_dev/st_ino/st_mtim under st_valid |
| `arpa/inet.h` | htonl, htons, ntohl, ntohs only; no socket or address parsing/formatting declarations |
| `sys/types.h` | ssize_t and off_t as signed long; mode_t as unsigned int; dev_t and ino_t as uint64_t |
| `limits.h` | SSIZE_MAX as LONG_MAX |
| `stdio.h` | Existing FILE interface and BUFSIZ = 8192 |
| `inttypes.h` | PRId/PRIi/PRIo/PRIu/PRIx/PRIX output macros for fixed-width 8/16/32/64-bit, pointer and greatest-width types |

Open selects exactly one of read-only, write-only or read/write access. Create
and truncate require writable access. O_EXCL requires O_CREAT and uses native
exclusive creation: after authority checks, an existing name fails with EEXIST,
without opening or truncating it, even with O_TRUNC. It never falls back to
opening a concurrent creator's file. O_RDWR requires both native READ and WRITE
rights; denied access is not attenuated to one direction.

Unknown flags, combined O_WRONLY|O_RDWR, read-only mutation and O_EXCL without
O_CREAT fail with EINVAL before lookup or examining the variadic argument.
With O_CREAT, open consumes mode_t
and accepts only 0666 as native creation policy; other modes fail with ENOTSUP
before lookup, even for existing files. No mode argument is read otherwise.
This creates no permission system or additional rights; virtio-fs retains its
0644 creation request. Paths follow the existing [capability resolver](paths.md).

Read/write first check descriptor and access validity (EBADF), then reject
counts above SSIZE_MAX (EINVAL). A valid zero-count call returns zero without
touching the buffer or backend. A nonempty `read` of a regular file returns the
full count unless the file ends first (an error after progress returns the bytes
read). Other nonempty calls return one confirmed transfer, including positive
short progress, or -1 with errno. File/pipe zero reads mean EOF; unexpected console zero progress and nonempty zero writes mean EIO.
Descriptor calls neither inspect nor update FILE indicators.

The [descriptor I/O reference](stdio.md#descriptor-io) and
[native errno mapping](stdio.md#native-error-translation) give the detailed
contract. lseek changes the descriptor's private position; pread/pwrite use an
explicit file offset without changing it. There is no shared cursor.
O_APPEND gives a descriptor fopen's "a" policy; fdopen and duplication remain
absent.
fileno exposes an existing FILE descriptor without adding an alias.

### Read/write and exclusive-create qualification

[Userland PR #171](https://git.internal/PyxisOS/pyxis-userland/pulls/171), commit
`ff278aec50adfaf6af8d8c15062084a8594642e3`, implements the first
[Git source-acquisition task](../wip/git-on-pyxis.md#task-status-and-accepted-decisions).
The ordinary `make -j16 image` build used parent `32ee113d`, userland `f5c4de2`
plus the exact two-file change subsequently committed above, ports `f2da003d`
and filesystem `b427df29`. Clang/LLD 23.1.3 came from the existing
`pyxis-builder:pyxis-llvm23.1.3-49e2c1a` container; no compiler rebuild was needed.
The libc build had no warnings. The full image retained vendor warnings and
exposed a macro redefinition in Links' existing unsupported O_EXCL sentinel;
Links' private-mode/configuration-save refusal is unchanged by this task.

Manual qualification used nested-KVM Q35, two vCPUs, 256 MiB, VirtIO networking,
RNG and virtio-fs, with the matching raw OVMF pair. A disposable observation
program launched from `host://` performed file operations in RAM `tmp://` and
denied creation in read-only `boot://`. It was not packaged or committed.

| Observation | Result |
| --- | --- |
| `O_RDWR\|O_CREAT\|O_EXCL`, mode 0666 | Descriptor 3 opened with both access directions; wrote six bytes `abcdef` |
| Duplicate name with O_EXCL and O_TRUNC | -1/EEXIST; the held file and its six bytes remained unchanged |
| lseek to 2, write `XY`, seek to 0, read | `abXYef`, six bytes; close and O_RDWR reopen retained the same bytes |
| Repeated denied exclusive creates | -1/EACCES; subsequent authorized creation reused descriptor 3 |
| Invalid access, read-only create, unknown bit, O_EXCL without O_CREAT | -1/EINVAL; invalid create flags were exercised without a mode argument |
| Mode 0600 on an existing name | -1/ENOTSUP before lookup |

Read-only GDB inspection at descriptor_open, path_create_file and the probe's
observation points confirmed reservation before path work, requested native
rights READ|WRITE, CALL_DENIED with no returned child handle, invalidation of the
copied path handle and a free failed-open slot. The duplicate attempt freed
descriptor 4 and left descriptor 3's handle, position and access intact.
Descriptor capacity retained normal growth to six slots; cleanup promises live
ownership release, not shrinking its allocation. No debugger function calls,
fault injection, new tests or boot/output automation were used. Task-owned QEMU,
debugger, remote client and virtiofsd processes were stopped.

This qualifies the selected RAM-file operations and ordinary denied-create
unwind. Racing writers, forced allocation failure, malformed close replies,
NPFS/host mutation and power-loss durability were not exercised. Their unchanged
preflight and uncertain-close rules were inspected in source.

### Positioned file I/O and qualification

`pread` and `pwrite` use the native FILE protocol's explicit offset directly,
without seek/restore, new libc allocation or a fill/retry loop. Short counts, read EOF
and native errno translation match read/write. Descriptor/access validation
precedes the SSIZE_MAX count limit, then negative-offset EINVAL, then nonfile
ESPIPE. This also applies to zero-count calls: a valid file request returns
zero without touching bytes or the backend, but negative offsets and nonfiles
are still refused. Backend offset/range restrictions retain their real errors.

Pread bypasses read-ahead without consuming or filling it. Nonempty pwrite drops
that descriptor's read-ahead before the native call; it stays discarded on
uncertain failure so later reads refetch current bytes. Neither operation changes
the private position, append flag, associated FILE pushback or EOF/error state.
Pwrite uses its explicit offset even through fileno on an append FILE. A caller's
ungetc byte is FILE state, not speculative file data. Other open descriptors'
buffers retain the existing [read-ahead limits](stdio.md#input-read-ahead).

The helpers snapshot the already-owned handle before blocking and never access
a descriptor-entry pointer afterward. The current one-user-task-per-process
contract keeps that handle owned during the call; no extra reference or lock is
introduced. Mutation failure/uncertainty returns an error, without replay or an
invented count.

[Userland PR #172](https://git.internal/PyxisOS/pyxis-userland/pulls/172) publishes
`88217be07f089c90d79c71b3cf9387f7420d0ec9`. The ordinary `make -j16 image` build
passed at parent `5cf60bc3`, userland `751345c` plus the exact four-file change
subsequently committed above, ports `f2da003d` and filesystem `b427df29`.
The existing Clang/LLD 23.1.3 builder was used; libc had no warnings, while
unchanged third-party warnings, including Links' O_EXCL override, remained.
No compiler or kernel/protocol change was needed.

Manual qualification used nested-KVM Q35, two vCPUs, 256 MiB, VirtIO network/RNG,
virtio-fs for launching a disposable observation program, and RAM `tmp://` for
its file operations. The probe was not packaged or committed.

| Observation | Result |
| --- | --- |
| Buffered read of `a`, then pread offset 4 from `abcdef` | Returned `ef`; cursor stayed 1 with five unread bytes; ordinary read next returned `b` and advanced to 2 |
| Pwrite `XY` at offset 2 | Returned 2; cursor stayed 2; four unread bytes were discarded before FILE_WRITE; next buffered read returned `X`, then ordinary read returned `Y` |
| Ordinary write `Q` at private offset 4, then pread offset 0 | Ordinary write advanced to 5; pread returned `abXYQf` and kept cursor 5 |
| Pwrite `Z` at offset 9 past six-byte EOF | Returned 1; size became 10; pread of gap returned `00 00 00 5a`; cursor stayed 5 |
| 8192-byte requests after resize to 8192 | One pwrite returned 4080; one pread returned 4088; both kept cursor 5, matching native transfer limits |
| Pread at EOF | Returned zero; cursor, FILE EOF and error indicators were unchanged |
| Negative offsets / invalid descriptor / console type | EINVAL / EBADF / ESPIPE; zero-count console pwrite also returned ESPIPE |
| Zero-count file pwrite at offset 30 | Returned zero; size remained 10 and cursor remained 5 |
| Pwrite on an `a+` FILE | Wrote `L` at offset zero; its private cursor stayed zero |

Read-only GDB confirmed these cursor and cache states, including read-ahead
already empty at native file_write's explicit offset 2. The inspected helpers
do not use entry pointers after the blocking call. Associated FILE indicators
were unchanged after EOF and refused operations. Source inspection establishes
preserved caller-supplied pushback, zero-count cache preservation and invalidation
before uncertain mutation; these were not forced separately at runtime.

No new tests, fault injection, boot/output automation or performance measurement
was added. Task-owned QEMU, GDB, remote client and virtiofsd processes are stopped.
Persistent backend writes, racing callers, malformed/uncertain replies and
allocation-failure paths were not qualified by these RAM-file observations.

## Native path checks and removal

Owner decision, 2026-10-09: `access` reports a minimal native authority check,
not Unix mode/owner permission checks. It resolves through the caller's startup
roots and working-directory grants and releases each temporary owned handle
immediately using the validated [close policy](#close-failure-and-cleanup).

| Request | File rights | Directory rights |
| --- | --- | --- |
| F_OK | None | None |
| R_OK | READ | ENUMERATE |
| W_OK | WRITE | CREATE and REMOVE |
| R_OK \| W_OK | READ and WRITE | ENUMERATE, CREATE and REMOVE |

Kind detection first requests no file rights, so a directory check does not
require the unrelated READ_FILES/WRITE_FILES delegation rights. File R/W then
does a second lookup with the requested rights. Traversal still needs LOOKUP;
success is only a time-of-check observation. It does not promise later lookup,
backing I/O, host permission or volume writability. No content is read or written.
X_OK returns ENOTSUP: launch requires an executable READ grant and separate
launcher authority, with no native execute bit. Provider routes return ENOTSUP
after binding discovery and before provider OPEN; unknown/unavailable routes
retain the resolver's real errors. Unknown mode bits and NULL/empty paths return
EINVAL; missing, denied and intermediate-file paths return ENOENT, EACCES and
ENOTDIR. A close failure is reported, without retry or successful fake cleanup.

`lstat` is equivalent to `stat` only while native filesystems have no symlinks
and lookup never follows host symlinks (ENOTSUP). This condition is recorded
beside its declaration; it does not expose symlink metadata. Existing sizing
authority and provider-request behavior are inherited from `stat`.

`rmdir` removes one empty directory through native parent REMOVE authority.
Nonempty, wrong-type and missing paths return ENOTEMPTY, ENOTDIR and ENOENT;
denied removal returns EACCES. Roots and final dot/dot-dot components return
EINVAL. A trailing slash is accepted; no recursive deletion occurs. Only
access/rmdir translate WRONG_TYPE to ENOTDIR; existing generic translation
remains EINVAL. ENOTDIR and its strerror message are public libc additions.

The four byte-order helpers are pure fixed-width conversions for the current
little-endian x86-64 target. They leave errno unchanged. Their header supplies
no placeholder socket or inet functions.

### Path and byte-order qualification

[Userland PR #174](https://git.internal/PyxisOS/pyxis-userland/pulls/174) publishes
`7add29afabcba078a7e344dfd627fb451ef3a575`. The ordinary `make -j16 image` passed
with that exact commit, parent `d6733033`, ports `cff4a82b` and filesystem
`b427df29`, using the existing Clang/LLD 23.1.3 builder. Libc emitted no warnings;
existing vendor warnings remained. No compiler rebuild was needed.

Manual qualification used nested-KVM Q35, two vCPUs, 256 MiB, VirtIO network/RNG
and virtio-fs for a disposable observation executable and a host symlink.
File mutations used RAM `tmp://`; the program was not packaged or committed.

| Observation | Result |
| --- | --- |
| lstat on a four-byte file / directory | S_IFREG and size 4 / S_IFDIR and size 0 |
| lstat on missing / denied parent traversal / host symlink | ENOENT / EACCES / ENOTSUP |
| File and directory access F_OK, R_OK, W_OK, R_OK \| W_OK | All succeeded with the tmp grants |
| Boot file/directory W_OK / boot directory R_OK | EACCES / success |
| Access missing / intermediate file / host symlink | ENOENT / ENOTDIR / ENOTSUP |
| Access X_OK / unknown mode bit / NULL path / text provider | ENOTSUP / EINVAL / EINVAL / ENOTSUP |
| Rmdir nonempty / file / missing / intermediate file / denied parent | ENOTEMPTY / ENOTDIR / ENOENT / ENOTDIR / EACCES |
| Rmdir root / final dot or dot-dot | EINVAL |
| Rmdir empty directory with trailing slash | Success; subsequent lstat ENOENT; refused nonempty directory's child still present |
| htonl(0x11223344) / htons(0x1122) | Values 0x44332211 / 0x2211; memory bytes 11 22 33 44 / 11 22 |
| Inverse conversions / zero and all-one values | Original host values / unchanged boundary values; errno unchanged |

Read-only GDB observed directory requests of 0, 2, 40 and 42 (the exact native
rights above), successful lease slot reuse with changing handle generations,
and the wire bytes. A breakpoint at provider binding discovery fired; a
provider_open breakpoint did not fire before access returned ENOTSUP.
Source inspection establishes the native-only branch and existing one-attempt,
reply-validating close policy; no malformed/uncertain reply was forced.

Task-owned QEMU, debugger, client and virtiofsd processes are stopped. No kernel
change, tests, fault injection or boot/output automation was added. Persistent
backend removal, races and allocation-failure paths were not qualified by these
RAM-file observations.

## Close failure and cleanup

Code inspection of [close_handle](../../kernel/syscall.c) and
[capability_close](../../kernel/object/capability.c) establishes the current
native contract: CLOSE returns CALL_OK after removing the capability entry and
releasing its reference, or CALL_BAD_HANDLE if no matching entry exists. Both
return zero reply bytes. Closing requires no access right and does not perform
file sync or report storage writeback errors. Final object destruction can
still wait for the existing BSP retirement path.

Libc's internal release path inspects `syscall_close()` from the exported
syscall.h. The current `handle_close()` convenience helper reduces every
failure to -1 and discards both the status and malformed-reply distinction;
it cannot provide this translation. Libc uses this native syscall directly.

- An invalid/already-closed descriptor returns -1 with EBADF without a native
  call. For a valid descriptor, detach its native handle and invalidate the
  entry and every associated FILE before making one native close attempt.
  The descriptor remains free for reuse regardless of that result.
- CALL_OK with zero reply bytes returns 0 and leaves errno unchanged.
  CALL_BAD_HANDLE with zero reply bytes returns -1/EBADF. Neither current
  outcome leaves a native capability owned by that entry: success released it;
  BAD_HANDLE says it was already absent. Do not retry either outcome.
- A different in-range failure with zero reply bytes returns -1 using
  libc_call_errno. It is not an outcome produced by today's CLOSE path, and
  libc must not infer that it released the reference. An out-of-range status
  or nonzero reply_size is a malformed CLOSE result: return -1/EIO, including
  when status claims success. Reply validation precedes status translation.
- In the unexpected/malformed cases, any surviving native capability may
  remain until the kernel reclaims the process's capability table at exit,
  including `_Exit` or a fatal fault. That residual kernel reference could
  delay pipe peer closure until then. Libc does not retain a hidden native
  copy, retry list or live descriptor to make an uncertain release look like
  success. This is a failure-containment rule, not normal deferred close.
- Discard the closed entry's open-state metadata without allocation or another
  release attempt. An existing FILE may retain only the metadata needed for
  its invalid association and indicators until fclose/exit; it owns no native
  reference. Later fclose of that stale FILE returns EOF/EBADF and disposes of
  the wrapper without touching a reused descriptor. Fclose of a live FILE
  applies the same release policy, disposes of its wrapper even on failure,
  and reports EOF with the release errno if close fails. Static standard FILE
  storage stays allocated but invalid.
- Normal exit closes each remaining live descriptor once, including those
  without FILE wrappers, then reclaims invalid FILE metadata. Cleanup errors
  do not replace the process's requested exit status. There is no exit-time
  retry of descriptors already invalidated by close/fclose; the kernel's
  process teardown reclaims any residual native entries.

The same release primitive is used for temporary access-check handles and for
rollback of an internal open failure, preserving that open failure's errno.
A failed rollback release has the
same process-exit reclamation limit. Successful close is not a durability
promise; the existing explicit file-sync contract is unchanged.

## Packaged consumers

The [sbase recipe](../../ports/sbase/README.md) pins revision
`c546c3a5724c81cee9a11d816a38ccdf17472129`. It packages cksum and tee at
`bin://cksum.pxe` and `bin://tee.pxe`, with the full MIT license/contributor list
and arg.h notice under `boot://share/licenses/sbase`. The focused build includes
only their eprintf/fshut/ealloc/writeall helper closure. Ordered patches narrow
private util.h and adapt tee; cksum, helper bodies and arg.h remain unchanged.
No substitute SDK headers are installed.

Cksum uses conventional descriptor input and stdio output. It supports named
inputs, stdin, the explicit `-` operand and multiple operands with aggregate
failure status. Tee copies stdin to stdout and named outputs using
O_WRONLY|O_CREAT|O_TRUNC with 0666. Options -a/-i are rejected before opening
outputs; append and signals are not emulated.

Tee validates stdin and snapshots stdout availability with zero-length calls
before opens can reuse absent standard slots. Missing stdin fails before file
mutation; missing stdout reports failure while named copies may continue.
Failed outputs are diagnosed and closed once, surviving outputs continue, and
tee stops reading and closes stdin when no outputs remain. EOF/read failure
also closes remaining descriptors, reporting close errors without retries.
The upstream writeall loop handles positive short writes. Output may be partial
after an error; an output aliasing the input can truncate its contents.

See [port usage](../development/ports.md#checksums-with-sbase-cksum) for commands and limitations:

```text
cksum host://input
cat host://input | cksum
cksum < host://input > home://checksum.txt
cat host://input | tee home://first home://second | cksum
```

## Validation evidence

Implementation builds used `make -j16 image` with the installed Pyxis compiler;
no compiler rebuild was needed. Manual QEMU validation ran inside the development
VM on q35/KVM with 256 MiB, one/four CPUs for ownership integration, and four CPUs,
virtio-fs/RNG and no NIC for the packaged consumers. Existing Kilo save, guest TCC
compile/run, redirects and pipelines continued to work. Upstream signedness
warnings in cksum/tee remain.

Guest cksum and host GNU coreutils 9.10 matched these inputs:

| Input | Bytes | CRC |
| --- | ---: | ---: |
| Empty file | 0 | 4294967295 |
| `Pyxis cksum\nsecond line\n` | 24 | 3852765307 |
| Bytes 0–255 repeated 1024 times, then `\x00\xffPyxis\n` | 262152 | 3239341589 |

Named files, stdin, explicit `-`, missing/unreadable operands, redirects and
pipes were checked. Tee's named and stdout copies matched the binary byte for
byte, including creation, truncation of a larger file, empty input and RAM-file
input. Rejected -a/-i left existing outputs unchanged. A read-only output grant
failed without modifying the host file, while the surviving stdout copy completed.
Early stdout closure preserved a complete named copy; when no outputs remained,
tee closed stdin and the upstream writer reported EPIPE. Shell commands returned
without hanging.

GDB observed static-to-heap descriptor growth, number reuse, FILE invalidation
on close, unchanged borrowed startup snapshots, a 661-byte read from an
8192-byte request, and pipe reads/writes returning 4096 of 8192 requested bytes.
It also observed mode 0666 on writable create/truncate and descriptor input EOF
without setting stdin's FILE indicators. Invalid/access-mode precedence, zero
and oversized counts, unsupported flags/modes, absent streams, reads after sticky
FILE EOF, stale stdout after slot reuse, allocation failure and exceptional
close replies were checked by code inspection, not forced at runtime.

No tests, fault injection or boot/output automation were added. This reference
summarizes the completed implementation checks; its documentation-only closeout
does not claim a new boot or probe run.

## Pinned source probe

The [exact probe recipe](libc-probe/README.md) preserves the historical SDK/source
revisions, compile/link commands, [scratch patch](libc-probe/scratch.patch) and
license. The initial SDK lacked descriptor headers/functions; declaration-only
scratch headers exposed cksum's unresolved open/read/close and tee's unresolved
open/read/write/signal. Those artificial declarations never constituted runtime
support. The recipe also retains the later real-SDK cksum check. Current image
builds use the ports recipe above, not the probe's replacement headers.

## Remaining limits

The [libc compatibility gaps](../technical-debt.md#libc-compatibility-gaps) record
absent interfaces and their revisit points. [Console EOF](../technical-debt.md#console-input-completion),
[creation mode](../technical-debt.md#public-open-creation-mode),
[non-atomic append](../technical-debt.md#non-atomic-stdio-append),
[uncertain native release](../technical-debt.md#unexpected-native-close-failures),
[narrow file metadata](../technical-debt.md#narrow-libc-file-metadata) and
[file alias side effects](../technical-debt.md#shell-redirection-side-effects-and-file-aliases)
remain explicit limits. General process semantics, polling/nonblocking I/O,
signals and cross-process shared offsets require separate design work; this
milestone does not select a successor or promise full POSIX coverage.
