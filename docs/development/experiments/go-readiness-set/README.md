# Go readiness sets and native-task capacity

Qualification on 2026-10-22, nested QEMU/KVM, q35, one or four vCPUs,
`-cpu max,+invtsc`, 2 GiB, OVMF, virtio RNG/network and read-only virtio-SCSI CD.
The guest selected stable KVM clock; invariant TSC was passed through. These
are nested results, not native hardware qualification. No debugger ran during
startup samples. Ordinary temporary C/Go callers and existing launcher/Remote
and GDB tools were used; no test framework or instrumentation was added.

The measured ELF was built from `9e22b2ba60a0`, based on `9d403d56b491`.
Its kernel/arch/ABI executable source equals rebased `a85d114b23cb` on #1113;
`40a8b817` changes only ABI comments. Thus the earlier build identity is not
a different readiness implementation. Userland:
`0775149f505b`; fs: `f3f22b6e07e7`; ports: `f68109ae9768`.
Go fork: `d41ac056ff3b`, based on `73f2b8acaa` (including precise-clock
startup). Bundles explicitly request clock read/precise/sleep. The later
kernel documentation commit changes no executable behavior.

The private sources, exact commands, images, hashes and raw captures remain at
`/shared/present/go-readiness-set/`. Native probes use the matching exported SDK;
Go callers use `GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0`,
`GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off`, `go build -buildvcs=false -trimpath`.
Run the staged `readinessqual` modes `basic`, `capacity`, `budget`, `owner`,
`fanout`, `concurrency` and `threadcap`; Go commands are `goqualify`,
`gomanysources 1000`/`2000` and `gomanyms`. The TCP caller connects only to a
localhost server through QEMU user networking (`10.0.2.2:18081`).

## Readiness and lifetime

| Check | Observation |
| --- | --- |
| Native capacity and fairness | 1,000 distinct ready pipe handles collected through seven-record batches; duplicate-source registrations and one-record batches rotated without omission. A later single write reached all 1,000 initially empty same-source watchers. Only three independently selected changed sources were returned among 1,000 empty distinct readers. |
| Budget | 32,511 disarmed registrations admitted; 16,777,016 bytes charged against 16,777,216. Further admission returned LIMIT without changing prior counts/charges. Removal permitted reuse. This is a measured configuration, not a promised registration capacity. |
| Authority and publication | Wrong rights/foreign owner, mask expansion and stale generations refused. Bad output preserved queued records; last CONTROL closed even with WAIT-only storage retained. A child with native libc's memory grant created its own set without a creation grant. |
| Revocation and endpoint ownership | One terminal CUT result; retarget retained the original source. Closing final pipe/TCP grants completed logical shutdown while observer storage remained. REMOVE joined source cleanup. |
| Concurrent collection | Delayed write, wake before/after park, BUSY on a competing collector, disarm/rearm and REMOVE while WAIT was pending passed. |
| TCP | Buffered peer FIN preserved exact seven-byte payload, TRY EOF and independent surviving write direction. Forty sequential final-grant closures alternated disarmed/READABLE registrations: each returned one ENDPOINT_CLOSED record, then no repeat, followed by REMOVE/reuse. The host saw all 41 connections close. Slirp reported EOF for abort cases; no wire-level RST preservation is claimed. |
| Go files/time regression | Files/directories, supplied streams, buffered EOF, pending read/write close, deadline expiry/extension/clear/advance, timers and configured Europe/Bucharest timezone passed. |
| Distinct Go sources | Two rounds at each of 1,000 and 2,000 independent pipes passed sparse/all-ready identities, close/reuse, live copied stacks plus GC, timers and close while blocked. A separate four-CPU snapshot saw 1,000 kernel registrations (516,408 charged bytes), six native tasks, all parked. |
| Worker/group cleanup | Whole-process termination with 1,000 confirmed blocked readers completed native process and execution-group retirement. A following Go launch succeeded. Separate debugger inspection found closing-account, work, source and timer queues drained. |

Single-CPU basic/capacity/budget, files/time, 1,000-source, native-M and
1,000-source stop checks passed. The fixture deliberately requests four Go Ps
even on that guest. Its first 40-second host client limit was too short for the
many-source/GC workload; an unchanged longer run completed. A debugger sample
saw runtime lock contention, not a stuck readiness collector. No single-CPU
throughput claim is made.

## Native tasks and backing

The raw native caller admitted 255 FS=0 siblings plus the initial task; the next
CREATE returned LIMIT without a returned observer or backing pin. Two replacement
populations woke all 255 waiters, observed actual completion, and released their
guarded stacks. A live-stack release returned BUSY. Whole-group stop at that
population also completed.

The Go caller completed three batches of 160 locked goroutines returning while
locked, then 160 concurrent regular-FILE workers doing 64 positioned 4 KiB reads
each, with independent byte checks. A separate parked control showed **167 actual
native tasks**, all in address waits, for 160 locked goroutines. Parent stop
observed actual group retirement. Go-side return messages alone are not treated
as native completion. The regular-FILE phase establishes correct concurrent I/O,
not 160 simultaneous native calls or a measured M peak for disk activity.

Per additional M, fixed backing remains 64 KiB g0 + 4 KiB TLS + 16 KiB kernel
stack, with two unbacked guards, plus runtime/kernel metadata and page tables.
The per-task service reservation remains 6,544 bytes; the readiness request fits
inside it, rather than increasing every task's reserved service area. Measured
registration/set headers are 512/248 bytes; indices and account storage are also
charged. Go's geometric poll index and reusable descriptor nodes retain their
high-water backing until process exit. Allocator/VM-range limits may refuse
before 256 tasks; no physical reservation is promised.

## Matched hello startup

On the same four-CPU kernel/archive, interleave A/B/A/B/A/B. A is fork
`73f2b8acaa` (the merged precise-clock baseline); B is `d41ac056ff3b`.
Compile identical `package main; func main() { println("Hello from Go on Pyxis") }`
with the same cross-build flags. Both use one P and identical explicit grants.
The existing `golaunchprobe IMAGE ARGV0` measures native LAUNCH through first
supplied-pipe output and actual process retirement, including source capture and
loading. Each batch discards one warmup and keeps eleven samples: 33 per arm.
No debugger, profiler or other qualification workload ran during these samples.

| Nested milliseconds | A p50 / p95 / range | B p50 / p95 / range |
| --- | --- | --- |
| First output | 4.509 / 5.382 / 4.123–5.455 | 4.602 / 5.337 / 4.264–5.779 |
| Process retirement | 4.707 / 5.550 / 4.311–5.637 | 4.829 / 5.506 / 4.425–6.241 |

First-output batch medians were A 4.577/4.441/4.459 ms and B
4.646/4.658/4.531 ms. The roughly 0.093 ms pooled-median increase is within
observed variation, while p95 is slightly lower; both remain well inside the
250 ms nested startup budget. This comparison isolates the Go adapter/sizing
change on the same kernel, not the cost of all intervening kernel changes.
The earlier pre-implementation control used kernel `0ae18f44e59a`, old fork
`4f504d65d673` and reported a 4.750 ms first-output median. It is retained as
provenance, not a matched clock/ABI comparison. No native startup result or
native-budget claim follows from these nested runs.

The matched kernel ELF SHA-256 is
`e2a6f9975acc010b918182e4aafdc6e20f50136dbb6a55e853a0833a07dd1065`;
hello A/B hashes are
`4f5b02d8a29707737cf13ae0ff0e0d9e6bb80677b7f3d8aecb29f4c106cdb90f` /
`2cfce4c7c8765b388de84800133f8e8b75dcbaeed6eed4e2d7ff2e56a1da0943`.

## Other-process progress during fanout

On a fresh four-CPU boot of the same image, one Remote client repeated
`readinessqual fanout` twenty times (1,000 shared watchers plus 1,000 distinct
sources, sparse changes and seven-record batches). A second client began three
unchanged `golaunchprobe` batches after a 200 ms host delay. Both sessions
completed, with all twenty fanout rounds and 33 measured Go launches successful.
First-output p50/p95/range were 10.052/12.310/8.413–12.358 ms;
retirement was 10.398/12.792/8.673–12.817 ms. This measures whole-launch
latency/progress under concurrent enrollment, fanout and cleanup, not isolated
BSP time or a bound on individual interrupt-off tails. The idle matched B p95
above was 5.337 ms. Producer work is a source-local dirty mark; worker fanout
and collection advance at most 64 candidates per service step before yielding.

## Limits

No native run, UDP hook or Go socket implementation is claimed. The existing
TCP connection-pool bound is unchanged; networking task 4 must address it before
claiming Caddy-scale live connections. The set's capacity was exercised with
pipes, not thousands of TCP connections. Console recovery was subsequently checked as recorded below; terminal-session
loss remains source-inspected, without a runtime loss check. Invalid replies were checked at admission, without forcing
mapping/revocation races. The source queues bound worker work in code; no
worst-case interrupt latency guarantee is inferred from these runs.


## Review follow-up: recoverable input and task retirement

The follow-up used kernel `576107e32218` and Go executable source
`95fc079c0402` (`47e01dcb2e77` adds documentation), with the same pinned
userland/fs/ports and four-vCPU nested configuration above. Captures and private
qualification sources are under `/shared/present/go-readiness-set/review/`.
The kernel ELF SHA-256 is
`eca7f967c191ec13613840ae9e1178287fc6de179dec44f6929886c872a380cd`.

QMP keyboard input filled the local console's 4,096-byte queue. Read-only GDB
inspection confirmed overflow at the full queue and one attached, disarmed
registration. Go acknowledged INPUT_LOST, then read a fresh byte through the
same stdin instance. A second boot disabled i8042 and supplied only an xHCI USB
keyboard. Unplug woke the same stdin with UNAVAILABLE; after replug it
acknowledged input loss and read a fresh byte. Neither recovery left a permanent
Go poll error. These are QEMU input-loss/keyboard-unplug checks, not native
keyboard qualification.

The native basic, budget, concurrency and ownership modes passed again, as did
Go files/time, two rounds of 2,000 pipes, three 160-locked-M batches and concurrent
regular-FILE reads. Twelve consecutive rounds of 240 locked goroutines returning
while locked also passed. This churn does not establish that CREATE returned
LIMIT during the run; the LIMIT retry's completion-gated behavior was separately
source-reviewed. A genuine exhausted live-task population still refuses.

The deferred-control queue increased the measured set header from 248 to 264
bytes. The budget run admitted 32,511 registrations, charging 16,777,032 of
16,777,216 bytes; further admission refused and removal permitted reuse.
Registration size remained 512 bytes and service request storage 6,544 bytes.
After both regression and keyboard recovery, closing-account, work, source and
timer queues were empty. UPDATE/REMOVE blocked on a borrowed publication now
park until FINISH or closure, rather than cycling continuously on the BSP.
No new startup timing is inferred from these debugger-assisted correctness runs.
