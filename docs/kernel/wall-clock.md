# UTC wall clock and calendar conversion

Limine's Date-at-Boot response supplies signed Unix seconds from the platform
RTC. The adapter copies that timestamp and its availability into `boot_info`;
no Limine response pointer is retained. BSP initialization pairs the seed with
a monotonic reading before starting APs. Wall reads add elapsed monotonic time
to that immutable anchor. QEMU runs with `-rtc base=utc`.

The seed has whole-second resolution and an unspecified delay before the
kernel samples its anchor. Fractional advancement does not promise nanosecond
absolute accuracy. An incorrect but representable firmware date is not detected.
There is no synchronization, clock-setting operation or timezone state in the
kernel. Time while the VM is paused or suspended need not advance. UTC has no
distinct leap-second representation. These limitations do not affect the
independent [monotonic deadlines](timekeeping.md).

## Native interface

`CLOCK_WALL_NOW` on the existing clock capability requires `CLOCK_RIGHT_READ`.
The header-only request replies with `clock_wall_reading`: signed 64-bit Unix
seconds and a normalized nanosecond fraction in `[0, 1000000000)`. A missing boot
seed returns `CALL_UNAVAILABLE`; overflowing seconds returns `CALL_LIMIT`.
Monotonic reads and sleeps remain usable without a wall-clock seed.

Libpyxis exposes `clock_wall_now` in `<clock.h>`, clearing its output on failure.
The startup `clock` resource is the same grant used for monotonic reads and
sleep, including ordinary shell forwarding to children.

## C runtime and consumers

`<time.h>` defines signed 64-bit `time_t`, `timespec` and the standard calendar
fields in `tm`. `timespec_get(TIME_UTC)` and `time` borrow the startup clock;
failures set errno rather than inventing a date. `time` returns `-1` on failure,
which is also a valid pre-epoch timestamp. `timespec_get` avoids that ambiguity
and leaves its destination unchanged on failure.

`gmtime_r` and `gmtime` convert Unix seconds using the proleptic Gregorian
calendar, including dates before 1970. They require no capability or allocation.
Years outside the range of the integer `tm_year` produce `EOVERFLOW`.
`gmtime` uses static storage; `gmtime_r` uses caller storage. UTC sets DST to zero.
The conversion comes from the existing pinned musl subset, with provenance and
license recorded in userland.

`date -u` prints `YYYY-MM-DDTHH:MM:SSZ`; plain `date` uses the selected
[local timezone](../userland/timezones.md) and a numeric offset. Both diagnose clock failures
and years outside the four-digit display range. Native TCC expands
`__DATE__` and `__TIME__` from UTC using their C spellings, and uses monotonic
elapsed milliseconds for `-bench`. Benchmark reports retain upstream floating-
point formatting through libc's [printf support](../userland/stdio.md#standard-streams-formatting-and-exit).
Host-running TCC continues to use the host's clock and local time.

[Local-time conversion](../userland/timezones.md) is provided by userspace. Reverse
conversion (`mktime`), `strftime`, locale and clock adjustment remain deferred.
Reads and calendar conversion work independently of [session configuration](../userland/session-configuration.md).
