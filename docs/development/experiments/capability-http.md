# Transparent HTTP snapshots (capability B6)

2026-10-15, B6, before any implementation. Parent `41c3c902` (main `ffb58ef6`
plus scope/acceptance documentation only), userland `a6040708`, ports `318c5c39`,
fs `101e1f68`, lwIP `a1aadb91`. Existing `pyxis-llvm23.1.3-df87791` builder,
ordinary `make -j8 image`, with host Kconfig modules selected through PYTHONPATH.
No compiler rebuild. Preserved ISO SHA-256:
`e2732ca4d62e8180d6b493ee490e030fd44279ca95aec103abb65f26dbd7ad55`.

Nested QEMU q35/KVM, host CPU, 4 CPUs, 2 GiB, std VGA, modern-only VirtIO
network/RNG, OVMF, quiet stock Remote 80×24. Local NAT 24486→2323;
existing HTTP fixtures on host loopback 19088, guest `10.0.2.2:19088`.
Three warmed cohorts on one boot, not three independent boots. Other QEMU
workloads were present and host scheduling/load was uncontrolled. These are
nested elapsed intervals, not native results or isolated guest CPU attribution.
No owned build/debugger ran during timing; clock overhead was not subtracted.

Each cohort uses existing Lua for ten native/text OPENs; existing `iobench` for
five verified 1 MiB reads each from boot, RAM and HTTP; and one POST warmup plus
three samples. POST writes sixteen 64 KiB `z` blocks through plain Lua
`io.open(uri,"w+")`, explicitly flushes, reads and verifies `POST accepted`, then
closes. Stage includes OPEN/write, commit/read includes flush/response/close.
All command completions and Remote FINAL drains succeeded. All 12 POST bodies
(including warmups) arrived exactly once in this run, with 1,048,576 bytes and
SHA-256 `3ac3338d67611f3edb444a8f730d5e3a6559d4640e7b1a2d5fa58bafbda3254a`.
This is a byte-count observation, not an adversarial exactly-once proof.

| Elapsed ms | Samples | Pooled median | Range |
| --- | --- | --- | --- |
| Lua OPEN boot://share/iobench.bin | 30 | 0.03961 | 0.03757–0.05135 |
| Lua OPEN text://welcome | 30 | 2.57617 | 1.32949–4.99816 |
| boot://share/iobench.bin complete | 15 | 0.27284 | 0.25603–0.30265 |
| tmp://b6-body complete | 15 | 0.46952 | 0.45055–0.83865 |
| http://10.0.2.2:19088/fixed OPEN | 15 | 18.73758 | 13.75767–21.58573 |
| http://10.0.2.2:19088/fixed complete | 15 | 245.53188 | 204.42612–292.61822 |
| POST stage | 9 | 374.47278 | 280.56572–443.96342 |
| POST commit/read | 9 | 893.34380 | 824.69066–958.83774 |
| POST whole | 9 | 1247.84007 | 1184.60312–1332.52831 |

Commands and raw JSON/output, server records, configuration, provenance and the
preserved image remain local under `pyxis-capability-shell/build/b6-before/`.
The matched runs below use identical inputs. OPEN is separate from complete
consumption: snapshots move download/spooling into OPEN. The current
whole-GET median is about 4.07 MiB/s; it does not predict native performance.

A separate read-only GDB inspection of the unchanged image, outside the timed
cohorts, observed HTTP private memory at 73,728 bytes idle and 139,264 bytes after
a GET / while a synchronized 1 MiB POST was staged. The staged body occupied a
separate native RAM FILE: 1,048,576 logical bytes, 256 page slots. After explicit
flush/read/close, that FILE was absent; private memory remained at 139,264 bytes
(allocator high-water observation, not a live-allocation or leak proof). The
separate HTTPS process remained at 610,304 private bytes. Backed VM ranges were
1,802,240 bytes idle HTTP and 2,338,816 bytes HTTPS. These checkpoint samples do
not establish peak heap usage or native resident cost. Raw inspection logs stay
local with the baseline. This baseline observation is separate from the changed-image checks below.

**Pyxis must be able to hit good network targets overall in the future.**
The native TCP reference is 70.5 MiB/s send / 85 MiB/s receive (ThinkPad,
2026-10-09), not a contract or nested target. Track the HTTP/TCP gap and retained
snapshot storage cost rather than treating faster OPEN as faster consumption.
Baseline QEMU/fixture jobs were stopped after collection.

## Matched nested runs

Four alternating changed/unchanged cohorts, order B1/A1 through B4/A4, each on a
fresh boot (B1 followed functional warmup). Same q35/KVM, four CPUs/2 GiB,
modern VirtIO net/RNG, fixtures, commands and warmed sample counts as above.
A uses the preserved baseline ISO. B1/B2 use userland `2183545`, B3 `b32a107`,
B4 `d4aea6d`; ports `f75c3a1`, parent ABI/kernel B6 changes, fs `22eaf447`.
The later changes cover zero-byte writes and independent JSON binding, outside
these nonzero/plain-HTTP workloads. Final qualification uses code `cd524b7`
(printable redirect-failure details); userland `f5bd146` adds README changes only.
Ordinary qualification image SHA-256
`74e0c994cfd654db8fe2a9f99664bfb28d10c6708114a6e40ef6f057d4e205de`.
No owned build or debugger ran during timings. Builds occurred between cohorts;
other VMs and host scheduling remained uncontrolled. Main's intervening FAT32
verification changes do not participate in these RAM/boot/HTTP workloads.

| Elapsed ms, median [range] | n/side | A (live stream) | B (snapshot) |
| --- | --- | --- | --- |
| Lua native OPEN | 40 | 0.04069 [0.03785–0.14965] | 0.04129 [0.03954–0.14047] |
| Lua text OPEN | 40 | 2.61903 [0.93816–6.44109] | 3.06392 [0.92778–10.75410] |
| Native complete 1 MiB | 20 | 0.27685 [0.25467–0.71508] | 0.29351 [0.26824–0.52457] |
| RAM complete 1 MiB | 20 | 0.46752 [0.43056–1.03549] | 0.46921 [0.44484–0.50757] |
| HTTP OPEN 1 MiB | 20 | 15.52083 [12.43139–22.01235] | 430.84359 [383.69363–1229.11024] |
| HTTP complete 1 MiB | 20 | 220.02945 [198.92413–248.71018] | 772.86682 [675.98949–2331.55755] |
| POST stage 1 MiB | 12 | 305.18187 [208.56700–425.26435] | 324.76531 [227.53789–715.96563] |
| POST commit/read | 12 | 793.35760 [746.25660–845.94356] | 986.12271 [900.70879–1220.82759] |
| POST whole | 12 | 1120.14811 [1000.08897–1225.02167] | 1373.60523 [1183.90813–1678.95336] |

Whole GET is **+251%**, about 4.54 → 1.29 MiB/s here; POST is **+22.6%**.
These are material nested regressions, not a native estimate or CPU attribution.
Source inspection explains the additional work: full download is spooled before
publication, and spooling and retained reads each submit native FILE completions
(one per bounded chunk), in addition to provider IPC. This is not a measured
cost breakdown. All 32 POSTs including warmups match the 1 MiB byte count/hash
above. Keep the native TCP comparison and throughput debt open before HTTP-heavy
consumers. No optimization, tuning increase or transport retry was added.

## Functional qualification

Ordinary SDK/image builds use the existing df87791 container. Four-CPU QEMU and
local fixtures, including configured images made from the same programs, passed:

- Renamed typed roots and literal URI adapters: GET, POST, DELETE, re-read/seek,
  retained captures; fixed/chunked/close framing; strict 204/205, truncation and
  final-error outcome/errno handling; APPEND and read-only/JSON REMOVE refusal.
- 301/302/303/307/308 GET/POST/DELETE policy, frozen POST body/hash, relative and
  scheme-relative Location, loops/hop cap and public cross-origin. A cross-origin
  307 chain returning to its first origin dropped Authorization, Cookie and the
  credential-marked caller fields permanently; each hop received the same body.
- Disposable local CA: verified TLS, authenticated close-delimited EOF, raw EOF
  and truncated framing refusal, HTTP→HTTPS, downgrade refusal, custom-trust
  same-origin success/cross-origin refusal and explicitly writable TLS POST. A
  separate image with unavailable public trust published HTTP/JSON-HTTP while
  both optional TLS roles were absent; existing HTTP continued to work.
- Configured 4 KiB aggregate RAM budget: exact GET succeeds, overflow/unknown-size
  growth fails without a FILE, retained copies share the charge until final
  release, and body 1 KiB + response 3 KiB shares the same budget. Replacing
  `http` with a 2-byte-budget instance leaves literal `json+http` and its renamed
  alias on the original instance. No 64 MiB boundary allocation was claimed.
- Explicit private npfs volume on a disposable GPT disk: 8 KiB POST and 17 MiB
  GET succeed despite `--storage-cap 4096`; the latter matches host CRC
  636097022 and length 17,825,792. The volume is outside client roots. This is
  functional evidence, not storage durability or npfs performance qualification.

Read-only GDB during a configured retained capture observed a 4,096-byte native
RAM FILE (one page slot) in the provider; it disappeared after final release.
The process's private allocator capacity stayed at 688,128 bytes. This is a
checkpoint/high-water observation, not peak RSS or a leak proof. Raw commands,
JSON, logs, fixture profiles and images remain local in `build/b6*`.

Native B6 timing, fault injection and adversarial/lifetime qualification are not
claimed. In particular, allocation failure, racing cancellation/retirement,
uncertain mutation and malformed description/error-body cases await independent
review. Existing synchronous DNS/entropy waits remain 3 s/5 s. B7/B8 and C9 are
not delivered by these checks.
