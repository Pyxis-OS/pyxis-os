# SoH presentation and synchronization attribution

## Native baseline (2026-10-30)

Vela, diagnostic kernel 9bae87a8 on 7481526a, userland 3a8a398c,
ports 90ed9e60, SoH 9.3.1, Mesa 26.2.5/LLVM 23.1.3 llvmpipe with its default 16
workers. Content/canvas 2880×1768, requested interpolation 40, MSAA/vsync off.
HWP applied 16/16 and XSAVE 0x7 were observed. Each condition has two 256-frame
captures. These are instrumented samples, not uninstrumented frame-rate claims.

| Condition | FPS | Observed thread CPU | SDL pixel copy |
| --- | --- | --- | --- |
| Native gameplay | 27.07–27.42 | 826–836% | 11.77–12.04 ms/frame |
| Advanced 320×240 | 22.62–22.71 | 923–927% | 11.48–11.57 ms/frame |
| Advanced 1920×1080 | 19.50 | 1021% | 11.42–11.44 ms/frame |
| Native pause | 16.28–16.35 | 1073–1077% | 11.6–11.8 ms/frame |

The SDL destination is an ordinary write-back RAM DISPLAY slot, not the GOP
aperture. The original path cleared 20.37 MB and then scaled/converted every pixel,
including coordinate division for matching-size canvases. It neither reads the
framebuffer nor performs cursor composition. The output rate was 1.69–1.78 GB/s;
three-pass logical traffic is not a measured DRAM bandwidth figure. Kernel
composition and the final WC scanout copy happen later, outside this stage.
The Intel flip backend still needs SDL's RAM-slot copy.

Advanced 320×240 workers spent about 105 user and 257–258 kernel CPU-ms/frame
inside raster-scene scopes; native gameplay spent about 261 user and 7 kernel.
These scopes include Mesa's per-tile mutex. This is a synchronization lead,
not proof that all worker CPU is shader work or that a particular syscall is
responsible. Mutex/condition and ADDRESS_WAIT/wake isolation precedes any fix.

Only 18 instrumented threads were accounted; omitted threads are not assumed idle.
Sampling overhead under contention is unqualified. Linux/Fedora comparisons use
LLVM 22, window-system GL and independent process samples, so they compare whole
stacks. Exact pause-page equivalence and loaded native frequencies were not
recorded. Source, commands, detailed stage tables and small evidence are in
`/shared/present/soh-frame-limiter/ALPHA-INVESTIGATION.md`.

## SDL fast path

Both SDL2 and SDL3 now copy matching-size canvases by row without coordinate
division. The RGB 16/8/0 path masks the spare byte to zero as DISPLAY requires;
other shifts retain RGB conversion. A completely overwritten viewport skips
clearing; borders and different-size nearest-neighbour scaling keep their pixels.
Submission, slot rotation and GL ownership/lifetimes are unchanged. This changes
no grants, ABI, worker defaults or compiler pin. Static consumers need relinking.

Validation used parent 63a044fa, userland 36d9bce5 and the published 8bdf136
compiler. All four ordinary/OSMesa SDL2/3 archives build. Both actual GL archives
complete native-sized → scaled/letterboxed → native-sized swaps and teardown in
4-vCPU/4-GiB nested KVM with invariant TSC passed through; renderer is llvmpipe.
1,041 expected-pixel cases per backend include RGB layouts, spare alpha, margins
and untouched row padding. Actual backend disassembly has a vector mask/copy
branch without pixel division for 1:1.

For the extracted pixel function alone, three 16-frame runs at 2880×1768 measured
7.65–7.68 ms/frame before and 1.61–1.67 ms/frame after on that nested guest.
This is not native SoH throughput. Native remeasurement remains pending.
Evidence: `/shared/present/soh-frame-limiter/sdl-present/`.

## Mutex isolation and accepted change (chronium, 2026-10-30)

Vela's silent RAM-only primitive run used kernel 8529730a6 (63a044fa plus the
caller-only CPU snapshot), userland 36d9bce5, 16 usable CPUs and HWP 16/16. Two
repeats of 65,536 aggregate claims at 16 workers measured private mutexes 51–55ns
summed CPU/claim, shared atomic counter 0.816–1.095us, and shared mutex
15.06–15.65us (90.5–90.7% kernel). A separate wall/count build reproduced
shared-mutex elapsed cost 1.139–1.349us/claim. Both processes and Remote retired
normally. Exact raw counts/controls and limits:
`/shared/present/soh-frame-limiter/sync-cost-native-20261030/RESULTS-AND-PROPOSAL.md`.
No independent AC/frequency sample or per-operation hidden libc wait count was
recorded. This confirms contention cost without claiming a particular SoH gain.

Accepted: remove the mutex's separate sleeping metadata guard, with one atomic
ownership/waiter word using existing ADDRESS_WAIT/WAKE. Retain ownership,
recursion, try/timed and destruction checks. No spinning policy, kernel change
or new grant. Caller-owned storage must remain alive through admitted calls,
as before. The implementation uses existing 64-bit waits; size/alignment and
normal/recursive static initializer bytes are unchanged, so no compiler-container
change is needed. Static programs need relinking with the updated libc.

The condition guard remains necessary for its stack-waiter queue and notification
lifetime. Rwlock's reader/writer accounting guard and reader admission policy stay
unchanged. Once retains its initialization/waiter guard; its already-published
completed state now returns through an acquire load without that metadata lock.

QEMU qualification at userland 7d8d45f uses the same diagnostic kernel for both
38eadc43 (before) and the candidate, with 4GiB RAM and invariant TSC passed through.
One- and four-vCPU runs pass runtime, timeouts, once abort/retry, C++ publication
and exit notification, the focused runtime-mutex checks and raw 32/64-bit parking.
All four wall/count and CPU-sampled benchmark processes pass and retire normally.
The corrected runtime-time assertions match existing detach support; this PR
changes no detach behavior.

On four vCPUs (16 workers oversubscribed), the no-payload shared-mutex case
measured 4.77–4.98→2.41–2.49us summed CPU/completed claim and 1.22–1.26→0.61–0.63us
elapsed. The condition case had an after outlier (1.49/6.04us CPU versus 1.50–1.54us
before); no semantic failure occurred, and no native improvement or uniform
condition speedup is claimed. These are two repeats in a nested VM, not native
SoH costs. Evidence is in
`/shared/present/soh-frame-limiter/mutex-native-20261030/`.
Native Vela qualification (2026-10-30) passed all seven semantic/parking modes,
all four before/after probe processes and normal Remote closure. The kernel was
unchanged at 8529730a6; userland 38eadc43 (before) versus 7d8d45f (after), 16 usable CPUs,
HWP applied 16/16 and XSAVE 0x7/832 bytes. AC and loaded frequency were not
independently captured. Two 65,536-claim repeats with 16 workers measured:

| Metric | Before | After |
| --- | --- | --- |
| Shared mutex, summed CPU/claim | 14.82–14.97us | 2.83–3.29us |
| Shared mutex, elapsed/claim (wall-only build) | 1.205–1.267us | 0.205–0.207us |
| Private mutex, 1 worker, CPU/claim | 34.3–34.8ns | 18.6–18.7ns |
| Condition ping-pong, CPU/endpoint | 1.63–1.85us | 1.70–1.80us |

Elapsed/claim is inverse throughput, not an individual lock latency. The native
condition samples overlap, and the large nested outlier did not recur here;
these two repeats do not establish a universal speedup or a thermal series.
New mutex kernel share is still about 90% of its much smaller CPU total. Exact
counts, raw wait controls and typed retirement records are in
`mutex-native-20261030/results-vela-20261030T111033Z/` under the evidence path.
Measurement hooks stay on a never-merged diagnostic branch.


## Native SoH after both fixes (2026-10-30)

The repeat passed all eight captures and three normal foreground retirements;
Remote exited normally with complete drain. It uses diagnostic cdb1e8d58 on
main fb4c1dae, userland 7d8d45f and ports 46b9242b, versus the morning's
9bae87a8/7481526a, userland 3a8a398 and ports 90ed9e60. Private source, save,
fixtures and instrumentation are unchanged. Both use the same effective ordinary
copy configuration, 2880x1800 scanout/2880x1768 content, default 16 Mesa workers,
40fps request, MSAA/vsync off and two 256-frame captures per case. HWP applied
16/16; XSAVE 0x7/832. AC and loaded frequency were not independently captured.

| Case | Morning fps | After SDL + mutex fixes |
| --- | --- | --- |
| Native gameplay | 27.07–27.42 | 36.45–36.58 |
| Native pause | 16.28–16.35 | 22.80–23.04 |
| Advanced 320x240 | 22.62–22.71 | 39.18–39.42 |
| Advanced 1920x1080 | 19.50 | 30.48 |

SDL pixel-copy wall/CPU falls from 11.42–12.04 to 1.88–2.06ms/frame. Raster-work
summed kernel CPU/frame falls from 257–258 to 119–120ms in advanced 240p,
264–265 to 131ms in advanced 1080p, and 270 to 144–150ms in pause. Native gameplay
raster-work kernel CPU instead rises from 6.9–7.0 to 9.5–9.7ms/frame; its observed
thread CPU/frame is 305 before and 309ms after. Do not claim universal CPU savings.
Advanced GUI-end wall time remains 12.3ms at 240p and 18.7–18.8ms at 1080p.
These nested spans and concurrent worker times must not be added as frame time.

Observed-thread CPU utilization increases while more frames complete; advanced
observed CPU/frame nevertheless falls from 408 to 270ms (240p), 524 to 389ms
(1080p), and 659 to 540–547ms (pause). This is sampled partial thread coverage,
not whole-process accounting. Pause1's end-context interpolation count differs
1 versus 2: this is the instantaneous render-count accumulator for 40fps over
30Hz pause, not a changed target setting. Other scene/geometry/target fields match.

The new main includes changes besides these fixes, and two repeats are not a
thermal series. This unattended capture does not qualify optical/audio quality.
Main defaults to Intel flips for 0.0.11; this deliberately flip-disabled repeat
matches the morning copy baseline. A later flip-on run represents normal 0.0.11.
Reports and per-thread/stage detail:
`/shared/present/soh-frame-limiter/NATIVE-AFTER-FIXES-RESULTS.md` and
`NATIVE-AFTER-FIXES-COMPARISON.json`. Raw captures remain private on horse at
`~/.local/share/pyxis-private/soh-profile-20261030/results-vela-after-fixes-20261030/`.


## Native flip-on and FPS-only control (2026-10-30)

All sixteen captures and six normal foreground retirements passed; Remote closed
normally. Initial and final kernel evidence confirms continuous Intel flip
admission, with no reported fencing or timeout. HWP applied 16/16 and XSAVE
0x7/832; AC/core frequency were not independently recorded. Source 51e50e63
keeps the previous fb4c1dae/userland7d8d45f/ports46b9242b closure and effective
configuration except flip=y. Game source, fixtures, viewport, interpolation40
and upstream default workers match the copy repeat. The profiled helper only
adds an accounting-mode label outside measurement.

| Case | Copy, profiled fps | Flip, profiled fps | Flip, FPS-only fps |
| --- | --- | --- | --- |
| Native gameplay | 36.45–36.58 | 36.86–37.53 | 39.45–39.89 |
| Pause | 22.80–23.04 | 23.41–23.63 | 26.26–26.41 |
| Advanced 320x240 | 39.18–39.42 | 39.32–39.41 | 39.94–39.95 |
| Advanced 1920x1080 | 30.48 | 30.20–30.35 | 33.09–33.32 |

Flips show modest differences in these profiled samples, not a large general
throughput improvement. Removing hot observer calls changes observed performance
substantially in pause and 1080p. FPS-only compiles stage/count/scope macros out
of every injected library/game unit and matching helper; frame clock, context
and controller I/O remain. Its missing thread/stage counters are unobserved,
never zero CPU usage. This is a minimally instrumented control, not an entirely
uninstrumented release. The fixed profiled-then-FPS order permits thermal/cache
variation; two repeats do not isolate a precise observer penalty, and the 40fps
cap censors overhead/headroom when reached. Audio/optical quality is unqualified.

The separate accepted original-rate check uses existing interpolation20 to select
scene rates20/30/60, without changing shipped settings. Render FPS above a scene's
logic rate at interpolation40 still does not establish full-speed updates.
Reports: `/shared/present/soh-frame-limiter/NATIVE-FLIP-CONTROL-RESULTS.md` and
`NATIVE-FLIP-CONTROL-COMPARISON.json`. Raw captures remain private on horse at
`~/.local/share/pyxis-private/soh-profile-20261030/results-vela-flip-control-20261030/`.


## Original scene-rate check (2026-10-30)

All twelve captures and two normal foreground retirements passed, with normal
Remote closure and continuous flip admission before/after; no reported fencing
or timeout. Controller f4dceb20 reuses the exact 51e50e63 compiled kernel, games,
libraries and effective flip configuration. Only diagnostic interpolation40->20
and the controller changed; all other settings, save, assets and manifest bytes
are identical. Native content is 2880x1768, Mesa workers unchanged, MSAA/vsync off.
HWP applied 16/16, XSAVE 0x7/832; AC/core frequency were not independently logged.

| Scene | Target Hz | Profiled fps / completed game updates/s | FPS-only fps |
| --- | --- | --- | --- |
| Gameplay | 20 | 19.99 | 19.98–19.99 |
| Pause | 30 | 24.22–24.30 | 26.86–26.93 |
| File select | 60 | 35.46–36.27 | 37.02–37.49 |

Actual target/divisor/mode/source geometry matched each scene. The existing
interpolation20 option yields one render per logical update. Profiled completed
update counts match the rendered rates; FPS-only cadence follows that source
relationship as inference, since it has no update counter. Gameplay is near its
original rate; pause and file select remain below theirs. Complete collection
is not a claim of all targets met or of audio quality. No shipped setting changed.

Profiled pause uses 510–512ms summed observed CPU/frame, of which 203–205ms falls
in the task-accounting kernel bucket. File select uses 343ms, with 16–17ms in that
bucket. Main-thread final-fence waits are about 20.5ms (pause) and 21.4ms (menu);
pause command interpretation takes about 10ms, while game update itself is small.
Concurrent CPU and nested wall spans cannot be added to get frame latency. The
direct attribution repeat below follows this lead before another fix. Current accounting buckets are not exact implementation cost: entry VM
admission precedes the user-to-kernel accounting boundary, and scheduler-loop
work is charged to the next dispatched task (task.c). Keep that limitation when
interpreting all task-derived user/kernel figures above.

Two repeats, fixed profiled-then-FPS ordering and residual FPS-only frame/control
overhead limit attribution; no thermal series or unattended optical/audio proof.
Reports: `/shared/present/soh-frame-limiter/NATIVE-ORIGINAL-RATES-RESULTS.md` and
`NATIVE-ORIGINAL-RATES-COMPARISON.json`. Raw data remain private on horse at
`~/.local/share/pyxis-private/soh-profile-20261030/results-vela-original-rates-20261030/`.


## Direct native VM/wait attribution (2026-10-30)

The approved one-run observer completed all twelve captures, both primitive
controls and two waited foreground completions. Remote exited 0 with complete
drain; continuous Intel flip admission was confirmed before/after, with no
reported fencing or timeout. Source a3298342 is a never-merged diagnostic on
fb4c1dae/userland7d8d45f/ports46b9242b, using the same old FPS-only game/library
objects and compiler8bdf136. Only the small helper was relinked. The same binary
runs counters-off then counters-on, with unchanged original-rate settings/save,
2880x1768 content and default 16 Mesa workers. HWP applied16/16, XSAVE0x7/832;
AC and loaded core frequencies were not independently recorded.

| Scene | Target Hz | Counters off fps | Counters on fps |
| --- | --- | --- | --- |
| Gameplay | 20 | 19.99 | 19.98–19.99 |
| Pause | 30 | 23.85–23.92 | 25.57–25.71 |
| File select | 60 | 36.47–37.19 | 37.65–37.81 |

This fixed-order pair observes +7.35% pause and +2.44% menu FPS, not a negative
observer cost or proof of zero overhead. Gameplay is target-capped. Today's off
pause also differs from the earlier 26.86–26.93fps control; between-run variation
and disabled-hook overhead are not separated. No runtime optimization or shipped
setting changed. No unattended optical/audio quality claim is made.

All six enabled captures read16/16 coherent rows with zero busy rows and zero
incomplete-timing/latency events. Metadata charge is169,424bytes against the
512KiB diagnostic cap. Timing uses qualified ordered TSC, back-to-back pairs
14–18ns, with Bernoulli1/64 sampling and actual per-kind coverage. Private
thread diagnostics do not become production ABI or grants.

| Exclusive phase, share of sampled CPU spans | Pause, two captures | File select, two captures |
| --- | --- | --- |
| Process-VM guard acquisition spin | 78.13–78.29% | 47.3–48.8% |
| Token validation, excluding nested VM guard phases | 10.76–11.01% | 32.2–33.1% |
| Local VM-guard held work | 5.01–5.10% | 5.9–6.1% |
| Bucket + queue + notification sender | 0.89%, both | 3.0–3.1% |

Pause WAKE calls spend93.86–95.17us in VM-guard acquisition on average per
sampled complete WAKE call, across its multiple acquisitions, not per lock.
The two 256-frame pause captures count6,329,230/6,361,263 VM-guard acquisitions,
562,749/565,476 token checks and537,984,747/531,436,171 instrumented VM-range
visits. Range visits cover all callers of range_containing(), not just tokens;
visits/token is only an aggregate proxy. The token walk happens outside the VM
guard, so these results do not show it holding that guard or causing the spin.

VM-guard acquisition is the leading hotspot within the recorded spans, with
token validation second. There is no phase-overlap bug identified in source
review: child spans are exclusive and context-away time is excluded. Held-work
excludes nested queue/notification time and is not full physical lock-hold time.
Small queue/notification shares do not cover dequeue, all interrupt work or IPI
reception. Park-to-wake and wake-to-resume remain elapsed distributions, not CPU;
pause wake-to-resume p50 lies in2–4us and p95 in16–32us histogram buckets.

Endpoints retain31–33 active calls and0–2 sampled latencies. Completed-call
differences include some work from before the start and omit unfinished end
calls; coherent rows are not a globally atomic or complete interval. Every range
visit executes observer bookkeeping, including nonsampled calls. Clock-pair cost
does not bound that overhead. Sampled shares/means are not extrapolated into
exact frame CPU totals. Primitive controls ran alongside the uncaptured initial
menu: semantic qualification and loaded timing, not no-game isolated costs.

Reports: `/shared/present/soh-frame-limiter/NATIVE-WAIT-ATTRIBUTION-RESULTS.md` and
`NATIVE-WAIT-ATTRIBUTION-COMPARISON.json`. Raw logs remain private on horse at
`~/.local/share/pyxis-private/soh-profile-20261030/results-vela-wait-attribution-20261030/`.
The collector used238.76s host elapsed; last capture ended at212.548s target
clock, within the separately approved840s collection/900s visible window.

### Next change proposed — explicit fixed THREAD packet lease

Pending chronium's decision: use one explicit mapping-read lease for the fixed
ADDRESS_WAIT/WAKE packet, acquired after capability resolution and before header
copy. Preserve separate header/body/word/reply checks, rights and error order,
incarnations, atomic word reload, deadlines, CUT/stop/commit points and completion
protocol precedence. WAIT releases after enrollment/bucket unlock and before any
park; WAKE retains it through final checked reply copy, then releases before
outer authority cleanup. All failures release exactly once. A mapping lease
does not freeze sibling-written data.

Successful direct calls would use4 VM guards instead of8/12: syscall admission,
one borrow acquire/release pair and syscall leave. Admission and borrower proofs
remain distinct. Other protocols and non-wait THREAD operations keep their
existing handling. No spin, scheduler, worker or Mesa policy change is proposed.
VM-range indexing remains a separate possible follow-up after remeasurement.
This paragraph authorizes no implementation; the full proposal is in the report.
