# Calendar and encoding qualification

QEMU checks for slice 3 of [Neovim task 6](../../../wip/neovim-groundwork.md):
libc's [`mktime`](../../../userland/timezones.md#c-interface), the reduced musl
[`iconv`](../../../kernel/userspace.md#foundational-libc), the numeric/string
closure, and Lua 5.1's `os.time(table)`. Built on main `93beb07a` with userland
`8e85604` and ports `f510e21`, then rebased onto `1ef20a1c`, which
changed no libc, Lua or zone code.

## Configuration

- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 4 CPUs, 8 GiB, standard VGA, run from a Development tab.
- **Host:** Fedora with glibc 2.43 and GCC 16.2.1. The host program read the
  image's own IANA 2026d TZif files through `TZDIR`, so both sides used the same
  zone data.
- **Tools:** one scratch C program, never committed, built for Pyxis with the
  SDK and for the host with GCC, printing one line per case; the outputs were
  compared with `diff`. glibc treats an unset `TZ` as `/etc/localtime`, so the
  host build sets `TZ=UTC` where Pyxis leaves it unset.

## mktime

Of 117 lines, 93 were identical, including every UTC normalization and range
case: `1969-12-31 23:59:59` returns -1 with errno unchanged, out-of-range
month/day/hour/second fields normalize, years 1900 and -1000 convert, and a
`tm_year` of `INT_MAX` with month 12 fails with `EOVERFLOW`. Unique wall times
in every zone, past and future (footer-rule) folds selected by `tm_isdst`,
Dublin's negative DST and Lord Howe's 30-minute DST also matched.

A nonnegative `tm_isdst` that disagrees with an unambiguous wall time matched
glibc in every zone with daylight periods: July noon with `tm_isdst` 0 in
Bucharest (1782900000, 13:00 EEST) and January noon with 1 (11:00 EET), Lord
Howe's 30-minute shift, Dublin's negative DST, New York in 2100 (footer rule),
Tokyo (whose last DST was in 1951), São Paulo and Moscow (both 1 hour, from
their last DST periods), and 02:30 just before Bucharest's March gap.

The rows that differ are the stated profile:

| Zone | Wall time, `tm_isdst` | Pyxis | glibc |
| --- | --- | --- | --- |
| UTC | 2026-07-01 12:00, 1 | 1782907200, 12:00 (no daylight period) | 1782903600, 11:00 (assumed one hour) |
| Europe/Bucharest | 2026-03-29 03:00, -1 (gap) | ENOTSUP | 04:00 EEST |
| Europe/Bucharest | 2026-03-29 03:30, -1 or 1 (gap) | ENOTSUP | 04:30 EEST, or 02:30 EET |
| Europe/Bucharest | 2026-10-25 03:00 and 03:30, -1 (fold) | ENOTSUP | EEST (first) |
| Europe/Bucharest | 2026-10-25 03:30, 0 / 1 (fold) | 1792891800 EET / 1792888200 EEST | same |
| America/New_York | 2026-03-08 02:30, -1 (gap) | ENOTSUP | 03:30 EDT |
| America/New_York | 2026-11-01 01:30, -1 (fold) | ENOTSUP | EST (second) |
| America/New_York | 1883-11-18 12:01, 0 (LMT to EST fold) | ENOTSUP | 12:01 EST |
| Europe/Moscow | 2014-10-26 01:30, 0 (standard-offset fold) | ENOTSUP | 01:30 MSK +3 |
| Australia/Lord_Howe | 2026-10-04 02:15, -1 (30-minute gap) | ENOTSUP | 02:45 +11 |
| Nowhere/Zone | any | ENOENT | UTC |
| ../etc | any | EINVAL | UTC |

Every failure left the input structure unchanged. A `TZ` change between calls
(Bucharest, New York, Bucharest) gave each zone's own result.

## iconv

Conversions matched glibc byte for byte:
- `héllo 😀` from UTF-8 to UTF-16LE and UTF-16BE (with a surrogate pair), and
  back from UTF-16;
- all 256 ISO-8859-1 bytes to UTF-8 (384 bytes) and back;
- EILSEQ at `😀` for ISO-8859-1 and at `é` for ASCII, with the pointers at the
  failing character;
- EILSEQ for a lone low surrogate, an overlong `C0 80`, an encoded surrogate
  `ED A0 80`, `F4 90 80 80` above U+10FFFF and a bad continuation byte;
- EINVAL for UTF-8 or UTF-16 input ending inside a character;
- E2BIG with partial output for 5- and 12-byte outputs;
- byte-at-a-time streaming into a 4-byte output for three conversions, with
  the same per-call results and totals;
- a null-input reset returning zero.

Differences, all intended:
- `ED A0` at the end of the input is EILSEQ on Pyxis and EINVAL in glibc. No
  valid sequence starts with `ED A0`, so it is invalid rather than incomplete.
- `Utf_8` is accepted, through musl's punctuation-insensitive matching.
- `UTF-16`, `UCS-2`, `UTF-32`, the empty name, `//TRANSLIT`, `//IGNORE`,
  `EUC-JP`, `CP1252` and `WCHAR_T` fail with EINVAL on Pyxis.

The UTF-8 decoder was also checked exhaustively on the host against a reference
decoder: every 1-, 2- and 3-byte input and every 4-byte input with a lead byte
from F0 to F7, 151,060,736 inputs, for value, length and the
incomplete/invalid distinction. This found an integer-promotion bug that
accepted overlong two-byte forms; the fixed version had no mismatches, and the
QEMU results above are from it.

## Numeric and string functions

Identical on both sides:
- `strtoimax` and `atol` on `  -42x`, the `long` limits and one past each
  (ERANGE, saturated), `0x7f` and `z`;
- `strtok_r` on `,,a,b;;c,` giving `a`, `b`, `c` and then NULL, and NULL for
  separators only;
- `strcoll` in byte order;
- `isnan` and `isinf` on float, double and long double;
- `trunc` of -2.7, 2.7, -0.5 (giving -0), 1e300 and 4503599627370497.5.

## Lua 5.1 os.time(table)

From the image's `lua5.1` bundle, with the session's `TZ=Europe/Bucharest`
and with `TZ` changed through `uv.os_setenv`:
- 2026-10-10 12:00 gave 1791622800 in Bucharest, 1791648000 in New York and
  1791633600 with an empty `TZ` (UTC), matching the C results;
- gaps and folds without `isdst` returned `nil`; `isdst=true` and `false`
  selected 1792888200 and 1792891800 in the Bucharest fold, and `isdst=true`
  1793511000 in New York's;
- month 14, day 0 normalized to 2026-01-31, an unknown zone returned `nil`,
  and a missing `day` raised upstream's "field 'day' missing in date table";
- string comparison stayed in byte order.

## Limits

- QEMU and host only; no native run.
- Scratch programs and interactive runs, not a test suite.
- glibc is a reference for agreement outside the profile, not the contract:
  where they differ, the accepted contract decides.
