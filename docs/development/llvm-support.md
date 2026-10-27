# LLVM Support on Pyxis

The [native Support fork PR](https://git.internal/PyxisOS/pyxis-llvm/pulls/4)
implements the first [hosted Clang task](hosted-tools.md#delivery).
The installed compiler and SDK still use `49e2c1a1518b`; adopting this backend
requires the owner's matching container publication. This is platform groundwork,
not a runnable native Clang or LLD. See the
[build and QEMU record](experiments/hosted-clang-support.md).

## Paths and inputs

`LLVM_ON_PYXIS` selects native Support independently of Unix. Scheme roots and
the shared libc working-path context drive lookup; a leading Unix slash is
invalid. Backslashes are ordinary characters. Lexical dot removal cannot bypass
a missing component; aliases remain distinct. Proved `real_path` uses libc's
held-object comparison, not descriptive cwd text. Executable/resource discovery
needs selected native roots; `getMainExecutable` supplies no guessed name.

Status retains a FILE or directory reference, with independent domain/object and
mtime validity. Regular-file size must be available. Permission bits are unknown;
ownership, access time and link-count metadata are unavailable. Clang retains
references through its identity caches, including real-backed virtual entries. Unknown
mtime stays unknown for `__TIMESTAMP__`; dependency-time comparisons and
PCH/module/preamble caches requiring unimplemented validation refuse.

Owner decision, 2026-10-11: under `LLVM_ON_PYXIS`, unconditional metadata
accessors are deleted, so unaudited consumers fail compilation instead of
aborting when an input lacks metadata. Use `tryGetLastModificationTime`,
`tryGetUniqueID`, `tryGetLastAccessedTime` and `tryGetLinkCount`, returning
`std::optional`; identity-domain/ownership accessors and status comparisons use
the same rule (`tryEquivalent` distinguishes unknown from false). VFS status
preserves validity, and Clang's FileEntry/ref wrappers expose optional time and
identity too. FileManager checks identities before caching; `__TIMESTAMP__`
retains its unknown-time spelling. Metadata-dependent Support operations report
unsupported through their error channels. A checked virtual-ancestor operation
with no error-returning API explicitly refuses unavailable identity; it does not
fabricate a directory. Task 4's full build must adapt each remaining consumer
without replacing unknown values with zero or unchecked optional dereferences.

Inputs use copied `MemoryBuffer` reads. Descriptor opens adopt the original held
native FILE, preserving identity across name replacement. Default creation uses
native authority, not Unix modes; explicit restrictive modes, CRLF,
links, advisory locks and attribute mutations refuse. The legacy named/descriptor
`ToolOutputFile` wrapper refuses before taking ownership or changing a name;
optional thin-link/DWO output needs the staged backend before use. Native directory defaults
are unspecified permissions. Allocation failure after a generic exclusive file
create can leave its empty name; removing it requires separately held REMOVE
authority. Strong staged output below does own cleanup authority.

## Output and memory

`FileOutputBuffer`, on-disk virtual output and encoded output use exclusive random
sibling temps, with the native parent retained from staging. Parent admission
requires LOOKUP/CREATE/REMOVE/WRITE_FILES, valid identity and successful directory
sync. Positional writes are checked; any failed write poisons commit. Commit
re-resolves and compares the destination parent, syncs the FILE, replaces within
the held parent, then syncs the directory. LLD preserves the old output until
replacement. Executable output intent grants no extra authority or Unix mode.

Each output currently costs two directory syncs (admission in `TempFile::create`
and post-rename in `keep`) plus one FILE sync. The ABI has no backend durability
capability query; rights and metadata do not prove sync support. Keep the
admission probe. Task 5 must measure its cost separately on the installed npfs
pool, including per-object-file latency; the recorded RAM costs do not establish
npfs or HOST costs.

Before publication, failure discards the owned temp and preserves the old name.
An untrustworthy rename acknowledgement retires owners without unlink/retry;
successful rename followed by failed sync reports published-but-uncertain output,
without claiming rollback. Ordinary discard closes each owner once. Fatal faults
can leave temps. Identity is not a mutation lease or a name-bound compare-and-swap;
concurrent edits of the destination remain outside this contract.

Memory is eager private RW/NX, with actual page-rounded backing and a separately
owned memory-service grant. Zero-byte requests allocate nothing; a huge-page hint
does not require huge pages. Shared/write-through file mappings, protection
changes, executable memory and proximity requests refuse. An attempted release
retires its descriptor even on an uncertain reply, avoiding address-based retry;
backing left by a refusal remains owned by the process until exit.

Each live mapped block also owns one copied MEMORY handle; zero-byte requests
own none. Task 5's handle budget must include these alongside retained input and
directory references, rather than counting only bytes of mapped backing.

## Launch and faults

`PyxisLaunchOptions` selects the launcher, roots, resources and cwd authority per
call. Borrowed selections remain live through publication; cwd/environment are
copied snapshots. Explicit environment replaces the snapshot. No ambient PATH
interpretation, `/proc` discovery or implicit resource enumeration occurs.
Inherited live stdout/stderr carry diagnostics; stdin state and FILE cursors are
not transported. Redirects, detach, timeout, affinity, resource-limit and process
statistics requests refuse before publication. Capture bounds precede copies of
caller-controlled strings; launcher admission remains authoritative.

`ProcessInfo` owns a completion observer, not a PID. Moves transfer ownership;
observed completion closes it once. `ProcessCompletion` preserves the native
kind and signed exit status; callers must use the kind to distinguish a negative
exit from legacy error returns. A failed observation retains its owner; unknown
launch publication is reported without retry. Child resource grants remain
explicitly selected and attenuated.

Native faults terminate. Integrated cc1 uses ordinary execution without signal
crash recovery; reusable recovering `lldMain` refuses. Orderly error cleanup uses
retained native parents. Signal handlers, crash helpers, stack tracing, sockets,
watchdogs and unsupported process statistics have precise refusals. LLVM workers
remain off, with no fabricated locks or serial-only compiler source adaptation.

## Remaining delivery gates

Host Support builds pass. The target backend units compile and a local helper
links their reached closure against real SDK libraries; the full target archive
still needs C++ streams/wide/random facilities, real synchronization/TLS and the
reached libc names recorded in the milestone. Compiler/linker integration waits
for threads gates 5–8. Retained-handle, memory and stack budgets need actual native
compiler artifacts; no compiler admission or performance claim follows here.

## Native append

Support `OF_Append` uses libc's native-FILE-only open route and FILE_APPEND.
LLVM's Unix compatibility rule applies: append selects create-or-open without
truncation, including when the supplied disposition was CreateAlways/CreateNew.
Write access is required; READ-only append, CRLF and other unsupported mode flags
still refuse before effects. HOST append is unsupported. Each native mutation
is atomic only up to 4,080 bytes; larger raw-stream writes can interleave between
operations. `raw_fd_ostream::pwrite` uses libc
positioned writes, preserving both its stream position and the shared descriptor
cursor even when APPEND is selected; a seek/write/seek emulation would append
instead. Append adds no durability or append-only authority.

The staged VirtualOutput backend still refuses append: it publishes a temporary
by replacement. `AtomicLineLogger` also remains unavailable, with its existing
warning rather than an open: its public API promises an arbitrary-length atomic
line, which bounded native append does not provide. PID-based coverage output
retains its independent refusal. No new per-record locking or native PID is
invented to enable those features.
