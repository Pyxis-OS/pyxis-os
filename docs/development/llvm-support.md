# LLVM Support on Pyxis

The [native Support fork PR](https://git.internal/PyxisOS/pyxis-llvm/pulls/4)
implements the first [hosted Clang task](../wip/hosted-clang.md#parallel-delivery-tasks).
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
ownership, access time and link-count queries refuse. Clang retains references
through its identity caches, including real-backed virtual entries. Unknown
mtime stays unknown for `__TIMESTAMP__`; dependency-time comparisons and
PCH/module/preamble caches requiring unimplemented validation refuse.

Inputs use copied `MemoryBuffer` reads. Descriptor opens adopt the original held
native FILE, preserving identity across name replacement. Default creation uses
native authority, not Unix modes; explicit restrictive modes, append/CRLF,
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
