# Monotonic time and deadline sleep

Monotonic time counts from clock initialization during this boot, not a
calendar date. All CPUs and clock capabilities observe that same epoch. Boot
reads a shared HPET counter discovered through ACPI, started during BSP
architecture initialization before any AP. Before the scheduler starts, every
CPU switches to KVM time when every vCPU registers its time record; otherwise
native TSC qualification decides whether every CPU switches to its TSC or keeps
the HPET. A failed HPET calibration has a bounded recovery path described below.
See [KVM selection](#kvm-selection) and [native TSC selection](#tsc-selection).

The register page is mapped supervisor-only, writable, non-executable and
uncacheable. Clock initialization logs the hardware width, selected direct or
software-extended path, and advertised period. A 64-bit hardware counter retains
the direct high/low/high reads that avoid torn values on implementations that
split MMIO accesses. A 32-bit counter uses one shared atomic tick accumulator.
Every extension attempt acquire-loads that accumulator, reads a fresh low counter
word, adds the unsigned modular delta with an overflow check, and publishes with
compare-and-swap. A failed attempt discards both its snapshot and counter sample.
Readers on other CPUs and interrupting readers use the same accumulator without
waiting for an interrupted reader to release a lock.

Conversion to nanoseconds avoids overflowing an intermediate femtosecond product
and saturates at `UINT64_MAX`. Extension also returns that terminal nanosecond
value if its stored tick total is exhausted; it never computes another modular
delta from a saturated tick value. The epoch is never reset after initialization.
Boot requires a page-aligned memory-mapped HPET with a nonzero advertised period
of at most 100,000,000 fs and a usable sampling interval when extension is needed;
it fails explicitly otherwise.

HPET supplies elapsed time only. Its comparator interrupts and legacy replacement
mode are disabled. Before AP startup the BSP sets the local APIC timer rate once,
and every CPU uses it to target both sleep deadlines and nominal 120 Hz
preemption:
- on Intel, when CPUID `0x15` reports the core crystal, the rate is that crystal
  divided by the timer's divide-by-16, with no measurement;
- otherwise (AMD), one 50 ms measurement against the HPET.

The legacy 8254 PIT is not used; recent Intel platforms such as vela gate it. No
PCI or VirtIO device support is involved.

## Software-extension sampling and support limit

Correct extension requires **strictly less than one full hardware wrap between
successfully incorporated samples**, starting when the clock is enabled. The
wrap interval is `2^32 * period_fs / 10^15` seconds: about 300 seconds for the
ThinkPad's recorded 14.318180 MHz counter. A missing full wrap cannot be detected
or recovered from the low counter word.

The BSP LAPIC timer handler maintains the accumulator before scheduler handling,
after `CONFIG_HPET_MAINTENANCE_TICKS` serviced nominal preemption occasions.
Additional deadline interrupts do not advance this countdown. The
[menuconfig option](../development/configuration.md#select-options) defaults to
120, nominally one second. This countdown is owned only by BSP timer dispatch;
AP timer interrupts do not decrement it. Ordinary clock reads incorporate
samples too. Initialization rejects a nominal maintenance interval of one wrap
or longer, without rounding the comparison or using 128-bit division helpers.
The configured interval must leave room for interrupt delays; nominal validation
alone does not establish the actual sampling bound.

Before BSP interrupts are enabled, explicit samples cover boot phases, PS/2
polling, AP startup waits, initrd mapping/validation, PCI devices, spaces and
initial task launches. This avoids accumulating the time of an entire long boot
without sampling. Individual operations between these samples, runtime execution
with interrupts disabled, firmware stalls and any debugger/VM pause during which
HPET advances must still satisfy the bound. Operation after a violating gap is
unsupported and requires reboot. Suspend/resume and migration are not qualified.
A pause that also stops HPET does not consume the counter's wrap interval.

The ThinkPad T14 Gen 1 AMD used this path natively before TSC selection. On
2026-10-07 the owner kept a native session running for more than 15 minutes,
about three wraps, with the clock holding; see the
[target notes](../targets/t14-gen1-amd/notes.md#native-status). The extension
keeps its maintenance only while the HPET is the clock.

## Clock reads in timer handling

An HPET monotonic read exits under virtualization; other sources still have
ordering and conversion costs. Timer handling takes as few readings as it can:

- **One reading per timer interrupt.** The LAPIC timer handler reads the clock once
  and publishes it to a shared monotonic cache before expiry of timed waits,
  BSP sleeper wakeup and the re-arm. Concurrent older samples cannot lower the cache.
  Expiring against a reading taken moments earlier can only leave a just-due wait for
  the next interrupt, which the re-arm schedules at the minimum count; no deadline is
  treated as passed early.
- **Re-arm only for an earlier target.** Each CPU remembers the absolute time its
  armed countdown targets, cleared when the timer interrupt runs. A re-arm whose
  target is not earlier writes nothing and reads no clock. A later deadline leaves the
  earlier interrupt armed; it finds nothing due and re-arms.
- **Expiry only in timer entry.** Scheduler passes and reschedule IPIs only check
  whether an earlier target needs arming. Address-WAIT expiry still defers bucket
  detachment to the permanent scheduler stack, before the task can run again.

The no-early-expiry argument assumes cross-CPU clock agreement: the shared
maximum can lead a local precise read by any TSC skew, and the boot-only warp
check does not bound later skew.

Readiness and npfs periodic maintenance use cached observations. Readiness,
network and npfs workers park through `task_wait_sleep_until_coarse`, keeping
the initial expiry check consistent with their observation. A stale sample
can delay observation; precise earlier-target arming still ensures progress.
Nominal cache freshness is one serviced preemption period (about 8.3 ms), with
no elapsed-time bound across masked interrupts, firmware stalls or VM/debugger
pauses. Metadata uses cached UTC and permits repeated timestamps.

Public sleeps, address WAIT, operation admission, hardware waits and measurement
brackets remain precise, as does the default `task_deadline_expired`. Nonblocking
TCP admission and an empty connection-maintenance pass take no unused reading.
The [task 1 record](../development/experiments/clock-cache.md) covers this change;
[earlier measurements](../development/experiments/timer-clock-reads/README.md)
and [clock-source debt](https://git.internal/PyxisOS/pyxis-os/issues/1380)
cover the remaining reads.

## TSC selection

When KVM is unavailable or any vCPU fails admission, native selection requires
invariant TSC on every CPU, a startup check only, and all CPUs falling back to
HPET together. This preserves the owner's 2026-10-09 native qualification rule.
No TSC-deadline timer. [Clock precision authority](../interfaces/clocks.md) is
independent of the selected source.

**BSP checks and calibration.** During clock initialization, before APs start
and with interrupts off, the BSP requires:
- a TSC (`CPUID.1:EDX[4]`) that is invariant (`CPUID.80000007:EDX[8]`);
- an ordered read:
  - `RDTSCP; LFENCE` where `LFENCE` serializes: always on Intel, and on AMD
    when `CPUID.80000021:EAX[2]` says so;
  - on bare AMD hardware from family 10h it sets `DE_CFG` (MSR `C001_1029`)
    bit 1 first. That MSR is never touched under a hypervisor;
  - every other CPU, or a CPU without `RDTSCP`, uses `CPUID; RDTSC`.

It then measures the TSC against the HPET for 100 ms. Each end of the interval
pairs one HPET read with the midpoint of the narrowest of eight TSC brackets
around it. The error bound is half of each bracket plus one HPET period, over
the interval. The 100 ms is spent on every boot whose BSP qualifies, including
boots that later fall back.

The wait has a ten-million-poll cap and, when a qualified BSP TSC has a
CPUID-derived frequency, an independent one-second TSC watchdog. The cap bounds
iterations rather than wall time if no frequency is known; register accesses
must themselves complete. A BSP without a qualifying TSC still receives a
bounded HPET-progress check.

On timeout, a complete qualified CPUID `0x15` frequency supplies provisional
BSP boot time, so PS/2 reply deadlines and device reset delays can progress
before AP validation. A checked AP also uses this conversion for bootstrap
timer interrupts while later APs start. The final switch retains that epoch.
AP read-kind and warp checks still apply; an AP never uses unqualified
provisional time. With
no qualified frequency, or if any later AP rejects the TSC, boot fails explicitly
with an unusable-clock diagnostic. This is the owner's accepted exception to
warn-and-fallback (2026-10-15); a timed-out HPET is never advertised as a working
fallback. That timeout recovery adds no independent clock or runtime watchdog.

- **Intel with a complete CPUID `0x15`** (ratio and crystal): the frequency is
  crystal × numerator ÷ denominator. The HPET measurement only cross-checks it,
  and a disagreement above 1000 ppm is logged without changing the choice. A
  failed or uncertain measurement does not reject the TSC.
- **Otherwise** (AMD, or no crystal): the measured frequency is used. A bound
  above 100 ppm, or a frequency below 100 MHz, rejects the TSC.

**Checks on each AP.** Startup brings APs online one at a time. Each AP
repeats the CPU checks, including its own `DE_CFG`, and must support the
BSP's read kind. Once it is online, the AP and the BSP run a 2 ms warp check:
- each repeatedly takes a shared lock, reads its TSC and stores it;
- the check fails if a value is below the last one stored by either CPU.

A finite check cannot prove agreement for the rest of the boot. Without KVM
advertisement, native reads have no shared floor. Failed KVM admission retains
the admission floor on native fallback reads. Neither path has a runtime watchdog.

**Switch.** After the last AP, before the scheduler starts, the BSP switches
once (or retains the provisional BSP conversion after an HPET timeout):
1. it reads the TSC, then HPET nanoseconds;
2. it publishes the pair with a fixed-point multiplier,
   `(10^9 << 32) / frequency`;
3. it release-stores the selection, which readers acquire.

Reading the TSC first makes TSC time start at or after any HPET reading another
CPU takes before it observes the switch, so time never steps back. A reading is
the base nanoseconds plus the scaled TSC delta, from a 128-bit product. It
saturates at `UINT64_MAX`, and a TSC below the base counts as zero elapsed.
After the switch nothing reads the HPET, and its software-extension maintenance
stops. The HPET is therefore not a maintained fallback once the TSC is selected:
a 32-bit counter's accumulator goes stale after one wrap. Any later runtime
switch back, such as a watchdog, would have to restart maintenance and rebase
the epoch, not resume reading it.

**Fallback.** With a progressing HPET, a failed TSC qualification keeps every
CPU on the HPET:
- a missing invariant TSC or ordered read on any CPU;
- a calibration outside its bounds;
- a warp.

One log line reports the outcome, for example:

```text
clock: TSC selected on 4 CPUs, 3187.062 MHz calibrated against HPET (+/-250 ppm), RDTSCP+LFENCE
clock: HPET kept: CPU 0 has no invariant TSC
clock: HPET kept: CPU 0 calibration too uncertain (+/-250 ppm)
```

QEMU guests see an invariant TSC only when the host has one and `-cpu` asks for
`+invtsc`. Without admitted KVM time, nested HPET reads can widen the calibration
brackets past the 100 ppm bound. The
[measurements](../development/experiments/tsc-clock/README.md) record those native
selection paths.

## KVM selection

KVM discovery scans hypervisor signature leaves in 0x100 blocks, including KVM
relocated behind Hyper-V enlightenments. `KVM_FEATURE_CLOCKSOURCE2` admits one
resident, WB kernel-data time
record per vCPU through `MSR_KVM_SYSTEM_TIME_NEW`. BSP and AP registration run
with interrupts disabled before online publication. The physical records remain
reserved until reboot even if one failed admission leaves native fallback in
use. No heap allocation, runtime mapping mutation or legacy KVM MSR is needed.

After every CPU registers a usable record, BSP takes a raw KVM sample followed
by the old clock sample and publishes one common epoch offset. Every
`arch_monotonic_ns` fallback read already joins the shared monotonic floor when
KVM admission is possible,
including reads completing across selection. Selection never resets that floor.
KVM selection stops HPET extension maintenance; the stale HPET is not a runtime
fallback. Native checks and early boot HPET requirements remain intact.

Each precise read masks interrupts to pin the vCPU, locates its record through
the installed GDT, and validates an even, unchanged version around live
conversion fields and an ordered TSC read. GDT identity remains valid in
GS-unsafe NMI/debugger entry and across user roots. Intel with RDTSCP uses
`RDTSCP; LFENCE`, as does AMD with RDTSCP and serializing LFENCE;
the conservative `CPUID; RDTSC` path adds a trailing compiler/hardware serializer
before final version validation. Signed scaling and addition check overflow.
A zero multiplier or saturated result is refused before floor publication: that
read returns the current floor, and a later valid host update can advance time.
Persistent invalid samples cannot advance time; no clock-dependent fatal path
or switch to the stale HPET is attempted. Host conversion updates are consumed
on each read.

CPUID bit 24 licenses the stable flag; its absence is supported. Every usable
precise KVM sample joins the shared atomic maximum, including stable readings, so a flag
transition cannot discard the floor. Equal values are allowed. A nested reader
never waits for its interrupted reader to release a lock. This enforces numeric
cross-CPU monotonicity, not a bound on host clock skew or observation latency.
A host indefinitely holding an odd version prevents progress. Suspend/resume,
migration, a runtime source watchdog and a userspace mapping remain unqualified.

The [task 2 record](../development/experiments/kvm-clock.md) distinguishes
source inspection, nested-QEMU checks and matched package measurements.

## Clock capability

Boot grants init a named `clock` resource. The shell forwards it to foreground
children and sessions when present, like its other explicit grants. The object
holds authority to use the shared clock; it does not own a private epoch or an
asynchronous timer. Closing a handle releases that grant.

[The protocol](../../include/abi/clock.h) uses synchronous tagged requests:

| Request | Right | Result |
| --- | --- | --- |
| `NOW` | `READ + PRECISE` | Monotonic nanoseconds in `clock_reading` |
| `WALL_NOW` | `READ + PRECISE` | Unix UTC seconds and normalized nanoseconds; see [wall clock](wall-clock.md) |
| `COARSE_NOW` | `READ` | Cached monotonic nanoseconds, same epoch/units |
| `WALL_COARSE_NOW` | `READ` | UTC advanced by the cached interrupt observation |
| `SLEEP_UNTIL` | `SLEEP` | Block the caller until an absolute nanosecond deadline; no reply payload |

A deadline already reached succeeds immediately. Sleep never completes before
its deadline, but becoming runnable and executing again can happen later. A
sleep uses the existing task-owned timed-wait record and no allocation, BSP
allocation request, or separate timer object. The capability keeps the clock
object alive while the calling task is blocked, even if a sibling closes the
handle. Sleeps keep only per-task wait records, so siblings sleep independently
and are never refused; a process stop wakes a sleeper with ENDPOINT_CLOSED.

Libpyxis exposes `clock_now`, `clock_sleep_until` and `clock_sleep_for` in
`<clock.h>`. The relative helper reads the clock once and checks addition for
overflow, returning `CALL_LIMIT` instead of wrapping the deadline. It requires
READ|PRECISE|SLEEP. `clock_coarse_now` and `clock_wall_coarse_now` expose explicit
cached observations; denial never downgrades precise calls. Each quality is
monotonic separately; coarse can trail precise. Interrupt samples refresh the
cache nominally at 120 Hz, without an elapsed staleness bound during interrupt
masking, firmware stalls or VM/debugger pauses. `clock_wall_now` and the libc
calendar subset are documented in
[UTC wall clock](wall-clock.md).

## Scheduler timing

Console timeout deadlines and kernel-task sleeps use the same monotonic clock.
Deadline expiry is checked by the sleeping CPU's local timer. Existing
resource-wakeup ordering remains in place: notification before a task finishes
parking cannot make its still-running context available on another CPU.

`kernel_task_sleep_until` takes an absolute deadline and yields if it is already
past. The presentation task targets approximately 60 Hz and drops missed frames
rather than repeatedly rendering to catch up. This is not display refresh
synchronization. Timed console operations retain one deadline across their waits.

After scheduler startup each CPU alone programs its LAPIC countdown with IF=0
for the earlier of its first local deadline and next nominal preemption occasion.
Boot timers remain periodic until then because AP startup polls the BSP countdown.
Preemption keeps its absolute 120 Hz phase even when idle; extra wake interrupts
do not postpone it, and delayed service skips missed occasions without a catch-up
burst. This is not a tickless scheduler.

Timed records are task-owned and sorted under the existing scheduler queue lock,
with no allocation or new timer limit. BSP kernel sleepers use a sorted BSP-owned
list. Earliest lookup is constant time, insertion/cancellation linear, and expiry
visits the due prefix. The handler expires all due records and rearms before EOI
or a context switch, even while idle or in non-preemptible execution. Ordinary
ready queues decide when eligible tasks run.

Each timer pass reads the clock at most once. The timer interrupt's own reading,
taken to advance the preemption phase, also serves expiry, BSP sleeper wakeup
and the rearm. Reschedule and scheduler passes take a reading only when an
earlier target needs arming; they do not scan for expiry. Each CPU records the absolute
time its pending countdown targets, and the timer interrupt clears it. A rearm
whose target is not earlier leaves that countdown in place and reads no clock;
the earlier interrupt finds nothing due and rearms. A reused reading can be a
few microseconds old, so the countdown it programs ends that much after its
target. The [measurements](../development/experiments/timer-clock-reads/README.md)
record the effect.

Insertion is local because blocked syscall continuations cannot migrate. A remote
resource wake or stop removes membership under the queue lock and uses the
existing runnable IPI; it never programs another CPU's timer. Removing a minimum
can leave one harmless earlier interrupt armed. Count conversion rounds upward,
with a positive minimum; the clock confirms expiry before waking a sleeper, so an
early or stale interrupt only recalculates the next arm.

Nanoseconds are the representation, not a wakeup-precision guarantee. Delayed
interrupts and scheduling still delay execution, but missing interrupts no
longer extends a deadline by losing counted ticks. There is no busy-wait sleep.

The clock need not include time while QEMU is paused. The extension support limit
above additionally excludes full-wrap gaps during which the counter advances.
Suspend and resume and VM migration are not qualified with either source.
There is no clock-setting operation, cancellation, HPET alarm, or userspace
direct counter mapping. Native deadline-timer qualification remains in
[sleep wake debt](https://git.internal/PyxisOS/pyxis-os/issues/1379).
Wall-clock precision and clock-source limits
are tracked in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1380).
