# Userspace local time

The kernel clock remains UTC Unix seconds. Libc converts those seconds using
[the packaged IANA database](timezone-data.md); it neither changes the clock nor
requires Lua. Monotonic deadlines are unaffected.

## Selection and authority

`TZ` comes from the [mutable process environment](process-state.md), seeded
from startup. Absent or empty `TZ` means UTC and needs no zone-file capability
or allocation. Initial environment copying may allocate; lookup failure is
reported rather than treated as absence. A nonempty value is an
IANA name such as `Europe/Bucharest`, `Pacific/Auckland` or `Etc/UTC`. Libc reads
`boot://share/zoneinfo/<name>` through the process's existing `boot` directory
capability. Names allow ASCII letters, digits, `_`, `-`, `+` and nonempty
slash-separated components. Absolute paths, dots, URI prefixes and raw POSIX
rule strings are not accepted.

Named-zone selection is lazy. The first conversion loads and validates the file;
a process-owned cache retains its bytes until replacement or process exit.
Changing the selected name replaces the cache only after a successful load.
Invalid selections report errors on each conversion, never the previous zone or
a silent UTC fallback. Files are not watched or reloaded while the name remains
unchanged. The cache assumes the current one-thread-per-process runtime.

The shell forwards an explicit snapshot of its current environment to children. There is no new shell
assignment command or timezone syscall. The default [session configuration](session-configuration.md)
explicitly selects Bucharest and supplies `TZ` when starting the shell.
Startup that bypasses the session launcher still uses UTC when `TZ` is absent
or empty; libc's default has not changed.

## C interface

`<time.h>` adds `localtime_r(const time_t *, struct tm *)` and
`localtime(const time_t *)`. `struct tm` includes `long tm_gmtoff`, measured in
seconds east of UTC, and a borrowed `const char *tm_zone` designation.
`tm_isdst` follows the database; daylight time need not mean
a larger UTC offset. UTC conversion sets offset and DST to zero and names the
zone `UTC`. Local designations point into the validated TZif cache and remain
valid until a successful cache replacement or process exit.

`localtime_r` returns the destination on success, preserving errno. It returns
NULL on failure without changing the destination. `localtime` uses its own
static result, overwritten by its next successful call. Existing `gmtime` and
`gmtime_r` remain allocation-free UTC conversions and ignore `TZ`.

`mktime(struct tm *)` is the inverse of `localtime` in the zone `TZ` currently
selects. Month, day, hour, minute and second may lie outside their ranges and
are normalized with the pinned calendar arithmetic; `tm_wday`, `tm_yday`,
`tm_gmtoff` and `tm_zone` are ignored. Libc tries every UTC offset the zone can
report and keeps each instant whose `localtime` offset matches:

- **One match:** the result, unless a nonnegative `tm_isdst` disagrees with it.
  Then, as C specifies and glibc and musl do, the wall time is read in the
  requested kind of time, using the UTC offset of the nearest period of that
  kind in the zone data: within the footer's rules, the rule's other offset;
  otherwise the closest earlier or later table period, the earlier on a tie.
  July noon with `tm_isdst` 0 in Bucharest is therefore 12:00 EET, shown as
  13:00 EEST, and Lord Howe's half-hour DST shifts by 30 minutes. A zone with
  no period of that kind, such as UTC, keeps the match; glibc instead assumes
  a one-hour difference there.
- **A fold** (the wall time occurs more than once): a nonnegative `tm_isdst`
  selects the only candidate whose daylight flag matches. Without one, and in
  folds where both candidates have the same flag, such as a standard-offset
  change, `mktime` fails with `ENOTSUP`.
- **A gap** (the wall time is skipped): `ENOTSUP`. Other libraries move the
  time across the gap instead.

These `ENOTSUP` cases are a stated profile restriction, not overflow. Zone
errors are `localtime`'s. A result that cannot be represented fails with
`EOVERFLOW`. Success stores the normalized `localtime` result, preserving
errno; failure returns -1 and leaves the structure unchanged. Since -1 is also
a valid time, callers distinguish failure by setting errno first.

File and allocation failures retain their corresponding errno. Invalid names,
malformed TZif and unsupported file formats use `EINVAL`; unspecified local
time uses `ENOTSUP`; timestamps whose converted calendar cannot fit `tm_year`
use `EOVERFLOW`. Missing named files do not become UTC.

## Data interpretation

The loader supports leap-free TZif versions 2, 3 and 4. It bounds the blocks and
footer before use, checks ordered 64-bit transitions, type and designation
indices, indicator flags and footer syntax. Type zero supplies times before the
first transition. Files with no transitions use their footer or type zero.

After the transition table, the footer determines offsets and DST. Its Julian,
day-of-year and month/week/weekday rules include signed extended transition
times. Adjacent rule years handle changes crossing UTC New Year, southern
hemisphere seasons and all-year DST. An empty footer beyond the table, or a
`-00` designation, means unspecified time and produces an error rather than
freezing the last known offset. No leap-counting database is supported.

Calendar arithmetic and rule-to-date calculations use the pinned musl subset;
loading, bounds checks, cache ownership and error propagation belong to Pyxis.
The local adaptations and license are recorded in userland's musl provenance.
The file contract follows [RFC 9636](https://www.rfc-editor.org/rfc/rfc9636.html).

## Date display and limits

`date` displays `YYYY-MM-DDTHH:MM:SS+HH:MM` using the selected zone. Historical
offsets with nonzero seconds retain those seconds in an `+HH:MM:SS` suffix.
`date -u` ignores `TZ` and prints UTC with `Z`. Both diagnose clock/conversion
errors and years outside the four-digit display range.

`strftime` formats the C locale's English names, numeric calendar fields,
composite dates/times, ISO week dates, `%z` and `%Z`. Standard E/O alternatives
use the same C-locale forms. `%z` uses minute precision even when `tm_gmtoff`
contains historical seconds; `%Z` uses the actual UTC or TZif designation.
Invalid formats report EINVAL; insufficient output space returns zero. Format
flags and field widths are not supported. `difftime` subtracts timestamps before
conversion to double, retaining small intervals at large timestamps.

Ambiguous and nonexistent local input fail as described for `mktime`; locale,
per-user policy and live configuration reload remain outside this interface. TCC's existing calendar
macros continue to use UTC. Firmware-clock precision and synchronization limits
remain as described in [wall-clock support](../kernel/wall-clock.md).
