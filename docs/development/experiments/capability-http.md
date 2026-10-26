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
above. The attribution and corrections below address incidental work without a tuning
increase or transport retry. Keep the native TCP comparison open before HTTP-heavy
consumers.

## Regression attribution and corrections

2026-10-15 review follow-up. These are nested measurements, not native results.
The fixed workload already used near-maximum chunks: one inspected GET had 258
snapshot writes (256 × 4,080 bytes plus 3,920 and 176) and 257 positive client
reads (256 × 4,088 plus 2,048), then EOF. TCP TRY_READ ran 260 times. Increasing
an application buffer cannot exceed these FILE message bounds. RAM quota checks
are local integer accounting, not allocations or IPC: a declared fixed length
reserves once, and later per-chunk checks find that charge already sufficient.

The original path submitted **515 native FILE completions per MiB**: 258 writes
and 257 reads, plus directory create/remove and TCP connect. Each completion
allocates an 8,720-byte kernel request object, copies input/result, crosses BSP
admission, and needs collection and receipt close. That is about 4.28 MiB of
cumulative request-object allocation, not peak memory. It also drove provider
clocks, receiver scans and scheduler/timer work. It was unnecessary for the
provider's exclusively held native RAM FILEs. Persistent backing can block and
keeps completion I/O.

Corrections:

- Verify RAM from the backing directory's attested filesystem type, never from
  FILE_DOMAIN. Use ordinary bounded RAM writes for staged bodies and unpublished
  snapshots, and reads for completed frozen snapshots. The worker holds all
  native backing references; clients hold exported facets. STAGING ends before
  engine source reads; no snapshot writer exists after publication. RAM growth
  allocates on the calling CPU. Persistent I/O, directory operations, resize and
  engine body-source reads retain native completions. Reservations still precede
  growth, and deadlines, copy bounds and failure/uncertainty checks are unchanged.
- Clear terminal request scratch once rather than after each retained READ
  (about 3.16 MiB of repeated clearing removed per MiB read). Publish the terminal
  descriptions once rather than repeating the same two publications in settle.
  Completed idle transactions no longer take progress clocks on every READ.
- Allocate the 64 KiB diagnostic prefix only for an HTTP error response. The
  old code allocated **once per response**, not once per chunk; it copied at most
  64 KiB across chunks and erased it at cleanup. A diagnostic-only comparison
  overlapped the old GET range (674 vs 696 ms medians); it does not explain the
  bulk regression. The correctness fix removes successful GET's dependence on
  optional diagnostic allocation.

Read-only GDB counts (timings excluded):

| Per 1 MiB GET | Original B6 | Corrected RAM path |
| --- | --- | --- |
| Native FILE WRITE / READ completions | 258 / 257 | 0 / 0 |
| Directory create / remove, TCP connect completions | 1 / 1 / 1 | 1 / 1 / 1 |
| Positive native RAM READs | through the worker | 257 ordinary calls |
| Native RAM WRITEs | through the worker | 258–259 ordinary calls |
| Provider CLOCK calls | 1,938 | 1,557 average |
| TCP TRY_READ calls | 260 | 261 average |
| Terminal description publications | 4 | 2 |

The old count covers one GET; the corrected count covers iobench's warmup and
one measured pass, divided by two. TCP segmentation changes tail sizes and write
counts. Debugger stops perturb scheduling, so clock/WAIT counts are indicative,
not unprofiled CPU costs. A read-only QMP checkpoint sample found 114 of 146 BSP
RIPs in HPET access and 30 in VirtIO network service/notification, with two idle.
QMP checkpoints can be biased toward VM exits; this is **not a cycle percentage**.

This guest kept HPET because invariant TSC was unavailable. Each direct clock
read uses at least three HPET MMIO reads. Existing iobench clock loops measured
about 36–37 µs/call; 1,557 provider calls alone imply roughly 56 ms at that
observed rate, excluding additional kernel wait/timer clocks. This is a scale
estimate, not an additive/exclusive profile. The pattern matches the timer/HPET
cost in [network send profiling](../network-throughput.md#qemu-results-2026-10-08).
Cheaper timekeeping is outside B6.

A separate unprofiled `/proc` QEMU-thread accounting window (20 iobench samples
plus warmup, calibration and Remote handoff, 7.873 s wall) used 4.98 CPU s in the
guest, 2.41 in QEMU userspace excluding guest time, and 4.45 in host kernel.
A 2.001 s idle window used 0.08 / 0.03 / 0.03 CPU s respectively. The active sums
span four vCPUs and I/O threads; they are not BSP-only time or percentages of
wall time. Host KVM tracepoint access was unavailable. Device emulation and
host/KVM work must not all be attributed to guest copying.

### Matched corrected runs

Q1/F1/Q2/F2/Q3/F3, three fresh boots per side; same configuration, fixtures,
commands and samples as above, no owned build/debugger during timing. Both sides
use current userland-main startup `20a9818` and the same SDK/kernel/ports.
Q restores the original B6 private HTTP provider source `4965544`, retaining
both review fixes (fixed-body timing is unaffected by the trailer correction).
Its source patch SHA-256 is
`40f7eece212b06670348c603f30da341ec1c7098229260436143d4595878ddae`;
ISO `d3efa9f614050e575fc8fc2c4b3db209115f051ad1c11359350f5383a945c143`.
F is userland code `c6af73f`; `82cf957` adds comments/README only. F ISO:
`0e2de33dbb7d5bcb7589e87db99e6b396c94d71e43f5ffcbc4cf82f88ea4fa20`.
The preserved kernel is B6 on parent main `390b3139`, fs `22eaf447`, lwIP
`a1aadb91`, ports `f75c3a1`, SDK `46de6a2` (unchanged public SDK/runtime; only
private httpfs/parser source changed). Explicit bundle reuse followed input
validation. Before the final write correction, an isolated read-path pair on
this same SDK reduced payload 315 → 186 ms and whole GET 706 → 582 ms; POST
showed no reliable benefit. This separates the read-side gain from spooling.

| Elapsed ms, median [range] | n/side | Q, original provider | F, corrected RAM path |
| --- | --- | --- | --- |
| Native complete 1 MiB | 15 | 0.278 [0.257–0.764] | 0.295 [0.262–0.830] |
| RAM complete 1 MiB | 15 | 0.482 [0.431–0.879] | 0.475 [0.446–0.521] |
| HTTP OPEN 1 MiB | 15 | 418.676 [356.253–710.304] | 276.436 [177.129–610.172] |
| HTTP payload read | 15 | 340.729 [271.802–450.080] | 195.937 [99.514–208.838] |
| HTTP complete | 15 | 766.925 [648.260–1066.531] | 474.557 [294.954–812.018] |
| POST stage | 9 | 368.044 [219.715–393.697] | 214.113 [195.276–221.726] |
| POST commit/read | 9 | 963.569 [773.315–1264.960] | 792.103 [771.434–837.257] |
| POST whole | 9 | 1208.603 [1070.472–1658.657] | 1000.615 [974.528–1056.949] |

Whole GET improves **38.1%**, OPEN 34.0%, payload 42.5%; POST whole improves
**17.2%** and staging 41.8%. Samples include large scheduling variation; per-stage
medians do not sum. POST shares the per-WRITE completion overhead in staging;
its engine body reads still use completions and dominate much of commit time.
A separate live-stream reference in the initial three-pair window was 198.4 ms
[180.0–230.2] GET and 981.7 ms [940.8–1155.4] POST, on the preserved older image.
It is not a same-SDK counterfactual for the final pair. The remaining GET gap is
material and is not declared solved or entirely intrinsic.

Existing native RAM controls wrote 1 MiB through 258 growing writes in 10.180 ms
[9.852–10.739], versus prepared overwrite 10.425 ms [9.907–10.757]; a native RAM
read took about 0.4 ms. These include syscall/allocation/metadata clocks. Full
snapshots inherently add a private write/read and serialize receive-before-read,
with longer first-byte latency. **Hundreds of milliseconds are not an intrinsic
RAM-copy requirement.** Current provider READ RPCs, scheduling and HPET handling
remain implementation cost: even the completed snapshot payload took about 196 ms in the matched F runs. Literal URI compatibility performs four fixed bridge
operations (prepare/commit/status/metadata); renamed generic OPEN performs one.
Neither adds a per-body-chunk OPEN. No capacity or chunk ABI was increased, no
retry was added, and complete-before-publication remains intact.

Existing-tool commands (the POST timing command is the Lua workload described
above, unchanged across sides):

```text
iobench read http://10.0.2.2:19088/fixed --bytes 1048576 --buffer 4088 --rounds 5
iobench read boot://share/iobench.bin --bytes 1048576 --buffer 4088 --rounds 5
iobench read tmp://b6-body --bytes 1048576 --buffer 4088 --rounds 5
iobench write tmp://b6-native-grow --buffer 4080 --rounds 5
iobench write tmp://b6-native-overwrite --prepared --buffer 4080 --rounds 5
```

Raw commands, JSON, fixture hashes, counter/profile dumps and preserved images
remain local in `build/b6-profile/`; no raw dumps are committed.

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

Daybreak's review of parent `b03775f3` / userland `4965544` found the two
correctness issues corrected above. Its controls refused truncated fixed/chunked
bodies and a 30 s mid-body timeout without publishing readable content; the next
GET succeeded. Our corrected ordinary fixture checks refuse buffered 204/205
surplus and declared nonempty 205 with EIO, and accept valid fixed/chunked/close
empty 205 (including validated trailers). They do not claim detection of bytes
sent after a framed body has completed. Successful-response allocation behavior
is source-inspected; no allocation fault injection was run.

The native timing below is owner-run. The final review status is recorded with
the native A/B-prime result below; allocation failure injection is not claimed. Existing synchronous DNS/entropy waits remain 3 s/5 s. B7/B8 and C9 are
not delivered by these checks.

## Current-main integration refresh

The final linear refresh contains actual scope #884 on parent main `b371de5d`
(NCM and documentation merges) and userland main `20a9818`. Code/build checkpoint
is parent `79e876b0`, userland `82cf957`, ports `f75c3a1`, fs `22eaf447`, lwIP
`a1aadb91`. A fresh ordinary full SDK/image build passed with df87791; ISO
`52a7615f7e065429626fe153f79ec3c82c585bc839773e43b1d6537b9fa12470`.
The current-main four-CPU boot repeated valid/refused bodyless framing, truncation,
seek/re-read, 17 MiB CRC, four 1 MiB POSTs, and early rejection followed by GET.
Configured images using those same programs repeated the 4 KiB exact/overflow,
retained-copy release and combined 1 KiB upload + 3 KiB response boundary,
independent JSON root replacement, and local-CA TLS/redirect controls. Explicit
private npfs again passed 8 KiB POST and the 17 MiB CRC despite a 4 KiB RAM cap.
The local `/echo` server records declaring 1 MiB all match the byte count/hash
above; this is not an exactly-once race proof. The refreshed boot is functional
qualification, not another matched timing cohort. Timings and counters above
remain measurements of their recorded revisions. No compiler rebuild or new
guest program, klog or benchmark infrastructure. Owned runtime/build jobs stopped.

## Owner native A/B, 2026-10-15

ThinkPad, hands-free Remote, 18:21–18:30, A1/B1/A2/B2/A3/B3; all six cohorts
PASS. A is main merge base `b371de5d` (userland `20a9818`, ports `318c5c3`);
B is `b05ce591` (userland `82cf957`, ports `f75c3a1`). Both ordinary builds use
`df87791`, matching flags/kernel configuration, info logging, UDP logging and
`REMOTE_BEACON=t14`, with no debug, prebuilt selection or personal game assets.
Same 4,088-byte iobench reads and sixteen 64 KiB Lua POST writes as above.
Each fresh boot has a warmup then five GET/control and three POST samples.
Fixture counts/hashes and command completion passed. These are **native** results,
not the nested cohorts above. Owner packet/raw results are local under
`/shared/present/http-b6-native/`; only the summary belongs here.

| Elapsed ms, median [range] | n/side | A, stream | B, complete snapshot |
| --- | --- | --- | --- |
| GET OPEN | 15 | 1.710 [1.492–1.990] | 12.591 [12.122–13.637] |
| GET payload | 15 | 11.254 [10.257–12.263] | 6.168 [5.672–7.157] |
| GET whole | 15 | 12.893 [11.982–14.321] | 18.904 [17.915–20.832] |
| POST stage | 9 | 11.141 [8.709–12.766] | 6.933 [6.369–7.525] |
| POST commit/read | 9 | 22.776 [20.054–24.401] | 23.939 [23.198–26.581] |
| POST whole | 9 | 31.626 [29.585–36.649] | 30.756 [29.782–32.950] |
| Native whole | 15 | 0.220 [0.210–0.225] | 0.219 [0.212–0.232] |
| RAM whole | 15 | 0.290 [0.288–0.300] | 0.295 [0.292–0.538] |

Whole GET regresses **46.6%**, 77.56 → 52.90 MiB/s versus the 85 MiB/s native TCP
receive reference. POST is unchanged within the observed ranges. TSC was selected
on both: iobench clock loops A 480/213/481 and B 481/214/215 ns/call. This native
gap is **not HPET**. The complete snapshot moves receive/spooling into OPEN,
approximately the old whole-stream time, then serializes payload consumption.
But native RAM control needs only 0.295 ms, versus 6.168 ms through the provider.
A hypothetical OPEN + native-control payload is about 12.9 ms; that is a scale
comparison, not a measured new implementation or a promise of parity.

## Local completed-RAM READ correction

Source attribution: each 4,088-byte client READ already uses the maximum FILE
payload. It queries the exported kind, calls the endpoint, reserves delivery/
reply slots and schedules the provider, which copies from RAM and replies.
Client and provider packet copies, readiness, clocks and scheduling repeat 257
times per MiB plus EOF. There is no extra OPEN per chunk. Increasing the iobench
buffer cannot remove this path. It is avoidable completed-snapshot overhead.

Parent code `4613dbc4`, userland `9c8b325`, ports `f75c3a1` add an optional sealed
native RAM backing to description publication. The export remains the client
object: masks, CUT lineage, issuer description, receiver closure, quota lifetime
and RETIRE/ACK are unchanged. No raw native FILE grant escapes. The source must
be a raw RAM FILE with READ|WRITE; successful publication seals it atomically
only after validation/allocation. In-flight copies retain their description.
A local READ starts under the endpoint guard and copies endpoint header, FILE
reply and bytes under one mapping lease. It avoids provider delivery, receipt
allocation, round-trip copies and per-READ provider clocks. Existing kernel
deadline checks remain; timekeeping and the chunk bounds are unchanged.
The owner queries cumulative copied bytes for STATUS/retirement, preserving
reread counts without per-READ messages. Persistent snapshots keep provider I/O.
See the [interface](../../interfaces/file-providers.md#file-dispatch-and-lifetime).

### Matched local-READ nested comparison

A1/B1/A2/B2/A3/B3, three fresh boots per side. Here A is the **old B6**
`b05ce591` / userland `82cf957`, not main's streaming A above; B is local-READ
code `4613dbc4` / userland `9c8b325`. Same ports `f75c3a1`, fs `22eaf447`, lwIP
`a1aadb91`, df87791 builder, ordinary full image builds and identical effective
kernel configuration/flags. q35 nested KVM, host CPU, four CPUs/2 GiB,
modern VirtIO net/RNG, OVMF, info logging, t14 Remote 80×24. The native packet's
runner/fixtures were copied unchanged, with numeric `10.0.2.2:18080` and loopback
beacon destination. One warmup and five verified GET/control / three POST
samples per fresh boot. All six cohorts PASS, six GETs/four byte/hash-verified
1 MiB POSTs each. No owned build/debugger ran during timing; other agents' VM
work and host scheduling were uncontrolled. Broad OPEN/POST variation matters.
Both guests selected HPET (35–46 µs clock loops), unlike the native TSC pair.

| Elapsed ms, median [range] | n/side | Old B6 | Local RAM READ |
| --- | --- | --- | --- |
| GET OPEN | 15 | 273.968 [222.956–346.862] | 371.947 [217.244–483.756] |
| GET payload | 15 | 191.259 [148.296–249.300] | 0.739 [0.582–0.955] |
| GET whole | 15 | 477.170 [425.077–520.616] | 372.955 [217.893–484.531] |
| POST stage | 9 | 259.580 [221.583–353.621] | 260.228 [221.848–578.583] |
| POST commit/read | 9 | 965.277 [803.438–1558.615] | 925.906 [830.634–2456.692] |
| POST whole | 9 | 1192.712 [1027.434–1827.187] | 1229.174 [1066.288–3035.275] |
| Native whole | 15 | 0.292 [0.264–0.390] | 0.318 [0.275–0.733] |
| RAM whole | 15 | 0.482 [0.440–0.552] | 0.490 [0.433–0.548] |

Completed payload drops **99.6%**, 191.259 → 0.739 ms/MiB, against a 0.490 ms
RAM control. Whole GET drops **21.8%**, 477.170 → 372.955 ms. OPEN's median rises
35.8% with overlapping broad ranges, despite no receive/spool-path change;
POST has no reliable change. The payload result isolates the removed RPC cost;
this run does not establish receive CPU cost or predict native speedup. Remaining
local packet dispatch/copies take more than a raw FILE read, but no longer require
provider scheduling per chunk. There is no extra total-body limit, cache, retry,
capacity change or partial publication. The native B′ comparison below measures
the hardware effect separately.
Raw data/images/scripts stay local in `/shared/build/http-b6-read-profile/`;
its preliminary A99 observation and failed harness attempts are excluded.

Ordinary full SDK/image build passed on code `4613dbc4`, including all rebuilt
userspace/ports consumers of the replaced description-publication layout.
Four-CPU QEMU repeated valid/refused 204/205, truncation, typed alias OPEN,
seek/reread, 17 MiB CRC and POST. Interim STATUS returned consumed 0 → 13 → 26
through two reads of the same 13-byte reply. Current-program configured boots
passed 4 KiB exact/overflow and retained-copy final release, independent JSON
roots, local-CA/TLS controls and the shared 1 KiB upload + 3 KiB response boundary.
A private npfs boot with the 4 KiB RAM cap passed 8 KiB POST and 17 MiB CRC on a
copied disposable GPT disk. An initial unmounted test and a stale Remote bind
were harness failures, excluded; no target fix was inferred from them.

Read-only GDB, outside timing, stopped ordinary iobench at `file_read_captured`
with sealed native RAM and the 216-byte endpoint header prefix, confirming the
local copy path; iobench's bytes/EOF check and Remote FINAL drain passed after
detach. This is not a race, leak or failure-injection proof. Independent source
review caught a split header/payload mapping lease in the initial unqualified
implementation; the final shared helper copies both under one lease. Source
review found no further material issue. The owner's final Daybreak clearance is
recorded below; this does not imply allocation fault injection.

Native rerun packet: `/shared/present/http-b6-native/images/B-prime/`, with the
unchanged `run.py` in its separate `bprime-run/` working packet. Its A is the
original main A, its B is B′; the original B image and native results are intact.
The exact delivered head is recorded by each image's REVISION/PINS/provenance
and SHA256SUMS. The owner completed that rerun below.

## Owner native A/B-prime, 2026-10-15

ThinkPad, hands-free Remote, 19:36–19:44, three interleaved fresh-boot pairs
A1/B1/A2/B2/A3/B3; all PASS. A is the same main `b371de5d` (userland `20a9818`,
ports `318c5c3`); B′ is #885 `8b9c31da` (userland `9c8b325`, ports `f75c3a1`).
Same df87791 builder, effective configuration/flags, info/UDP logging and
`REMOTE_BEACON=t14` as the first native pair. The runner, fixture, collector and
host Remote tool are unchanged. Commands remain the 4,088-byte iobench GET and
sixteen 64 KiB Lua POST writes documented above. Each cohort has a warmup and
five GET/control plus three POST samples; all six GET/four POST counts and body
hashes passed per cohort. Original and B′ runs remain separate; these are
**native**, not nested results. Raw records and the owner's one-screen summary
stay in `/shared/present/http-b6-native/bprime-run/`.

| Elapsed ms, median [range] | n/side | A, stream | B′, local RAM READ |
| --- | --- | --- | --- |
| GET OPEN | 15 | 1.590 [1.416–2.154] | 12.621 [12.039–14.318] |
| GET payload | 15 | 10.641 [10.070–11.856] | 0.967 [0.730–1.023] |
| GET whole | 15 | 12.467 [11.624–13.656] | 13.650 [12.919–15.064] |
| POST stage | 9 | 12.138 [10.760–14.292] | 6.750 [6.374–7.033] |
| POST commit/read | 9 | 24.860 [23.854–27.081] | 22.961 [22.259–24.484] |
| POST whole | 9 | 36.932 [34.613–40.277] | 29.753 [28.960–31.041] |
| Native whole | 15 | 0.223 [0.218–0.437] | 0.220 [0.218–0.327] |
| RAM whole | 15 | 0.293 [0.287–0.503] | 0.291 [0.289–0.302] |

Whole GET is now **+9.5%**, 12.467 → 13.650 ms, **80.21 → 73.26 MiB/s**;
whole POST improves **19.4%**, 36.932 → 29.753 ms, **27.08 → 33.61 MiB/s**.
Native/RAM controls are unchanged. Both select TSC: clock loops A 479/614/480,
B′ 212/217/212 ns/call. Those differences are recorded, not subtracted.
The current native TCP reference remains 70.5 MiB/s send / 85 MiB/s receive
(2026-10-09), a reference measurement rather than a contract. B′ GET reaches
about 86% of that receive reference; compare the end-to-end workload with each
relevant change rather than equating POST's staging/response interval with TCP.

The former 6.168 ms completed-snapshot READ phase is replaced by a 0.967 ms
payload in this later matched window; this cross-window comparison is indicative,
while the table is the matched comparison. Complete-before-success necessarily
serializes receive/spooling and subsequent reading, making OPEN 12.621 ms before
the 0.967 ms payload. The remaining roughly 1.18 ms whole-GET cost is consistent
with that serialization and local packet dispatch/copies; it is not an exclusive
CPU attribution or proof that every remaining overhead is irreducible. Medians
of phases need not sum. Provider per-READ round trips are no longer the large
native regression. Retain the [network requirement and revisit point](https://git.internal/PyxisOS/pyxis-os/issues/1451).

Owner confirmed Daybreak cleared the fast path and the earlier two Low findings
were fixed. B6 is final for merge with this measured GET cost; no B7/B8/C9 work
is included. Runtime qualification remains at the exact measured B′ head above;
no additional boot or allocation failure injection is claimed.
