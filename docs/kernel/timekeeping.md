# Monotonic time and deadline sleep

The kernel uses a shared HPET counter discovered through ACPI. It starts
the counter during BSP architecture initialization, before starting APs. Its
epoch is clock initialization during this boot, not a calendar date. All CPUs
and clock capabilities observe that same epoch.

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

The ThinkPad T14 Gen 1 AMD uses this path natively. On 2026-10-07 the owner
kept a native session running for more than 15 minutes, about three wraps, with
the clock holding; see the [target notes](../targets/t14-gen1-amd/notes.md#native-status).
TSC with extended-HPET fallback is the accepted
[later direction](../wip/later-os-directions.md#clock-source).

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
with a positive minimum; HPET confirms expiry before waking a sleeper, so an
early or stale interrupt only recalculates the next arm.

Nanoseconds are the representation, not a wakeup-precision guarantee. Delayed
interrupts and scheduling still delay execution, but missing interrupts no
longer extends a deadline by losing counted ticks. There is no busy-wait sleep.

The clock need not include time while QEMU is paused. The extension support limit
above additionally excludes full-wrap gaps during which the counter advances.
There is no clock-setting operation, cancellation, HPET alarm, or userspace
direct counter mapping. Native deadline-timer qualification remains in
[sleep wake debt](../technical-debt.md#sleep-wake-granularity).
Wall-clock precision and HPET read cost
are tracked in [technical debt](../technical-debt.md#wall-clock-time-and-clock-source-performance).
