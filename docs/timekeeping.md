# Monotonic time and deadline sleep

The kernel uses a shared, 64-bit HPET counter discovered through ACPI. It starts
the counter during BSP architecture initialization, before starting APs. Its
epoch is clock initialization during this boot, not a calendar date. All CPUs
and clock capabilities observe that same epoch.

The register page is mapped supervisor-only, writable, non-executable and
uncacheable. The clock uses the advertised counter period and reads high/low/high
words to avoid a torn value on implementations that split 64-bit MMIO accesses.
Conversion to nanoseconds avoids overflowing an intermediate femtosecond product.
The initial implementation requires a page-aligned memory-mapped HPET with a
64-bit counter and a valid advertised period; boot fails explicitly otherwise.

HPET supplies elapsed time only. Its comparator interrupts and legacy replacement
mode are disabled. The local APIC still provides 120 Hz preemption, with its
existing PIT calibration. No PCI or VirtIO device support is involved.

## Clock capability

Boot grants init a named `clock` resource. The shell forwards it to foreground
children and sessions when present, like its other explicit grants. The object
holds authority to use the shared clock; it does not own a private epoch or an
asynchronous timer. Closing a handle releases that grant.

[The protocol](../include/abi/clock.h) uses synchronous tagged requests:

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
Deadline expiry is checked by BSP scheduling and timer preemption. Existing
resource-wakeup ordering remains in place: notification before a task finishes
parking cannot make its still-running context available on another CPU.

`kernel_task_sleep_until` takes an absolute deadline and yields if it is already
past. The presentation task targets approximately 60 Hz and drops missed frames
rather than repeatedly rendering to catch up. This is not display refresh
synchronization. Timed console operations retain one deadline across their waits.

Nanoseconds are the representation, not a wakeup-precision guarantee. Delayed
interrupts and scheduling still delay execution, but missing interrupts no
longer extends a deadline by losing counted ticks. There is no busy-wait sleep.

The clock need not include time while QEMU is paused or the machine suspended.
There is no clock-setting operation, cancellation, HPET alarm, or userspace
direct counter mapping. Wall-clock precision and HPET read cost
are tracked in [technical debt](technical-debt.md#wall-clock-time-and-clock-source-performance).
