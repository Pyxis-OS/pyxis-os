# Fewer clock reads and explicit precision

Assigned to Codex delta (chronium, 2026-10-21). Proposal only; implementation
waits for review. The [baseline](../development/experiments/clock-read-baseline.md)
records nested HPET and native TSC package installation before any clock change.

## Accepted direction

Chronium (2026-10-20): general time comes from the reading the existing nominal
120 Hz timer interrupt already takes; precise time is explicit. Use kvmclock
under KVM, including when its stable-TSC flag is absent, with a cross-CPU
monotonic guard. No `+invtsc` requirement or outer-VM restart. This supersedes
the 2026-10-09 exclusion of kvmclock. Qualified native TSC and HPET fallback stay.
Keep package extraction unchanged so the after-measurement isolates clock work.

Coarse time performs a cached load, without an ordered hardware read. Publish
existing interrupt samples monotonically, before waking waiters; add no tick.
Nominal staleness is one preemption period, about 8.3 ms, while interrupts are
serviced. Interrupt masking, firmware stalls and VM/debugger pauses have no such
elapsed-time bound. Initialize the cache before its first consumer; a cached
sample older than the UTC anchor must yield zero elapsed, never unsigned wrap.

## Three proposed decisions

1. **Precision authority — default: a PRECISE right on the clock object.** Named
   coarse monotonic/UTC operations require READ; precise reading operations
   require READ and PRECISE. Preserve precise libc `CLOCK_MONOTONIC` and
   `CLOCK_REALTIME`, with explicit coarse access. Denial never silently
   downgrades a request. Update declared grants, restricted copies, retained
   libc clocks and bundle selection together, refreshing independently published
   bundles that need precise reads. Alternative: a separately issued precise
   clock capability. Neither choice alone prevents every timing side channel.
2. **Ordering — default: monotonic separately for each quality, same epoch and
   nanosecond units.** A coarse read may trail a preceding precise read. Keep a
   shared monotonic cache of interrupt samples; do not publish on every native
   precise read merely to order mixed-quality calls. Alternative: force coarse
   observations up to every precise observation, adding shared writes.
3. **Deadline construction — default: preserve fresh precise anchors and overflow
   behavior.** Absolute sleep keeps SLEEP authority; existing relative helpers
   additionally need precise-reading authority. Preserve precise public
   past-deadline handling separately from coarse internal parking, including
   existing stop/denial behavior. Coarsen audited observation and parking
   together. Defer a separate SLEEP-only relative operation; it is unnecessary
   for these hot paths.

## Reads to remove

These are source-inspected opportunities, not measured savings. Keep the
precise reader as the default and introduce named coarse access at reviewed sites.

| Path | Proposed change | Correctness boundary |
| --- | --- | --- |
| `task_timer_service`, scheduler passes and reschedule IPIs | Leave expiry scans to the already-armed timer IRQ; retain earlier-target arming and deferred address-wait cancellation | Local insertion still arms an earlier deadline; remote removal may leave one harmless earlier IRQ |
| Readiness service, network worker, npfs periodic maintenance | Reuse a cached observation through each pass and its parking decision | Preserve registrations and readiness winning over timeout; no coarse-not-due / precise-sleep-already-due spin |
| Nonblocking TCP read/send/accept | Skip the unconditional read where only the blocking branch uses it | Keep blocking admission and completion checks precise |
| Empty TCP connection maintenance | Return before reading time when the list is empty | No live connection or deadline is skipped |
| npfs operation timestamps and RAM metadata touch | Use coarse UTC | Metadata permits repeated timestamps and makes no unchanged-content promise; unavailable UTC stays unavailable |

For a selected source satisfying cross-CPU monotonicity, cached time `C` is no
later than a fresh precise reading `P`. Thus `C >= deadline` implies
`P >= deadline`: stale observation can postpone expiry, never make it early.
Retain deadline registration, earlier-target IRQ arming and cache publication
before wakes. The native TSC's finite boot warp check remains a qualification
limit; a monotonic cache cannot prove future agreement between native CPUs.

Keep precise endpoint admission/commit/reply checks, completion admission,
public sleep and `wait_many` deadline admission, hardware waits/cleanup,
stopped-debugger/NMI loops, measurement brackets, lwIP time conversion and input
gesture timestamps. No blanket replacement of `task_deadline_expired`.

## KVM source

Keep hardware details in `arch/x86_64`. Detect the KVM signature and
`KVM_FEATURE_CLOCKSOURCE2`; register resident WB per-vCPU records through
`MSR_KVM_SYSTEM_TIME_NEW` using physical addresses. Storage is bounded and
available before heap/AP startup; the hypervisor may write it throughout its
registered lifetime. Select the source globally only after every reader CPU is
registered, preserving the boot epoch with one common conversion offset.
See the [KVM CPUID](https://docs.kernel.org/virt/kvm/x86/cpuid.html) and
[MSR contracts](https://docs.kernel.org/virt/kvm/x86/msr.html).

Read an even, unchanged version around the live conversion fields and ordered
TSC sample; pin the association with that CPU's record. Check signed shifts,
multiply/add overflow and saturation. CPUID bit 24 licenses interpretation of
the stable flag; its absence is supported. Start with a shared CAS maximum on
all KVM precise readings, including stable ones, so a stable-flag transition
cannot lose the floor; equal values are allowed. Linux v6.12's
[versioned reader](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/arch/x86/include/asm/pvclock.h)
and [monotonic floor](https://github.com/torvalds/linux/blob/adc218676eef25575469234709c2d87185ca223a/arch/x86/kernel/pvclock.c)
are references, not imported code. Consume conversion changes on every validated
snapshot, including ordinary host vCPU movement.

Bootstrap and GS-unsafe NMI/debugger entry must find the correct record without
unconditional GS-relative CPU lookup, allocation or locks. Precise reads retain
the qualified ordered instruction sequence; coarse loads need none. Preserve
native qualification and fallback, including the existing 32-bit HPET support
limit. No runtime switch back to an unmaintained HPET accumulator, source
watchdog, userspace counter mapping, suspend/resume or guest-migration feature.
No compiler-container rebuild is expected.

## Delivery and after-measurement

- [x] Capture three pre-change native and three nested samples; keep the
  installed image and raw records for matched after-runs.
- [ ] Remove redundant expiry/worker reads and add the interrupt cache, with
  precise exceptions and parking handled together. After this task chronium
  can inspect fewer hardware reads without changing the selected source.
- [ ] Add the KVM source, registration and cross-CPU floor. After this task
  chronium can run the existing no-invtsc VM without HPET exits for precise time.
- [ ] Integrate clock rights/SDK/libc and declared grants; repeat measurements.
  Move general-time consumers such as date/UI refresh to explicit coarse reads
  without silently coarsening standard libc clock IDs.
  After this task chronium can choose precision explicitly and compare three
  fresh package extractions on each baseline configuration.

Use the same frozen nested storage fixture/VM, package and three fresh clones,
replacing the reviewed kernel and SDK/runtime consumers together; repeat the
ThinkPad disposable-stick packet with three fresh boots and AC recorded.
Compare phase ranges and separate, labelled HPET MMIO windows. Native and nested
times are not interchangeable; intervening workload changes need a fresh matched
before-run. Validate 1/4-CPU QEMU without invtsc, absent KVM
stable flag, HPET fallback, native TSC, quality-specific monotonicity across CPUs,
precise-right denial, no early timeout, readiness/expiry races and no worker spin.
Use ordinary builds and interactive debugger inspection; no production benchmark
or new tick. Attribute changes by focused PR rather than claiming the baseline
proves how much of extraction is clock cost.
