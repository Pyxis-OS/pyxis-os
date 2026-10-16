# Capability HTTP retirement

2026-10-16. Nested baselines and owner-native qualification are separate below.
Before source implementation: parent `be010d92`
(accepted B8 documentation on code main `bea92e98`), userland `cff500e6`,
ports `f75c3a1`. Three separate 4-vCPU/2-GiB q35/KVM boots, host CPU model,
VGA/VirtIO network/RNG; builder `pyxis-llvm23.1.3-df87791`. No owned build,
debugger or second QEMU ran during measurements; host/CI load was uncontrolled.
Same B7 build flags: REMOTE_BEACON=t14, LOG_LEVEL=info, LOG_UDP=1,
DEBUG_NET empty, DEBUG_WAIT=0, flip/timing disabled, no game assets or init override.

Existing Remote runner and local B6 GET/POST fixture, no new guest benchmark.
Each boot used iobench READ with one warmup/five samples at 1 MiB and 4088-byte
buffer on boot FILE, copied RAM FILE and HTTP; Lua ordinary `io.open("w+")`,
16 × 65536-byte writes, explicit flush, response read/close (one warmup/three
samples); `pyxis.run` echo launch/exit and cp HTTP-to-RAM (warmups excluded).
B7 typed capture, reread and interim `=>`/`@` were also exercised; their Remote
windows include host/protocol overhead and are not pure syscall latency.

| Workload | Samples | Median (min–max), ms |
| --- | --- | --- |
| native open | 15 | 0.041 (0.040–0.044) |
| native payload | 15 | 0.213 (0.197–0.242) |
| native complete | 15 | 0.290 (0.274–0.320) |
| ram open | 15 | 0.039 (0.039–0.097) |
| ram payload | 15 | 0.385 (0.357–0.459) |
| ram complete | 15 | 0.460 (0.432–0.534) |
| http open | 15 | 179.622 (170.601–242.136) |
| http payload | 15 | 0.589 (0.574–0.812) |
| http complete | 15 | 180.259 (171.230–242.777) |
| POST stage | 9 | 225.038 (206.093–242.750) |
| POST commit/read | 9 | 871.483 (728.404–898.679) |
| POST whole | 9 | 1100.521 (934.497–1141.428) |
| echo launch/exit | 15 | 8.366 (6.183–10.635) |
| cp HTTP to RAM, launch/exit | 9 | 201.915 (194.966–264.658) |

Each cohort checked 10 GETs and 6 POSTs, correct POST lengths/hashes, and both
retained reads. One earlier partial cohort used unavailable Lua `os.execute`;
it is excluded, and completed cohorts use the existing native `pyxis.run` API.
No API was added for the measurement. No native result or HTTP/TCP attribution
is implied. **Pyxis must be able to hit good network targets overall in the
future**; the existing [native B6/TCP references](capability-http.md) remain the
comparison, not a contract. The comparison below keeps the original before image; none of these nested
figures establishes native throughput.

Raw commands, Remote output, fixture checks, configuration, revisions and image
hash are local under `/shared/build/http-b8-delivery/` (`results/A1`–`A3`,
`before-metadata.json`). The retained before image is for matched repeats and
is removed when the task closes. Provider negotiation is accepted in the
[milestone](../../wip/capability-model-implementation.md#task-8-scope-retire-the-interim-surface-2026-10-16).

## Integrated comparison and qualification

2026-10-16: source parent `09c0ca6b` plus the rescue-list packaging correction,
userland `9049a035` (same source tree as measured `ebd1edb5`) and ports `a8b62659`.
Same build flags, accelerator, devices, CPU/memory and commands as above. Full
ordinary image/SDK/ports build passed. B1/A4/B2/A5/B3/A6 interleaved fresh boots;
15 READ samples and nine POST/copy samples per side, warmups excluded. All
fixture byte counts/hashes passed. No owned build/debugger ran during timings.

| Median ms [range] | Before A4–A6 | B8 B1–B3 |
| --- | --- | --- |
| native whole | 0.304 [0.269–0.827] | 0.268 [0.252–0.307] |
| RAM whole | 0.493 [0.441–1.016] | 0.460 [0.436–0.538] |
| GET OPEN | 182.037 [159.584–267.911] | 261.286 [174.494–607.457] |
| GET payload | 0.821 [0.574–0.914] | 0.709 [0.579–1.096] |
| GET whole | 182.679 [160.554–268.895] | 261.920 [175.386–608.486] |
| POST stage | 214.559 [199.781–688.907] | 212.722 [199.245–225.631] |
| POST commit/read | 775.162 [722.596–2778.885] | 757.611 [742.688–782.506] |
| POST whole | 1001.304 [922.376–3467.792] | 967.249 [955.260–1001.296] |
| echo launch/exit | 11.426 [8.347–35.929] | 10.189 [7.199–12.178] |
| HTTP cp launch/exit | 283.503 [192.106–650.955] | 212.910 [185.784–246.045] |

The full cohort's GET median regressed 43.4%, entirely in OPEN. This is not a
new receive-before-read cost: both revisions already return complete snapshots.
Source comparison found identical libhttp fetch/request code, provider receive/
wait loop, per-DATA maximum (4080 bytes), RAM spool writes, quota checks and
progress-clock checks. Ordinary GET has no navigation prefix. The fixed route
changes from PREPARE/COMMIT/STATUS/METADATA to one typed OPEN; no new per-chunk
work was identified. This source evidence does not prove the timing cause.

A focused follow-up alternated literal `http://` and renamed `web://` READs
within each boot using existing iobench, one warmup/one sample per invocation,
five pairs per image (A then B). Before literal/renamed medians were
174.546/172.240 ms; B8 was 175.333/170.877 ms. Literal ranges were
172.701–176.219 and 170.380–186.911 ms; renamed ranges 171.811–231.448 and
170.638–206.032 ms. Thus this narrower run did not reproduce the regression or
find a name-routing cost. It does not erase the full-cohort result. Both guests
use HPET (roughly 34–35 µs clock calls here). These nested controls do not
establish native performance; the owner-native result below provides that evidence.

QEMU functional checks passed: captured POST and two reads without another POST;
plain Lua stdio and uncaptured POST; a typed failed HTTP 400 value whose reader
fails; renamed roots; retired syntax rejected before creating its output;
quoted `"@name"` literal; ordinary command lookup for `request`; declared
`shell tmp://post.pxs`; and Ctrl+C during a nested script/child returning control
to the shell. Links followed a redirected page's relative link. TCC compiled
and ran redirected C with a relative header included twice under `#pragma once`;
the server observed one header fetch. Links and TCC also passed the explicit
unsupported-operation fallback through ordinary textfs. Generic STREAM retention
is source/build-checked; deeper malformed-reply, cut and lifetime checks go to
Daybreak through the owner.

A read-only GDB checkpoint confirmed the 48-byte configuration layout and empty
process/task retirement queues after functional cleanup; this is not a leak
proof. Raw output, server logs, route samples and GDB transcript stay beside the
cohort records under `/shared/build/http-b8-delivery/`. All owned runtime jobs
were stopped after qualification.

## Focused interleaved attribution (2026-10-16)

Reused the original measured before/after ISOs, q35/KVM/4 CPUs/2 GiB, fixture,
4088-byte iobench buffer and command prefix through GET (copy, native/RAM
controls, HTTP READ). Fresh A1/B1/A2/B2/A3/B3 boots; one warmup and five samples
per command, 15 samples per side. No build/debugger overlap; six verified GETs
per boot. Later POST/capture/launch commands from the full cohort were omitted;
they occur after GET and cannot add work to its measured interval.

| GET ms, median [range] | Before | B8 |
| --- | --- | --- |
| OPEN | 198.234 [170.564–277.188] | 175.702 [162.625–210.517] |
| payload | 0.640 [0.575–1.039] | 0.589 [0.570–0.843] |
| whole | 198.918 [171.196–278.164] | 176.332 [163.355–211.143] |

The original +43.4% was entirely OPEN (+79.249 ms), not payload (-0.112 ms).
This interleaved rerun instead measured whole GET -11.4%; the regression does
not reproduce. Native/RAM whole controls were 0.280/0.460 ms before and
0.258/0.454 ms after. These results retain the original full-cohort evidence;
they do not establish a particular host/KVM scheduling cause.

Source audit at userland `9049a035` against `cff500e6`:

- Plain `fopen` goes through `descriptor_open_result`/`open_typed_path`
  (`libc/descriptor.c:314–335`), not `http_navigation_fopen`. Navigation
  negotiation lives only in `lib/http_navigation.c:257–290`; ordinary provider
  OPEN sets `response_flags=0` (`httpfs/provider.c:832`), so the prefix function
  returns immediately. Plain GET does not negotiate or encode a framed response.
- FILE reads still use one `file_read` (`libc/descriptor.c:1043–1048`); the
  completed-RAM kernel fast path and backing publication remain. No added
  per-READ provider IPC or client copy. Libhttp fetch/request source is unchanged.
- DATA remains bounded to 4080 bytes and ordinary RAM spooling/clock/wait work
  is unchanged (`httpfs/provider.c:1492,1570–1579,1595–1596`). The queue clamp
  cannot split existing DATA chunks. The fixed route replaces four legacy
  PREPARE/COMMIT/STATUS/METADATA calls with one generic OPEN; caller receipt/
  deadline handling differs, but source review finds no added per-chunk work.

The evidence supports variable OPEN/receive scheduling conditions rather than a
reproducible +43% B8 copy/READ cost; the specific cause of the earlier scheduling
variation remains unidentified. No speculative code optimization was made.
The owner-native A/B below checks hardware impact. Raw focused samples are in
`/shared/build/http-b8-delivery/focused-interleaved/`.

## Owner-native A/B (2026-10-16)

ThinkPad T14 Gen 1 AMD, hands-free Remote, 09:13–09:21. A: main `a6510a15`,
userland `cff500e6`, ports `f75c3a1`; B: #905 `e5f1166f`, userland `9049a035`,
ports `a8b62659`. Same `df87791` builder, FLAGS and effective kernel.config;
B6's unchanged runner/fixtures/summarizer. Three interleaved A1/B1/A2/B2/A3/B3
boots, all PASS: 15 GET/control samples and nine POST samples per side after
warmups, with byte/hash/request-count checks. These are native, not nested runs.

| Median ms [range] | A, main | B, B8 |
| --- | --- | --- |
| GET OPEN | 12.775 [12.082–14.283] | 12.317 [11.758–13.792] |
| GET payload | 0.442 [0.439–0.975] | 0.934 [0.746–0.985] |
| GET whole | 13.743 [12.527–15.266] | 13.113 [12.700–14.784] |
| POST stage | 9.708 [6.101–10.804] | 6.987 [6.524–10.674] |
| POST commit/read | 24.272 [22.898–27.712] | 24.114 [22.029–24.403] |
| POST whole | 34.338 [29.310–36.853] | 31.390 [28.788–34.788] |
| Native whole | 0.215 [0.210–0.417] | 0.210 [0.208–0.421] |
| RAM whole | 0.289 [0.288–0.293] | 0.290 [0.287–0.365] |

Whole GET improved 4.6% (72.8 → 76.3 MiB/s) and whole POST improved 8.6%
(29.1 → 31.9 MiB/s). Native/RAM controls are unchanged. GET payload's higher
median overlaps A's range and does not increase whole-GET time. TSC was selected
on both sides; clock-call loops were A 480/485/481 ns and B 214/214/483 ns.
The nested OPEN outlier did not reproduce natively. No code optimization was
needed; the specific cause of that earlier nested variation remains unproven.

**Pyxis must be able to hit good network targets overall in the future.** B8's
76.3 MiB/s whole GET is about 90% of the current 85 MiB/s native TCP receive
reference; POST is 31.9 MiB/s against the 70.5 MiB/s send reference. These are
different end-to-end workloads and reference measurements, not a contract.
Keep tracking the gap rather than treating this result as a general throughput
or concurrency qualification. Daybreak's independent correctness review follows.

Raw results, hashes, flags and the owner's summary stay in
`/shared/present/http-b8-native/` (`results/` and `summary-orchestrator.txt`).
The packet images retain the measured revisions; this subsequent update changes
the qualification record only.
