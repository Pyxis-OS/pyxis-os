# HTTP prepare: before-code baseline

2026-10-15, task B7 scope only, before implementation. Current merged main
`befe4e180ffec71063715ab6644eaa5b20494ddb`, userland `9c8b325`, ports `f75c3a1`,
fs `22eaf447`, lwIP `a1aadb91`. `git diff 8b9c31da..befe4e18` contains only
Markdown; runtime source and all dependency pins match the exact B6-qualified
`8b9c31da` image. Its preserved kernel/archive hashes were checked against the
local ISO inputs. The baseline uses **that image**, not a claimed rebuilt-main
binary. Builder `pyxis-llvm23.1.3-df87791`; no compiler rebuild.

## Nested existing-tool samples

q35 nested KVM, host CPU, four CPUs/2 GiB, OVMF, std VGA, modern-only VirtIO
net/RNG, info/UDP logging, t14 Remote 80×24, NAT to `10.0.2.2:18080`. B1/B2/B3
are fresh boots of the same unchanged image; each has a warmup then five verified
1 MiB GET/control and three POST samples. G1/G2/G3 are three additional fresh
boots, each warming then measuring five generic-root GETs through `bench=$http`.
The existing B6 fixture, Remote collector, iobench and Lua commands are reused;
no new target program or benchmark infrastructure. All command/FINAL drains,
read-byte verification and six GET/four complete POST counts/hashes per B cohort
passed; each G cohort delivered six verified 1 MiB GETs. No owned build/debugger
ran during timing. Other agents' VM work/host scheduling remained uncontrolled;
wide OPEN/POST ranges are elapsed intervals, not CPU attribution or native claims.
All guests selected HPET; B clock loops were 31.6/48.3/35.1 µs/call.

| 1 MiB elapsed ms | Samples | Median [range] |
| --- | --- | --- |
| GET OPEN | 15 | 243.111 [173.435–430.106] |
| GET payload | 15 | 0.647 [0.580–1.223] |
| GET whole | 15 | 243.955 [174.121–431.503] |
| generic OPEN | 15 | 241.100 [206.779–659.724] |
| generic payload | 15 | 0.706 [0.565–1.292] |
| generic whole | 15 | 241.800 [207.542–660.843] |
| POST stage | 9 | 242.193 [220.026–338.251] |
| POST commit/read | 9 | 858.824 [804.626–1887.185] |
| POST whole | 9 | 1093.317 [1032.599–2225.436] |
| native whole | 15 | 0.292 [0.265–0.320] |
| RAM whole | 15 | 0.456 [0.421–0.635] |

Literal GET goes through the current interim libc option bridge; generic GET
uses one captured PROVIDER root through ordinary typed OPEN. Both return complete
snapshots. Compare these routes separately when B7 adds the configured provider,
not by treating differently named OPENs as equal work. OPEN includes download;
whole consumption includes payload, EOF and close. There is no existing reusable
configuration issuance operation to time: do not invent a zero-cost baseline
for it. Report its new cost separately after owner acceptance.

Commands (the unchanged B6 runner records exact substitutions/raw output):

```text
iobench read http://10.0.2.2:18080/http-b6/iobench.bin --bytes 1048576 --buffer 4088 --rounds 5
export bench=$http
iobench read bench://10.0.2.2:18080/http-b6/iobench.bin --bytes 1048576 --buffer 4088 --rounds 5
```

Controls read `boot://share/iobench.bin` and its RAM copy with the same buffer,
size and rounds. Existing Lua POST writes sixteen 64 KiB `z` blocks to `/http-b6/echo`,
flushes once, verifies `POST accepted`, then closes; one warmup and three samples
record stage, commit/read and whole intervals. Every body is 1,048,576 bytes,
SHA-256 `3ac3338d67611f3edb444a8f730d5e3a6559d4640e7b1a2d5fa58bafbda3254a`.
Cohort query tags only distinguish fixture records. Raw commands, timing JSON,
configuration, provenance and images remain local under
`/shared/build/http-b7-before/`; none are committed. Owned QEMU/fixture jobs stopped.

## Native reference before B7

The owner's just-completed [B6 A/B′](capability-http.md#owner-native-ab-prime-2026-10-15)
is the runtime-equivalent native before-code reference: ThinkPad, three interleaved
pairs, TSC, B′ whole GET 13.650 ms [12.919–15.064], payload 0.967 ms [0.730–1.023],
whole POST 29.753 ms [28.960–31.041]. These are prior owner measurements, not a
new native B7 run; they do not qualify a future prepared-endpoint implementation.
Repeat matched workloads after acceptance/implementation, with revisions, ranges,
route and preparation/execution separated; native attribution needs matched
native evidence.

**Pyxis must be able to hit good network targets overall in the future.**
Current native HTTP reference: 73.26 MiB/s whole GET / 33.61 MiB/s whole POST;
TCP: 85 MiB/s receive / 70.5 MiB/s send (ThinkPad, 2026-10-09). They are
measurements, not contracts or nested targets. Retain the
[throughput revisit point](../../technical-debt.md#http-provider-throughput).
