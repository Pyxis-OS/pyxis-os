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
after a FILE observes EOF and never fill a short request through repeated reads;
they return bytes a FILE read ahead before any backend transfer.
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
| `fcntl.h` | `open(path, flags, ...)`; O_RDONLY = 0, O_WRONLY = 1, O_CREAT = 0x100, O_TRUNC = 0x200 |
| `unistd.h` | read, write, close; STDIN_FILENO/STDOUT_FILENO/STDERR_FILENO = 0/1/2 |
| `sys/types.h` | ssize_t as signed long; mode_t as unsigned int |
| `limits.h` | SSIZE_MAX as LONG_MAX |
| `stdio.h` | Existing FILE interface and BUFSIZ = 8192 |
| `inttypes.h` | PRId/PRIi/PRIo/PRIu/PRIx/PRIX output macros for fixed-width 8/16/32/64-bit types |

Open accepts read-only access or write-only access with optional create/truncate.
Unknown flags and read-only mutation combinations fail with EINVAL before
lookup or examining the variadic argument. With O_CREAT, open consumes mode_t
and accepts only 0666 as native creation policy; other modes fail with ENOTSUP
before lookup, even for existing files. No mode argument is read otherwise.
This creates no permission system or additional rights; virtio-fs retains its
0644 creation request. Paths follow the existing [capability resolver](paths.md).

Read/write first check descriptor and access validity (EBADF), then reject
counts above SSIZE_MAX (EINVAL). A valid zero-count call returns zero without
touching the buffer or backend. Nonempty calls return one confirmed transfer,
including positive short progress, or -1 with errno. File/pipe zero reads mean
EOF; unexpected console zero progress and nonempty zero writes mean EIO.
Descriptor calls neither inspect nor update FILE indicators.

The [descriptor I/O reference](stdio.md#descriptor-io) and
[native errno mapping](stdio.md#native-error-translation) give the detailed
contract. O_RDWR, O_APPEND, public seek, fdopen, fileno and duplication are absent.

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

The same release primitive is used for rollback of an internal open failure,
while preserving that open failure's errno. A failed rollback release has the
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
