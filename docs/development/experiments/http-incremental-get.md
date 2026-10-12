# Incremental GET qualification

2026-10-12, nested QEMU 10.2.2/KVM, q35, `-cpu max -smp 4 -m 2G`,
HPET, UTC RTC, VirtIO network/entropy and fresh OVMF variables per boot.
HTTP fixtures were served on host loopback port 18080, guest `10.0.2.2`;
HTTPS used port 18443, `10.0.2.2.sslip.io` and an ephemeral local certificate
explicitly imported by the exercise consumer. DNS resolved that name to the
local gateway; no paid API, credential or remote HTTP service was used.

Baseline: parent `088816ab`, userland `e878f4f`, ports `28b5a3bc`,
fs `b427df29`. Baseline costs were captured before decoder changes.
The first implementation was `9559ab4`; final userland is `b24f390`, rebased
onto `2ccb58e`. The final ordinary image uses parent main `5dc08c5b`, fs
`9b4479e8` and the same ports pin. The compiler container stays
`pyxis-llvm23.1.3-49e2c1a`; no rebuild is needed.

## Snapshot cost

Commands: `iobench read http://10.0.2.2:18080/{fixed,chunked,close} --rounds 3`.
Each response contained the existing 1 MiB iobench pattern. Every reported pass
verified every byte: one warmup and three timed samples, 257 payload FILE reads,
one EOF read. OPEN includes complete snapshot fetching; complete includes its
subsequent consumption. Clock-call overhead was recorded, not subtracted.

The pre-edit capture and alternating baseline/implementation boots used identical
guest configuration and server framing. No local builds or debugger ran during
reported timings. Shared host load was uncontrolled. An initial changed run with
343–1264 ms complete times and a baseline chunked attempt overlapping local ISO
assembly were excluded; the latter was repeated after assembly finished.

| Run / framing | OPEN median (range), ms | Complete median (range), ms |
| --- | --- | --- |
| Pre-edit fixed | 211.445 (207.974–217.984) | 231.745 (228.308–238.881) |
| Pre-edit chunked | 237.057 (225.448–243.170) | 269.413 (257.873–276.429) |
| Pre-edit close | 163.304 (162.640–164.365) | 196.751 (195.025–199.995) |
| B3 fixed | 204.858 (188.619–206.114) | 225.079 (208.634–225.391) |
| A3 fixed | 170.786 (170.672–187.138) | 212.473 (204.883–228.574) |
| B3 chunked | 336.576 (299.721–448.504) | 374.659 (357.733–480.226) |
| A3 chunked | 246.602 (243.816–283.687) | 247.787 (245.182–284.879) |
| B3 close | 209.752 (208.634–213.481) | 241.327 (237.929–242.504) |
| A3 close | 186.757 (170.953–205.004) | 206.164 (203.937–222.224) |

Preliminary order was B1/A1/B2/A2; B is baseline, A was `9559ab4`.
Fixed complete medians were 242.329 / 219.025 / 185.798 / 202.582 ms;
clock calls ranged 35.686–43.640 µs/read. Baseline variation alone is large.
B3/A3 use the same rebuilt current-main kernel: B3 keeps the original baseline
archive/HTTP consumers, A3 uses final `b24f390` after deferring fixed-body terminal
observation until after DATA. Public ABI and runtime sources are unchanged;
the refreshed SDK additionally exports FAT32 support. These measurements establish
working byte-verified transfers, not a native speedup. Final B3/A3 complete
throughput at each median was fixed 4.443/4.706, chunked 2.669/4.036 and close
4.144/4.851 MiB/s; clock calls were 36.388–39.088 µs/read.

## Allocation demand and framing

Read-only GDB inspection observed scratch and charged body reservations; it made
no target calls or memory/register writes. Baseline fixed 8 MiB reserved
8,388,608 bytes plus 41,096-byte scratch. The changed collector reserved the same
body plus 41,104-byte stream scratch. Both 1 MiB chunked collectors peaked at
1,572,864 charged bytes during old/new buffer overlap, ending at 1,048,576 bytes.
The incremental consumer instead kept a 4 KiB caller buffer and a 41,104-byte
stream object while delivering 17,825,792 bytes. Code inspection confirms the
engine has no body allocation or allocation per chunk. Allocator bookkeeping,
network buffers and TLS allocation are separate from these figures.

A temporary local consumer called `http_get_open`, repeated `http_stream_read`
and `http_stream_close`, printing chunk counts and retained framing state. It
was staged only in the qualification image, not ordinary images. The fixtures,
private certificate key, raw logs and screenshots stay outside the repository.
The following are manual QEMU observations at `9559ab4`, with final `b24f390`
rechecks of fixed-body completion, 17 MiB delivery, exact 16 MiB fixed snapshots,
truncation, early close, terminal retention and TLS EOF:

| Fixture | Observed result |
| --- | --- |
| 17 MiB fixed / chunked / close-delimited HTTP | Complete; identical byte sum 2,272,788,480; reads at most 4 KiB |
| Slow chunked 12 KiB, 400 ms pauses | First DATA at 114.528 ms, final at 1210.844 ms; prefix visible before completion |
| Short fixed, short chunk, bad payload CRLF, unfinished trailers | Failed framing after a retained three-byte prefix |
| Overflowed decimal length / hex chunk size | Limit failure with zero body bytes |
| Malformed quoted extension | Failed framing with zero body bytes |
| Framing-changing or oversized trailer | Failed framing / metadata limit after three bytes |
| Fully framed 404 | Complete with status 404 and its three-byte body |
| Early close during slow body | Failed/aborted; delivered prefix retained, native cleanup OK |
| Repeated reads after complete / failed | Same terminal result, zero bytes |
| HTTPS close-delimited with close_notify | Complete |
| Same HTTPS body with raw TCP EOF | Failed TLS truncation after the 1 MiB prefix |
| Authenticated EOF before fixed body ends | Failed HTTP framing after three bytes |
| HTTPS fixed body followed by raw TCP EOF | Complete; framing requires no peer close_notify |
| HTTPS fixed / chunked 1 MiB; fixed 17 MiB | Complete, maximum DATA 4 KiB |

TLS runtime accounting was 547,786 bytes peak / 357,630 retained after close for
both 1 MiB and 17 MiB HTTPS fixtures. This includes imported trust; it does not
include the stream object, caller buffer or kernel/network storage.

`cksum` through unchanged snapshot providers followed `/redirect` successfully
(1 MiB, checksum 1625934143). Short framing published no FILE, 404 remained a
snapshot policy error, and 17 MiB fixed/chunked bodies returned file-too-large.
Exact 16 MiB fixed and chunked snapshots succeeded (checksum 2210080146).
Native handle-close failures and counter exhaustion were inspected, not injected.
No native hardware performance or multi-GB transfer is claimed. The 30-second
cap and synchronous per-read engine remain; responsive provider service and
longer deadlines are later milestone tasks.
