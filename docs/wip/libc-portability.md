# Standard C libc over the native ABI

Status: tasks 1 and 2 complete: the source probe, descriptor ownership and stdio
integration are implemented and validated. Public descriptor I/O remains task 3
and has not started. The intended result is a small libc
descriptor layer exercised by an upstream cksum port, with tee as a conditional
second consumer. This document does not authorize the whole compatibility backlog.

## Intended result

Pyxis should provide a standard C library implemented using its own OS
primitives and capability ABI. Simple C programs should eventually build with
minimal source changes; ordinary allocation, formatting, file I/O and other
library calls should not require each port to learn native object protocols.
Grow the existing libc toward that goal, with correct behavior and documented
gaps rather than successful stubs or repeated application-specific workarounds.
This is a direction toward conformance, not a claim that today's libc is complete.

The application-facing library contract and the kernel interface are separate
decisions. Familiar C APIs can map to native resources without prescribing
syscall names, a global Unix filesystem namespace or a POSIX kernel architecture.
Library code runs with the caller's grants; an adapter cannot manufacture missing
authority. Native libpyxis interfaces remain available where applications need
explicit capability operations.

ISO C library coverage and selected compatibility APIs are related but distinct.
Descriptor functions such as open/read/write/close are not ISO C requirements,
but many otherwise small C programs use them. Support useful subsets as actual
ports require them, without committing to full POSIX compatibility, Unix process
semantics or binary compatibility with another OS.

## Boundary for application patches

Prefer implementing conventional library behavior over patching each application
to use native APIs. Ordinary file reading, writing, seeking and allocation should
work through the application's existing library calls. Missing libc support is
a shared compatibility gap to address, not a reason to permanently rewrite those
operations in every port.

Pyxis-specific application changes are appropriate for platform integration and
intentional features. For example, teaching TCC's include-path handling to accept
URI schemes for future `http://` imports is useful integration. Replacing its
ordinary file reads solely because libc lacks the expected interface or semantics
is a workaround. Keep URI resolution and resource access in shared libraries or
providers where possible; a compiler should not need its own HTTP client.

When a bounded milestone needs a temporary workaround, record the missing library
or OS support and the condition for removing the patch as technical debt. This
direction does not require retroactively rewriting existing ports in this PR or
silently expanding the current implementation task.

## Agreed descriptor and FILE constraints

Task 2 implements the private ownership and stdio machinery below. Public
descriptor calls, including the `close(1)` example, remain task-3 behavior.
Libc owns process-local descriptor entries over native file, console and pipe
capabilities. A descriptor number is neither a kernel handle nor authority.

- A descriptor and its associated FILE use one open state, including the native
  handle, access mode and file cursor. The FILE association adds no owning native
  reference. Closing the descriptor releases that handle even while the FILE
  wrapper still exists; it cannot keep a pipe writer or reader alive.
- Closing an entry permanently invalidates its existing FILE association.
  For example, after `close(1)`, nonempty output through the old stdout fails
  with EBADF. Reusing descriptor 1 never reconnects that stdout to the new
  resource. Later cleanup of the stale FILE must not close the new descriptor.
  The entry's non-owning FILE back-pointer clears the wrapper's descriptor
  association before the slot can be reused.
- `fclose` releases the associated live descriptor and FILE metadata exactly
  once. Normal exit also releases descriptors without FILE wrappers. `_Exit`
  and fatal faults still rely on kernel process-resource reclamation. No hidden
  startup or adapter copy may delay peer closure.
- Standard entries use static storage and adopt the exclusive startup handles
  before heap initialization. Later descriptor storage may grow on the heap.
  Missing startup streams remain unavailable to their FILE wrappers, with EBADF
  on nonempty I/O and no fallback authority.
- Public read-only open does not remove writable stdio. Existing `fopen` r/w/a
  modes, their update forms, creation/truncation, append, seeking and cleanup
  continue through the same internal open state and backend machinery. Do not
  maintain a second ownership/cursor system for APIs arriving in different tasks.
  Preserve allocation/authority checks before truncation and the documented
  non-atomic append limitation.
- FILE EOF/error indicators remain FILE state. Current unbuffered transfers,
  `fread` element/fill semantics and `fread_some` prompt progress remain intact.
  Descriptor reads do not inherit a sticky FILE EOF indicator or fill a request
  by repeatedly reading; they return available progress. Descriptor writes
  report actual progress, including successful short writes.
- Supported operations, selected access mode and granted native rights are
  separate. A readable/writable backend does not confer missing authority.
  Adopted handles retain their rights; path opens resolve only through existing
  grants. Distinguish invalid/closed entries, wrong access modes, native denials
  and other backend failures from successful short transfers. Reuse the native
  errno translation; do not fabricate rights or report failure as EOF.

Public fdopen, fileno and duplication are deferred. Internal FILE support uses
one ownership/position model. Cross-process shared offsets and
inheritance remain separate decisions. No kernel descriptor ABI is proposed.

### Task 2 implementation and validation

Userland's private `libc/descriptor.c` owns native handles, backend kinds, access
modes, append policy and cursors directly in entries. The lowest free slot is
reserved for an open, including closed or absent standard slots. Descriptors 0–2
start in static storage before heap initialization; growth copies the table to
heap storage. FILE stores a number, never a pointer into the relocatable table,
and each entry has at most one non-owning FILE back-pointer. Closing clears the
association before releasing the entry. Shared open-state allocation and
duplication are not implemented.

Fopen allocates its FILE and reserves descriptor capacity before opening or
truncating. Path workspace allocation and authority resolution also precede
truncation. A successful truncate is followed by allocation-free publication;
failure releases reservations and attempts to release any opened handle under
the close policy below. Existing r/w/a modes, their writable update forms, seeks
and non-atomic append use this same private state.
Backend transfers report one result without touching FILE indicators; stdio
alone owns EOF/error updates and its fread/fwrite loops.

Standard entries adopt the existing native grants without copying or changing
their rights. `startup_stream()` remains an immutable borrowed snapshot: it
retains no reference, becomes stale when the owning descriptor closes, and must
then neither be used nor forwarded to a child. Number reuse never refreshes it.
The release and exit implementation follows the close-failure contract below.
The supported behavior is described in [stdio](../stdio.md).

Validation used the installed toolchain with `make -j16 image`, then interactive
QEMU q35/KVM boots with 256 MiB and one/four CPUs in the development VM. Both
boots reached the shell and ran a `cat | head` pipeline. The four-CPU boot used
virtio-fs and virtio-rng; Kilo saved `host://hello.c`, guest TCC compiled it, and
the resulting executable printed its message. Additional manual checks covered
independent stdout/stderr file redirection, missing input, rejection of a TCC
output open in read-only `app://`, and early pipe closure reporting EPIPE to cat.
A full `cat app://tcc.pxe | cat > host://tcc-copy.pxe` copy matched the source
SHA-256 on the host.

GDB observed the table grow from three static entries to six heap entries and
reuse descriptor 3 after close during TCC compilation. A successful close left
the entry free and its live FILE association at -1, preserving the prior errno.
Head's stdin close also invalidated its FILE while leaving the borrowed startup
snapshot unchanged. No debugger calls or fault injection were used. Absent
startup bindings, allocation-failure unwinding and exceptional CLOSE responses
were inspected in code, not forced at runtime. Public descriptor functions and
headers remain unimplemented; their signed-count checks and end-to-end
`close(1)` checks belong to task 3.

## Pinned source probe

Investigated sbase revision `c546c3a5724c81cee9a11d816a38ccdf17472129`
(2026-05-25), from `https://git.suckless.org/sbase`. This is the selected source
revision for this investigation and subsequent consumer work; no recipe is
installed yet. Keep its MIT LICENSE, contributor notices and individual source
notices, including arg.h, when packaging it in ports.

| Consumer | Source/helper closure | Gaps against the current SDK |
| --- | --- | --- |
| cksum | cksum.c, libutil/eprintf.c, libutil/fshut.c; arg.h and util.h | fcntl.h, unistd.h, ssize_t, open/read/close, O_RDONLY, inttypes.h/PRIu32 and stdio BUFSIZ |
| tee | tee.c, libutil/eprintf.c, libutil/ealloc.c, libutil/writeall.c; arg.h and util.h | Descriptor headers/types, open/read/write, writable create/truncate/append flags and creation mode; BUFSIZ; signal.h, signal and SIGINT handling for -i |

The upstream Makefile builds all of libutil and libutf even for these commands.
Neither consumer needs that full library set. The util.h umbrella includes
sys/types.h and regex.h and declares unrelated mode_t/off_t/regex_t APIs;
compat.h also defines a hostname-limit fallback unused by these consumers.
A focused ports build and narrowed private helper header can avoid these unused
dependencies without adding speculative libc declarations. The actual public
ssize_t declaration is still required. Ealloc's complete object also references
malloc/realloc/strdup/strndup and the matching diagnostics; these already resolve
in the SDK. Reallocarray and UTF helpers are not in either selected closure.

### Measured build results

Built the SDK with `make -j16 sdk` using the existing Pyxis GCC 16.2.0 and
binutils 2.47.20260726. Inputs were Pyxis
`54873691f7ee0ee00115e77c84b3153d102cfa5b` and userland
`1263b5c5081239deb1d9831462a07032adb96d49`. The SDK manifest reports the parent
as modified because its submodules were advanced to remote merge commits;
all three submodule trees match the committed pins, and userland is clean.

Unmodified command compilation stops at missing fcntl.h; diagnostic/allocation
helpers stop at sys/types.h through util.h, and writeall stops at unistd.h.
To inspect the remaining closure, a separate scratch copy narrowed util.h to
its existing selected-helper declarations, supplied declaration-only missing
headers and defined BUFSIZ as 8192. No function bodies or SDK changes were added.
The scratch inttypes.h supplied stdint.h and PRIu32 as "u", matching the SDK's
unsigned-int uint32_t. Scratch flag/signal values were parsing aids only, not
proposed ABI constants or support for those operations.

With those explicitly artificial declarations, both commands and all selected
helpers compile under the SDK's GNU C23 freestanding flags. Existing source has
signedness warnings. Linking against only the target SDK leaves:

- cksum: unresolved open, read and close.
- tee: unresolved open, read, write and signal.

The diagnostic, formatting, shutdown and allocation dependencies resolve. This
is evidence of the remaining symbol closure, not successful application builds
or runtime validation. No executable stubs, host libc, tests or boot automation
were introduced. Neither command was booted.

The [exact reproduction recipe](libc-probe/README.md) includes the original
compile/link flags, source list and [scratch patch](libc-probe/scratch.patch).
The patch preserves every replacement header and the reduced util.h used by
the isolated probe. It applies only to a disposable copy of the pinned sbase
source, never to the SDK. Neither it nor the commands are test infrastructure
or an installable compatibility layer.

## Accepted initial public slice

The initial slice below is accepted for tasks 2 and 3. Task 2 implements the
private representation and invalidation mechanism; the public API remains task
3. The implemented close-failure contract is specified immediately afterward.

- Add `int open(const char *path, int flags, ...)`, `int close(int fd)`,
  `ssize_t read(int fd, void *buffer, size_t count)` and
  `ssize_t write(int fd, const void *buffer, size_t count)`.
  Use signed long for ssize_t on the current LP64 target, declared once through
  sys/types.h and included by unistd.h. Supply STDIN_FILENO/STDOUT_FILENO/
  STDERR_FILENO as 0/1/2. No public seek call is required by this source closure.
- Initially accept only O_RDONLY (value 0) in public open; reject other flag
  values with EINVAL before path lookup. This does not restrict internal fopen
  modes. Writable public opens and mode_t/permission-mode policy remain part
  of the later tee decision.
- Allocate the lowest free descriptor, including closed or initially absent
  standard slots. FILE association invalidation still applies when those slots
  are reused. Grow storage as needed within int descriptor-number limits;
  report ENOMEM for allocation failure and a new EMFILE for number exhaustion.
- Invalid descriptors and wrong access modes return -1/EBADF, native denied
  authority remains EACCES, unsupported native operations remain ENOTSUP, and
  other backend failures use libc_call_errno. These distinct causes need not
  all have distinct errno numbers. A successful short transfer returns its
  positive byte count, without inventing an error or completing the remainder.
- Check descriptor/access validity even for count zero, then return zero
  without backend I/O. Reject counts above the signed return type's maximum
  with EINVAL. Nonempty file/pipe zero reads mean EOF; the console's unexpected
  zero progress remains EIO. A nonempty zero write remains EIO. Closing a
  descriptor invalidates its association even if native release reports failure.
  See the close-failure contract below.
- Supply BUFSIZ (8192) and the fixed-width output-format macros in
  inttypes.h needed to use existing stdint.h types without consumer-local
  format workarounds. The probe requires PRIu32; broader integer conversions,
  scanning and unrelated inttypes functions are not prerequisites.

### Close failure and cleanup

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
it cannot provide this translation. No new kernel syscall or public libpyxis
helper is required for this slice.

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

### Tee checkpoint after cksum

Record the gaps now and revisit scope after cksum works. No choice has been
made between adding writable opens, restricting options or deferring tee.

The default command needs O_WRONLY|O_CREAT|O_TRUNC and a 0666 creation-mode
argument. Native capability grants do not implement Unix permission bits;
handling that argument must be agreed. `-a` adds O_APPEND, whose conventional
atomicity is not supplied by stdio's separate SIZE/WRITE sequence. `-i` invokes
signal(SIGINT, SIG_IGN); no-op signal handling is not support.

The writeall helper already loops over positive short writes. It treats zero
as a return, so the nonempty zero-write error rule matters. Tee marks
failed outputs inactive but does not close their descriptors, and keeps reading
input even after every output has failed. It relies on exit for descriptor
cleanup. Revisit whether that lifetime and downstream-closure behavior is
acceptable as part of the consumer review; do not silently change it now.

## Milestone boundary

The completed [shell streams and pipelines](../shell-streams.md) provide native
pipes and dedicated startup streams. Preserve their ownership and closure
behavior while making conventional descriptor I/O available in libc. Keep
fread_some as a useful extension; ports should not need to replace read with it.

Use a small upstream utility as the acceptance consumer rather than writing a
new Pyxis command with the same name. The pinned source above provides
[sbase cksum](https://git.suckless.org/sbase/file/cksum.c.html): it uses open,
read and close for input, with stdio for output. Probe its shared helpers and
headers too; the command source alone is not the dependency list. Pin the
selected revision and preserve its license through the normal ports workflow.

[sbase tee](https://git.suckless.org/sbase/file/tee.c.html) is the conditional
second consumer, exercising descriptor output and file creation. Its append and
signal options introduce separate policy questions. Do not expand this milestone
into signals or promise atomic append merely to claim a complete tee port.

This is not a full ISO C or POSIX conformance milestone, a libc replacement, or
a kernel descriptor ABI. Fork/exec, descriptor inheritance across processes,
dup/dup2, polling, nonblocking I/O, general signals, directory APIs and buffered
stdio remain outside the initial slice unless a separately agreed prerequisite
requires one. No speculative stubs or retroactive rewrite of every existing port.

## Focused tasks

- [x] **1. Pin and probe the consumers; settle the descriptor contract.** The
  source/helper inventory, accepted initial slice and close-failure policy are
  recorded above; exact commands and scratch patch are preserved in the
  [probe recipe](libc-probe/README.md). Tee's measured gaps are recorded for a
  scope decision after cksum. No descriptor implementation is part of this task.
- [x] **2. Add descriptor ownership and standard-stream integration.** Private
  entries own native handles and cursors; FILE associations are non-owning and
  explicitly invalidated on close. Static startup storage, heap growth,
  reservation before truncation and the close/exit policy are implemented while
  retaining writable stdio and native launch grants. Build, interactive guest
  and debugger validation is recorded above. Public descriptor APIs and
  fdopen/fileno remain deferred.
- [ ] **3. Add the conventional I/O slice.** Implement open/read/write/close and
  the headers/types/flags selected in task 1 over native file, console and pipe
  protocols. Read returns available progress without trying to fill the buffer;
  write reports actual progress, including short writes. Reuse capability path
  resolution and shared error handling rather than duplicating them in ports.
  Public open is read-only in this slice; preserve internal writable fopen
  and its existing seeks. Public writable-open flags, creation-mode policy and
  public seeking are deferred; revisit tee requirements after cksum.
- [ ] **4. Port cksum as the first final consumer.** Build and package the pinned
  utility with its ordinary I/O calls intact. Keep adaptations to build/platform
  integration and agreed library gaps. Check named files, stdin, empty input,
  missing files, redirection and pipeline use on ordinary QEMU boots; compare
  checksum and byte count with a host implementation on identical bytes.
- [ ] **5. Port tee if the agreed scope fits.** Exercise stdin to stdout and named
  output files, short transfers, read-only grants and a downstream reader that
  closes early. Resolve append and interrupt-option behavior before coding: the
  existing separate SIZE/WRITE append is not atomic, and no-op signal handling
  is not support. Any restricted port needs explicit agreement and documented
  limitations; otherwise record the reason for deferring this optional consumer.
- [ ] **6. Complete the handoff.** Document the supported libc contract and port
  adaptations, carry missing semantics into technical debt, and move this WIP
  document into docs as an implementation reference. Record tee as implemented
  or explicitly deferred, not an unresolved requirement for completion.

Tasks are intended as focused PRs; split a task further if review warrants it.
Libc belongs in userland, recipes in ports, and matching SDK/ABI export and pins
in Pyxis. A missing native primitive is a decision checkpoint, not permission to
reshape the kernel. Validate code through normal builds and manual boots/debugger
inspection without adding test infrastructure.

The final manual workflow should include commands such as these once packaged:

```sh
cksum host://hello.c
cat host://hello.c | cksum
cksum < host://hello.c > home://checksum.txt
cat host://hello.c | tee home://copy.c | cksum
```

The last command depends on accepting the tee slice. Existing
[stdio behavior](../stdio.md), [append limitations](../technical-debt.md#non-atomic-stdio-append)
and [directory API gaps](../technical-debt.md#directory-apis-in-libpyxis) remain
explicit starting constraints, not promises this milestone resolves them all.
