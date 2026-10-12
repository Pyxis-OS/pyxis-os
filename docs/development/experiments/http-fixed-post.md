# Fixed native HTTP POST qualification

2026-10-12. Baseline parent `ae0bc1df`, userland `bdef26b`; implementation
parent based on `173f1321`, userland `97ae55f`. Ports `28b5a3bc`, lwIP `a1aadb91`;
fs baseline `9b4479e8`, current `28386cc7`. Ordinary image and focused httpfs builds
use the existing LLVM container; no SDK, kernel ABI or compiler change is needed.

Nested QEMU 10.2.2, q35/KVM, `-cpu max -smp 4 -m 2G`, HPET, UTC RTC,
fresh OVMF variables, VirtIO networking/usernet and entropy, no disk or HDA.
HTTP listeners are local host port 18080; TLS uses local ports 18443/18444,
`10.0.2.2.sslip.io` and a disposable explicit CA. No paid API or credential.
A temporary consumer was staged only in qualification images. It opens a native
FILE with READ, supplies GET/POST/header spans, drains bounded DATA and prints
retained results. No consumer, fixtures, keys or raw captures are installed.

## Costs and memory

Before code, `iobench read http://10.0.2.2:18080/fixed --rounds 3` fetched the
existing 1 MiB pattern: one warmup, three byte-verified samples, 257 FILE reads
plus EOF. OPEN median 188.518 ms (178.652–209.260); complete 221.759 ms
(211.664–232.021), 4.509 MiB/s. Clock-call cost 36.200 µs/read, not subtracted.
`cp boot://share/iobench.bin tmp://post-body`, then
`iobench read tmp://post-body --rounds 3`: complete median 0.433 ms
(0.430–0.459), payload 0.359 ms (0.349–0.386), clock 42.135 µs/read.

Read-only GDB inspection observed 41,104-byte baseline stream scratch and
41,112-byte changed scratch. The upload adds a 4 KiB stack chunk and a copied
request head bounded at 32 KiB, erased on release. Source inspection confirms no
body-sized allocation or allocation per upload chunk; allocator bookkeeping,
network buffers and TLS accounting are separate. TLS returns to 357,604 charged
bytes with public roots plus the local CA; peak including import is 547,786.

## Local fixture results

Commands use `httpget URI --post boot://share/iobench.bin`; TLS additionally uses
`--ca-bundle boot://local-ca.pem`. `tmp://tiny` holds two bytes. The 17 MiB file is
seventeen copies of the existing 1 MiB pattern. Server SHA-256 checks independently
match all fully received bytes; upload counts represent local acceptance only.

| Fixture | Observed result |
| --- | --- |
| Empty native POST | Content-Length 0, sent=true; complete fixed 404 body |
| HTTP/HTTPS 1 MiB POST | Exact Content-Length and SHA-256; complete 200, 13-byte body |
| HTTP 17 MiB POST, deadline 120 s | Exact length/hash; complete 200 after 72.003 s, no 16 MiB request cap |
| Caller `Git-Protocol: version=2` | Received unchanged by GET and HTTPS POST fixtures |
| HTTP/HTTPS early 413, declared 17 MiB | Complete 15-byte error; upload stopped at 4,088 locally accepted bytes, sent=false |
| Chunked/close-delimited 404 | Complete 13-byte body, status retained; chunk trailers validated |
| Short fixed 404 | Failed malformed response after 13-byte prefix; status 404 retained |
| TLS 1.2 authenticated close-delimited 404 | Complete 13-byte body |
| TLS raw EOF / authenticated short fixed body | Failed TLS truncation / HTTP framing, status and 13-byte prefix retained |
| Invalid CR/LF, NUL, duplicate header / owned Host | BAD_ARGUMENT / UNSUPPORTED before start; server saw no request |
| Source length mismatch | SOURCE_ERROR/BAD_REQUEST before start; zero upload |
| Five-second POST deadline, response delayed 35 s | TIMED_OUT after 5.015 s, request_sent=true, no replay |
| 302 | Retained as one response, no redirect/replay |
| HTTP POST response delayed 35 s, deadline 45 s | Complete after 39.114 s including upload |
| HTTPS POST response delayed 35 s, deadline 45 s | Complete after 35.414 s with two-byte source |
| HTTPS peer waits 35 s before reading 1 MiB, deadline 90 s | Complete after 35.438 s; exact hash, no replay |

Adjacent timing order A1/B1/A2 uses the same current kernel/fs with either the
baseline archive/HTTP consumers or the implementation archive. There are no
libc/libpyxis/include source changes between baseline userland and the current
`5fa4f42` used for those measurements. No local build or debugger ran during these
samples; shared host load was uncontrolled. All samples verified every byte.

| Run | GET OPEN median (range), ms | GET complete median (range), ms | RAM complete median (range), ms |
| --- | --- | --- | --- |
| A1 | 1963.950 (1294.158–2044.068) | 2442.248 (1801.015–2938.510) | 2.525 (2.490–2.541) |
| B1 | 1256.534 (762.108–1393.018) | 1475.409 (1042.043–1633.997) | 1.013 (0.988–2.754) |
| A2 | 177.465 (177.125–196.737) | 207.460 (207.115–226.560) | 0.418 (0.415–0.440) |

B1 alone is over six times slower than the pre-edit baseline. HTTP clock calls
vary from 68.346 to 279.772 µs/read; RAM controls vary too (35.797–232.249
µs/read). A2 complete throughput is 4.820 MiB/s, versus pre-edit 4.509 and B1
0.678. These timings establish working transfers under host variation, not a native performance result.

Before code, an explicit 45-second incremental GET against a 35-second body delay
failed at about 30 seconds, retaining status 200 and zero body bytes. The changed
engine retains the explicit deadline; existing snapshots/redirect chains still
have their own 30-second cap. CONNECT remains a single bounded attempt.

The rare TLS 1.2 HelloRequest/control-output guard is established by inspection
of pinned Mbed TLS 4.1.1 `ssl_msg.c` and the BIO send guard, not by inducing a
control message during a pending write. No native hardware run or hostile-peer
coverage beyond the listed fixtures is claimed. Provider transactions, staging
freeze and shell/Lua submission are later tasks.

Final delivery rebases onto parent main `dc4b1487` and userland main `40f81eb`,
pinning userland `d6842f8`. HTTP/TLS/Makefile trees are identical to measured
`97ae55f`. A rebuilt ordinary image with the refreshed SDK and verified unchanged
kernel bundle passed; final-head QEMU rechecks passed exact-hash HTTPS 1 MiB POST,
early HTTP 413 with zero body accepted, duplicate-header refusal before start,
and byte-verified GET snapshot consumption. No new matched timing claim is made
for the concurrent private-capture libc additions.
