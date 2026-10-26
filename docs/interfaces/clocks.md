# Clocks and precision authority

A clock capability grants access to the shared boot epoch, not a private timer.
The [native protocol](../../include/abi/clock.h) separates reading precision from
sleep authority. Handle copies and launch declarations preserve only their
explicitly selected rights.

## Reading and sleeping

| Operation | Required rights | Result |
| --- | --- | --- |
| `CLOCK_NOW` | `READ` and `PRECISE` | Fresh monotonic nanoseconds |
| `CLOCK_WALL_NOW` | `READ` and `PRECISE` | Fresh Unix UTC seconds and nanoseconds |
| `CLOCK_COARSE_NOW` | `READ` | Cached monotonic nanoseconds |
| `CLOCK_WALL_COARSE_NOW` | `READ` | UTC advanced by the cached observation |
| `CLOCK_SLEEP_UNTIL` | `SLEEP` | Sleep until an absolute monotonic deadline |

Precise and coarse readings use the same epoch and nanosecond units. Each
quality is monotonic separately; a coarse reading may trail a preceding precise
reading. Nanosecond units imply neither nanosecond accuracy nor precise wakeup.
Denied precise reads never silently downgrade to coarse. Precision authority
does not prevent every timing side channel.

The timer interrupt publishes its existing sample to a shared monotonic cache
before waking waiters. Concurrent older samples cannot lower the cache. Nominal
freshness is one serviced preemption period, about 8.3 ms; masked interrupts,
firmware stalls and VM/debugger pauses have no elapsed staleness bound. Coarse
UTC clamps elapsed time to zero if the cache predates the UTC anchor. A missing
UTC seed remains unavailable; see [wall time](../kernel/wall-clock.md).

Absolute sleep needs only `SLEEP`. Existing relative helpers first construct a
checked deadline from a fresh precise reading, so also need `READ|PRECISE`;
addition overflow fails instead of wrapping. There is no separate SLEEP-only
relative operation. Public deadline admission stays precise. Execution after a
wake can be delayed by scheduling.

## SDK, libc and declared grants

Libpyxis exposes precise `clock_now` / `clock_wall_now` and explicit
`clock_coarse_now` / `clock_wall_coarse_now` in `<clock.h>`. Standard libc
`CLOCK_MONOTONIC`, `CLOCK_REALTIME` and calendar reads stay precise; the named
`CLOCK_MONOTONIC_COARSE` / `CLOCK_REALTIME_COARSE` IDs are explicit cached reads.
Insufficient reading rights yield a real denial, including `EACCES` through libc.

Programs declare the rights they use in their bundles. Relative timeout helpers
need a precise anchor as well as sleep authority. The Go runtime checks its
required clock rights at startup; hosted Clang retains the selected clock rights
when launching its linker. Independently published programs must be rebuilt
with matching SDK/runtime and grant declarations after an interface change.
See [release ABI policy](abi.md) and [hosted tool development](../development/hosted-tools.md).

## Kernel observations and sources

Audited readiness, network and npfs worker passes use cached observations and
matching coarse parking. Scheduler passes avoid redundant expiry scans;
precise earlier-target arming still ensures progress. Operation admission,
hardware waits, debugger/NMI handling and measurement brackets retain precise
reads. Metadata timestamps may repeat. The
[timekeeping reference](../kernel/timekeeping.md#clock-reads-in-timer-handling)
defines these boundaries and the cross-CPU agreement assumption.

[Source selection](../kernel/timekeeping.md) starts with HPET, admits KVM time
only after all CPUs register, otherwise qualifies native invariant TSC or keeps
HPET. KVM consumes versioned per-CPU conversion records and applies a shared
atomic floor even without a stable-TSC flag; no `invtsc` requirement is added.
Native TSC's finite boot agreement check does not prove future cross-CPU order.
Runtime source switching, a watchdog, a userspace counter mapping, suspend/resume
and guest migration are not implemented or qualified; the
[clock-source debt](https://git.internal/PyxisOS/pyxis-os/issues/1380)
records progress, ordering and HPET extension limits.

## Qualification

The [baseline](../development/experiments/clock-read-baseline.md),
[interrupt-cache record](../development/experiments/clock-cache.md),
[KVM record](../development/experiments/kvm-clock.md) and
[precision integration record](../development/experiments/clock-precision.md)
separate nested measurements, debugger checks, hosted-runtime qualification and
native ThinkPad results. The final native DATA-package comparison has overlapping
ranges and establishes no attributable speedup. Earlier measurements use
different source/configuration boundaries and are not interchangeable.
