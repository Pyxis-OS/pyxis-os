# Caller-CPU DISPLAY VM work

Task 5 of [BSP removal](../../wip/bsp-removal.md), 2026-10-29.
Decisions 12–13 accepted by chronium: fixed display control/publication,
caller-CPU VM preparation, and final frame retirement on its preparation CPU's
existing RAM reaper. Public ABI, rights and whole-process quiescence remain.

## Inputs and baseline

Baseline main `37f0b8a0b11f` includes task 4 and #1587's alias batching.
The published `pyxis-llvm23.1.3-8bdf136` compiler builds ordinary GNU23/O2,
INFO kernels; configuration SHA256 starts `106bf0ae`. Before implementation,
the ordinary four-CPU full 16-command collector passed at that source; one
baseline launch observation was 782.357 ms, pages 17.725 ms. The frozen kernel
SHA256 is `fabab1e8cb35f914163b2c869ed356133d6f2a1b3bb59a3fe68e64e886d733e8`.

The common workload archive is explicitly frozen, not rebuilt at new pins;
SHA256 `6b3a2cbd6780c14d737ac149a442b18ef99557d2df9ecf1659c9ef0f2105a591`.
It contains the log reader already rebuilt for main's source-byte log ABI.
Remote/children allowance is CPU1; Development/network CPU0. QEMU/KVM q35,
4 GiB, four CPUs, host+invtsc, OVMF and ordinary flags are matched. No profiler,
new benchmark framework, kernel test hook or native action is involved.

## Matched nested comparison

After implementation `b81e1fef475a` (ELF
`92d434890194e530fa6e98022632f57b443ab4df1040290dbf111304c489cea5`),
three fresh pairs ran serially A1/B1/A2/B2/A3/B3, without our other guests,
builds or debugger. An unrelated agent's VM was present and is recorded;
shared-host load varied. Every full cohort, command result and Remote FINAL
passed. No sample is excluded. Median (range across three boot values), ms;
IPC entries sum all 100 verified 256-message passes in each boot:

| Workload | Main | Caller-CPU DISPLAY |
| --- | --- | --- |
| 1,024 launches | 1011.449 (874.252–1022.015) | 1058.860 (868.682–1234.419) |
| Private pages | 17.926 (17.905–18.490) | 18.164 (17.993–18.253) |
| CALL64 completion sum | 127.368 (122.429–242.415) | 133.809 (122.797–239.676) |
| CALL4096 completion sum | 152.742 (142.387–188.464) | 144.739 (143.181–145.122) |
| SEND64 completion sum | 92.436 (63.981–97.506) | 64.185 (62.372–66.479) |
| SEND4096 completion sum | 74.353 (70.154–77.957) | 78.190 (70.412–129.211) |

Launch's paired changes are +41.2%, +3.6%, −14.1%; the sign is not consistent.
SEND4096 has one large B observation while the other two overlap A. These
ranges do not establish a speedup or a consistent material regression. The
native comparison remains required; #1588 does not waive a new task-5 cost.
Earlier post-change cohorts overlapped our graphics VM and are functional-only,
not a valid matched performance comparison. Raw outcomes and variation are
retained under `/shared/present/bsp-removal/task5/matched-qemu/`.

## Ordinary graphics and lifetime qualification

Four/one-CPU GTK/X11 VirtIO-GPU checks use existing Mandelbrot, with only the
frozen archive's live configuration changed. Untimed GDB at `0264fc829032`
observed VM work on CPU1 and a second live space on CPU3, publication on CPU0,
two disjoint resize replacements, Escape RELEASE, genuine Ctrl+C owner exit,
reacquisition and empty retirement/quarantine queues afterward. At one CPU the
full acquire/resize/release/exit/reacquire sequence also passed on CPU0.

The final geometry fix at `b81e1fef475a` repeats four-CPU acquire, two successful
resize publications and Escape release, and one-CPU acquire/render/release.
Its two fixed publications observe matching generations, disjoint mappings and
an open geometry gate. This finite final check does not relabel the earlier
full exit/independent-space observations as exact-final-head evidence.

A local disposable SDK fixture supplies hands-free ordinary API coverage. Its
configured space declares normal launch, DISPLAY and RAM-home authority; it
passes only held grants to native leaf children. Two rounds each run six
acquire/submit/same-generation disjoint-replace/release cycles, stale-generation
BUSY checks with usable old slots, a normal child exit retaining a display,
actual EXITED-0 waits and parent ownership recovery. PASS requires four real
child waits, four recovery releases and twelve stale refusals. The same fixture
and archive pass on baseline and fixed candidate. The fixture is qualification
material only, not a new shipped API or timing framework.

Two real implementation defects were found and fixed: notification before
geometry admission reopened left Mandelbrot clipped after BUSY; closing that
admission before every idle resize poll caused spurious ACQUIRE BUSY during
VM-owner contention. Reservation now starts only for a real backend candidate
and reopens immediately after coherent publication, before notifying clients.
Failed runs and corrected host setup/observer attempts are preserved separately.
The genuine unedited resize failure is kept in the glitches gallery handoff.

Evidence: `/shared/present/bsp-removal/task5/graphics/` and
`/shared/present/bsp-removal/task5/fixture-qualification/`. No native claim yet.

## Remaining-consumer proof and limits

Stable shared queries retain their short mapping snapshot guard and each
consumer's frame/resource lease; they gain no unrelated-owner BUSY. NPFS already
uses bounded admission and intrinsic retirement. DMA, BAR/ECAM, scanout and
firmware windows retain explicit boot/device ownership. PCI physical-overlap
selection keeps its owner through claim/programming. #1587 batches that scan
without changing scalar-query ownership. Heap growth keeps its separate arena.

The existing `vm_protect_begin/step` source is byte-identical to the merged JIT
helper (`e3488a494`, function-span SHA256
`ef1a808c83aea237913bc4e8ffa6ee11e0e8d003dd3d1cd644436b06db473741`).
Its [JIT qualification](jit-grant/README.md) covers both permission directions,
wait-incarnation withdrawal, user faults and stop. This task adds no permission
operation or live-root mutation. Display aliases remain borrowed frames in a
reservation, not eager owned backing accepted by that range helper.

Unconfirmed frame retirement retains the entire backing/group/join until
reboot. A process hands its pending cleanup record to that join rather than
parking its sole RAM reaper. The new timeout/join schedule is source-reviewed,
not newly fault-injected. Ordinary runs do not prove every presenter-overlap
schedule, malformed reply-buffer case or partial allocation-failure unwind.
Existing device worker placement and the [shared-VM limits](https://git.internal/PyxisOS/pyxis-os/issues/1514)
remain. Native packet/run results will be recorded separately after the
orchestrator's hands-free vela comparison; no staging has occurred here.
