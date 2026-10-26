# Managed Rust std workers

2026-10-26, gate 3 of the [std milestone](../../wip/rust-std.md).
This qualifies managed workers, TLS cleanup and contended synchronization.
Writable files and directory mutation remain a separate gate.

## Inputs and build

Baseline kernel/SDK source `a5cf48f8`, userland `6cd1cf57`, ports `deff7c1b`,
fs `f3f22b6e`, lwIP `d52ebd20`. The baseline uses a freshly built current-main
kernel/SDK and the unchanged qualified native-program fixture at those exact
userland/ports pins. The selected native programs have no source changes.
No candidate Rust program runs inside the plain-launch timing envelopes.

Rust fork `f08e6f816abf6396aa4547fe11ff0f214a951384` over `a5c9a181`;
libc crate `bdaad80e28d7dc793f034460787c94dea622f588` over `cc65ef47`.
The source remains nightly-2026-10-24 `48f4b055`, with Rust CI LLVM 23.1.4;
final target links use Pyxis Clang/LLD 23.1.3 (`3d69e83`). No compiler-container,
userland runtime, kernel or ABI change. Both images use the same kernel bytes.

The compiler and both host/target libraries rebuild offline on horse from the
published fork. The final library pass explicitly sets `build.full-bootstrap=true`
and builds both targets before export. Exported compiler identity matches the
clean source; the receipt hashes 114 compiler/library/adapter files. All 26 target
rlibs / 506 ELF members contain no embedded `.llvmbc` or `.llvmcmd`.
The known static-target dylib warning and bootstrap change-id notice remain
visible; no other compiler warning or error is hidden.

The isolated libc dependency adds declarations only. C and native Rust layout,
constant and function-type checks agree: pthread identity size/alignment 8/8;
attribute size/alignment 16/8; fields at offsets 0/8. Builds with and without
`extra_traits` pass. Libc remains the sole FS, stack/TLS and final-cleanup owner.

## Runtime qualification

Nested QEMU/KVM q35, host CPU, one/four vCPUs, 2 GiB, OVMF, modern VirtIO GPU,
net/RNG, read-only VirtIO-SCSI ISO; loopback Remote/GDB. No native disk or PXE.
The private ordinary-main exercise requests MEMORY/THREADS MANAGE, real clock
read/precise/sleep and random/read. A separate manifest omits THREADS.

| Exercise | One CPU | Four CPUs |
| --- | --- | --- |
| Owned returned value, distinct Rust IDs/names, scoped shared/mutable borrows | Pass | Pass |
| 192 sequential create/join cycles | Pass | Pass |
| Drop 96 JoinHandles; later collect detached workers and recover all slots | Pass | Pass |
| 63 gated workers plus main; next create EAGAIN; join and reuse | Pass | Pass |
| Eight-way Once, predicate Condvar, 1,600 mutex/RwLock updates | Pass | Pass |
| Rendezvous channel 128 values; four producers/512 ordered messages; disconnect | Pass | Pass |
| Unpark before park; zero-duration park; real timeout allowing spurious wakes | Pass | Pass |
| TLS isolation across eight workers | Pass | Pass |
| Reverse language drain, four key passes, TLS first touched by a key callback | Pass | Pass |
| 128 native keys, next ENOMEM, delete and reuse | Pass | Pass |
| Stack requests 0, 1 MiB+1, 8 MiB; reject 8 MiB+1 and usize::MAX | Pass | Pass |
| Missing THREADS: Builder::spawn returns native EPERM | Pass | Pass |
| Worker panic | Whole process exit 1 | Whole process exit 1 |
| Main returns with live worker | Exit 0, finalizer skipped | Exit 0, finalizer skipped |
| Healthy process after failures | Pass | Pass |

The live-worker result is the existing libc limitation, not a new orderly-exit
promise. Panic-abort is not recoverable through join or catch_unwind. Creation
limits and allocation/grant errors remain real failures; no additional authority
is acquired by linking std.

The TLS ordering exercise records `[20,10,31,40,32,33,34,50]`: reverse original
Rust destructors, first key callback, late TLS initialization, remaining bounded
key callbacks, then its destructor. That final destructor still sees the worker's
Rust name. GDB observes runtime cleanup only afterward, with `cleanup_started`
true and no language destructors left. The initial stack-only main needs no
registered runtime cleanup; the first debugger script incorrectly expected an
extra callback and timed out. Corrected observation passes; its original evidence
is retained rather than treated as a runtime failure.

Read-only GDB also checks native CREATE and worker entry:

| Requested usable stack | Observed usable bytes | Guard |
| --- | --- | --- |
| 0 | 1,048,576 | One unmapped lower page |
| 1,048,577 | 1,052,672 | One unmapped lower page |
| 8,388,608 | 8,388,608 | One unmapped lower page |

FS equals its self-pointer inside separate libc context reservations. A physical
page-table walk observes no present guard leaf and user RW/NX first/last usable
pages. Join/collection retain the existing native retirement contract; the exiting
worker does not free its own stack. No injected guest calls or kernel probes.

## Existing-program regressions

Both CPU counts pass 16 ordinary-main/core/C control commands: args/owned TEXT,
allocation, lexical paths, clocks/entropy, initial TLS, pipe/file binary stdio,
missing grants, sleep overflow, panic/OOM and recovery. Rebuilt core+alloc hello
and unchanged tiny-skia retain checksum `d0dfb9f8f80d6c59`. Rebuilt hexyl matches
all seven pinned Linux output references, including empty/partial/all-byte input,
skips and include mode. These use the newly exported prefix and current SDK;
the generic upstream hexyl I/O limitations remain unchanged.

## Matched native-program controls

These measure existing C launch/exit plus whole Remote/Lua session overhead,
not worker speed or native hardware. After 16 warmup children, three sessions
per cohort each run 1,024 `echo -n` children and exit. Compiler/CI activity has
finished, and no debugger is attached during timed envelopes. Cohort order for
each CPU count is initial baseline, candidate, repeated baseline.

| 1,024-child session (s) | Initial baseline | Candidate | Repeated baseline |
| --- | --- | --- | --- |
| One CPU | 1.318, 1.146, 1.021 | 0.844, 0.846, 0.843 | 0.844, 0.840, 0.861 |
| Four CPUs | 1.115, 1.109, 1.202 | 0.879, 0.927, 0.869 | 0.872, 0.883, 0.927 |

Candidate and repeated-baseline ranges overlap. The early baseline was slower;
its cause is not established and those values are retained. There is no claimed
launch speedup, worker benchmark or native performance result.

PMM observations are identical across all three images/cohorts:

| Allocated 4 KiB frames | One CPU | Four CPUs |
| --- | --- | --- |
| Idle | 10,922 | 10,946 |
| Held Remote shell after echo | 11,313 | 11,337 |
| After session exit | 10,924 | 10,948 |
| Active-session delta | 391 (1.527 MiB) | 391 (1.527 MiB) |

These include kernel/page-table/cache backing, exclude reserved archive backing,
and are not a deduplicated process RSS census. Different qualification archives
reserve different boot bytes. Each observation retains two frames afterward;
this record makes no leak or cache-cause claim.

Raw compiler/export receipts, source/SDK inputs, binary and ISO hashes, private
exercise, Remote completions, GDB and page-table observations remain in
`/shared/present/rust-std-workers/`; libc layout inputs/results are in
`/shared/present/rust-workers-libc-bindings/`.
Worker executable SHA-256:
`b7370a69f8d90bf796e94817a1d6acb78c56f989d19c53898ace9d9b561bca5b`.

## Limits

`yield_now` is the explicitly accepted weak CPU hint, not forced rescheduling or
fairness. `available_parallelism` remains Unsupported without truthful effective
eligibility. Panic aborts the process; join workers before orderly main return.
Network/Command, writable-file/directory adapters, native identities, dynamic TLS,
unwind and a broader crate ecosystem are not delivered by this gate. The file
path readonly decision remains open; no permission behavior changes here.
