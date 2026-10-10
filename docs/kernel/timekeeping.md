# Monotonic time and deadline sleep

Monotonic time counts from clock initialization during this boot, not a
calendar date. All CPUs and clock capabilities observe that same epoch. Boot
reads a shared HPET counter discovered through ACPI, started during BSP
architecture initialization before any AP. Before the scheduler starts, every
CPU switches to its TSC if each one qualifies; otherwise all of them keep the
HPET. See [TSC selection](#tsc-selection).

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
mode are disabled. Each local APIC uses its existing PIT channel 2 calibration
to target both sleep deadlines and nominal 120 Hz preemption. No PCI or VirtIO
device support is involved.

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

Before BSP interrupts are enabled, explicit samples cover boot phases, PIT/PS2
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

A monotonic read is an HPET MMIO exit under virtualization, so timer handling
takes as few readings as it can:

- **One reading per timer interrupt.** The LAPIC timer handler reads the clock once
  and passes that value to expiry of timed waits, BSP sleeper wakeup and the re-arm.
  Expiring against a reading taken moments earlier can only leave a just-due wait for
  the next interrupt, which the re-arm schedules at the minimum count; no deadline is
  treated as passed early.
- **Re-arm only for an earlier target.** Each CPU remembers the absolute time its
  armed countdown targets, cleared when the timer interrupt runs. A re-arm whose
  target is not earlier writes nothing and reads no clock. A later deadline leaves the
  earlier interrupt armed; it finds nothing due and re-arms.

Kernel workers that call `task_deadline_expired` repeatedly in one pass, such as ARP
and IPv4, still read the clock per call; with the TSC that is cheap, with the HPET it
is not. The [measurements](../development/experiments/timer-clock-reads/README.md)
cover the nested-QEMU network and audio effect, and the
[clock-source debt](../technical-debt.md#wall-clock-time-and-clock-source-performance)
the remaining costs.

## TSC selection

Accepted by the owner 2026-10-09: invariant TSC on every CPU, no kvmclock and
no TSC-deadline timer; a startup check only, with all CPUs falling back to the
HPET together; the same rule under hypervisors.

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
the interval. A bound above 100 ppm, or a frequency below 100 MHz, rejects the
TSC. The 100 ms is spent on every boot whose BSP qualifies, including boots
that later fall back. CPUID `0x15` is read for comparison only and logged when
complete.

**Checks on each AP.** Startup brings APs online one at a time. Each AP
repeats the CPU checks, including its own `DE_CFG`, and must support the
BSP's read kind. Once it is online, the AP and the BSP run a 2 ms warp check:
- each repeatedly takes a shared lock, reads its TSC and stores it;
- the check fails if a value is below the last one stored by either CPU.

A finite check cannot prove agreement for the rest of the boot. No shared
floor or runtime watchdog follows it.

**Switch.** After the last AP, before the scheduler starts, the BSP switches
once:
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

**Fallback.** Any failure keeps every CPU on the HPET:
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
`+invtsc`. In a nested VM, HPET reads exit through two hypervisors, which widens
the brackets past the 100 ppm bound. The
[measurements](../development/experiments/tsc-clock/README.md) record both
paths.

## Clock capability

Boot grants init a named `clock` resource. The shell forwards it to foreground
children and sessions when present, like its other explicit grants. The object
holds authority to use the shared clock; it does not own a private epoch or an
asynchronous timer. Closing a handle releases that grant.

[The protocol](../../include/abi/clock.h) uses synchronous tagged requests:

| Request | Right | Result |
| --- | --- | --- |
| `NOW` | `READ` | Monotonic nanoseconds in `clock_reading` |
| `WALL_NOW` | `READ` | Unix UTC seconds and normalized nanoseconds; see [wall clock](wall-clock.md) |
| `SLEEP_UNTIL` | `SLEEP` | Block the caller until an absolute nanosecond deadline; no reply payload |

A deadline already reached succeeds immediately. Sleep never completes before
its deadline, but becoming runnable and executing again can happen later. A
sleep uses the existing task-owned timed-wait record and no allocation, BSP
allocation request, or separate timer object. The capability keeps the clock
object alive while the sole user task is blocked.

Libpyxis exposes `clock_now`, `clock_sleep_until` and `clock_sleep_for` in
`<clock.h>`. The relative helper reads the clock once and checks addition for
overflow, returning `CALL_LIMIT` instead of wrapping the deadline. It requires
both rights. `clock_wall_now` and the libc calendar subset are documented in
[UTC wall clock](wall-clock.md).

## Scheduler timing

Console timeout deadlines and kernel-task sleeps use the same monotonic clock.
Deadline expiry is checked by the sleeping CPU's scheduler and local timer. Existing
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
and the rearm. Reschedule and scheduler passes take a reading only when a
deadline list is non-empty or a rearm is needed. Each CPU records the absolute
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
[sleep wake debt](../technical-debt.md#sleep-wake-granularity).
Wall-clock precision and clock-source limits
are tracked in [technical debt](../technical-debt.md#wall-clock-time-and-clock-source-performance).
