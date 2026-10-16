# Capability HTTP retirement

2026-10-16, nested QEMU only. Before source implementation: parent `be010d92`
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
use HPET (roughly 34–35 µs clock calls here); native TSC behavior is unmeasured
for B8. Native evidence is needed to decide the end-to-end impact against the
existing ThinkPad TCP/B6 reference; do not present the focused result as a
native performance pass.

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
