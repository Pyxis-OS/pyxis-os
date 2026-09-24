# Userspace local time

The kernel clock remains UTC Unix seconds. Libc converts those seconds using
[the packaged IANA database](timezone-data.md); it neither changes the clock nor
requires Lua. Monotonic deadlines are unaffected.

## Selection and authority

`TZ` comes from the process's startup environment. Absent or empty `TZ` means
UTC and needs no filesystem capability or allocation. A nonempty value is an
IANA name such as `Europe/Bucharest`, `Pacific/Auckland` or `Etc/UTC`. Libc reads
`app://share/zoneinfo/<name>` through the process's existing `app` directory
capability. Names allow ASCII letters, digits, `_`, `-`, `+` and nonempty
slash-separated components. Absolute paths, dots, URI prefixes and raw POSIX
rule strings are not accepted.

Named-zone selection is lazy. The first conversion loads and validates the file;
a process-owned cache retains its bytes until replacement or process exit.
Changing the selected name replaces the cache only after a successful load.
Invalid selections report errors on each conversion, never the previous zone or
a silent UTC fallback. Files are not watched or reloaded while the name remains
unchanged. The cache assumes the current one-thread-per-process runtime.

The shell forwards its startup environment to children. There is no new shell
assignment command or timezone syscall. The default [session configuration](session-configuration.md)
explicitly selects Bucharest and supplies `TZ` when starting the shell.
Startup that bypasses the session launcher still uses UTC when `TZ` is absent
or empty; libc's default has not changed.

## C interface

`<time.h>` adds `localtime_r(const time_t *, struct tm *)` and
`localtime(const time_t *)`. `struct tm` includes `long tm_gmtoff`, measured in
seconds east of UTC. `tm_isdst` follows the database; daylight time need not mean
a larger UTC offset. UTC conversion sets both fields to zero.

`localtime_r` returns the destination on success, preserving errno. It returns
NULL on failure without changing the destination. `localtime` uses its own
static result, overwritten by its next successful call. Existing `gmtime` and
`gmtime_r` remain allocation-free UTC conversions and ignore `TZ`.

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

Reverse conversion (`mktime`), ambiguous/nonexistent local input, `strftime`,
timezone abbreviations in `struct tm`, locale, per-user policy and live
configuration reload remain outside this interface. TCC's existing calendar
macros continue to use UTC. Firmware-clock precision and synchronization limits
remain as described in [wall-clock support](wall-clock.md).
