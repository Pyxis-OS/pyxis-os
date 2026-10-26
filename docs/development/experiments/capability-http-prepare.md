# Reusable HTTP preparation (B7)

2026-10-15. Ordinary builds use `pyxis-llvm23.1.3-df87791`; no compiler rebuild.

| Artifact | Parent | Userland | Ports |
| --- | --- | --- | --- |
| Before A | `ef57f0a5` (scope docs on main `9155b226`) | `15e943c1` | `f75c3a1` |
| Timed implementation B | `d86e990c` | `66b094c` | `f75c3a1` |
| Final failure-path qualification | `adb2d11d` | `53b63ed` | `f75c3a1` |

fs `22eaf447`, lwIP `a1aadb91` throughout. Final code adds only the failed
byte-capture correction described below; the success path makes no additional
SDK call (source inspection, not an exact-final-head timing claim). Before-code
A was built and sampled before C edits; its
three fresh-boot cohorts passed: whole GET median 259.898 ms [200.627–390.529],
whole POST 1085.740 ms [1002.269–2199.780]. The earlier scope baseline used the
B6-qualified `8b9c31da` image (runtime/pin-equivalent to then-main `befe4e18`):
literal/generic whole GET 243.955/241.800 ms, whole POST 1093.317 ms. It is
historical, not a claim to have built current main then.

## Matched nested runs

q35 nested KVM, host CPU, four CPUs/2 GiB, OVMF, std VGA, modern VirtIO net/RNG,
info/UDP logging, t14 Remote 80×24, NAT `10.0.2.2:18080`; no disks/audio. Flags:

```text
make -j8 image REMOTE_BEACON=t14 LOG_LEVEL=info LOG_UDP=1 DEBUG_NET= DEBUG_WAIT=0 DISPLAY_FLIP=0 DISPLAY_FLIP_METRICS=0 DISPLAY_TIMING=off DISPLAY_TIMING_METRICS=0 INIT= NETWORK_CONFIG= MOUNT_DISK= PREBUILT= DOOM_WAD= DOOM_DEMOS= QUAKE_DATA= DIABLO_DATA= DUKE3D_DATA=
```

A1/B1/A2/B2/A3/B3 alternate fresh boots. Each reuses the unchanged B6 fixture,
Remote collector, iobench and Lua POST commands: warmup, five verified 1 MiB
GET/native/RAM samples and three POST samples. Additional configured-endpoint
GET/POST samples run in each B boot, after its controls. Issuance and use share
one Remote shell so exported bindings survive; the initial collector attempt
incorrectly reconnected between them and failed lookup. Its raw output remains
local, excluded from this complete series; no product fix was inferred from it.
All final cohorts passed command/FINAL drains, bytes, exact six GET/four POST
counts and body hashes. No local build or GDB ran during timing. Concurrent CI,
other agents' VMs and horse scheduling were uncontrolled: these are elapsed
nested samples with wide ranges, not isolated CPU or native attribution. All
selected HPET; per-call clock overhead remains visible in the raw iobench output.

| 1 MiB elapsed ms | Before A median [range] | B median [range] |
| --- | --- | --- |
| GET OPEN | 309.225 [203.279–406.744] | 289.047 [157.122–490.059] |
| GET payload | 0.626 [0.577–1.057] | 0.605 [0.587–0.910] |
| GET whole | 309.865 [203.919–407.386] | 289.728 [157.767–490.713] |
| POST stage | 235.048 [218.566–476.806] | 239.584 [192.117–350.698] |
| POST commit/read | 898.245 [759.725–985.268] | 942.342 [679.615–2002.443] |
| POST whole | 1138.362 [985.664–1402.541] | 1181.926 [879.722–2261.708] |
| native whole | 0.303 [0.270–0.675] | 0.296 [0.261–0.489] |
| RAM whole | 0.476 [0.435–0.862] | 0.512 [0.436–0.796] |

15 GET/control and nine POST measured samples per side. Whole GET −6.5%, POST
+3.8%; the broad overlapping ranges do not establish a speedup or regression.
OPEN includes receive/spooling; payload reads the completed snapshot. The
existing generic RAM snapshot fast path remains unchanged.

| Configured B route (new, no invented zero-cost baseline) | Samples | Median [range], ms |
| --- | --- | --- |
| GET OPEN | 15 | 272.544 [150.000–653.147] |
| GET payload | 15 | 0.838 [0.580–2.703] |
| GET whole | 15 | 273.179 [150.664–654.107] |
| POST stage | 9 | 266.122 [191.774–592.236] |
| POST commit/read | 9 | 1063.120 [709.653–1179.813] |
| POST whole | 9 | 1336.808 [906.374–1655.355] |
| issuance command, host-observed Remote interval | 6 | 86.909 [58.994–407.679] |

Issuance includes process launch, capture and Remote completion/drain, not just
private RPC or guest CPU. Configured POST is about 13% above B's interim route
in these noisy samples; this is a different new route, not a matched before/after
result. It adds a generic STAGE call and derived endpoint lineage. Those source
facts do not attribute the measured difference; native comparison is unrun.
No per-chunk reservation/copy/read path was added by configuration.

Commands use existing tools (cohort query tags omitted):

```text
iobench read http://10.0.2.2:18080/http-b6/iobench.bin --bytes 1048576 --buffer 4088 --rounds 5
cfgread=$(http prepare POST http://10.0.2.2:18080/http-b6/iobench.bin)
export cfgread
iobench read cfgread:// --bytes 1048576 --buffer 4088 --rounds 5
```

POST uses existing Lua stdio: sixteen 64 KiB `z` writes, explicit flush, verifies
`POST accepted`, EOF/close; warmup plus three samples. Both routes send 1,048,576
bytes, SHA-256 `3ac3338d67611f3edb444a8f730d5e3a6559d4640e7b1a2d5fa58bafbda3254a`.
The configured route opens `cfgpost://` through generic libc; no Lua HTTP adapter.
Raw commands, JSON, images, hashes and flags are local under
`/shared/build/http-b7-delivery/` (`matched-final/` for the complete interleaved
series). Raw profiling dumps are not committed; no new target benchmark program.

## Ordinary QEMU and debugger qualification

The ordinary full image built with the existing SDK/compiler. Four-CPU QEMU:
POST/PUT/PATCH each used repeatedly through exported libc/Lua names with exact
`abc` length/hash, GET and DELETE. The accepted shell capture example, a two-stage
producer pipeline, copied endpoints and independent retained replies pass.
A native CRLF header FILE is copied: changing it after preparation leaves the
original field on the later request. PUT 307 and PATCH 308 preserve bodies;
PATCH 303 becomes GET. A cross-origin 308 drops the supplied header block while
preserving the body/hash. A one-second configured GET to a slow fixture fails;
a subsequent GET succeeds. Retained replies remain readable after later requests.
No private data or real credentials are used.

GDB on B inspected issuance reply derivation: one grant, origin depth 2,
OBJECT_FORWARDER (53), rights 7, transport 3, CALL_OK. Preparation succeeded while
no HTTP fixture was listening; final fixture counts also show no extra requests.
This is ordinary inspection, not a heap-leak or concurrent-lifetime proof.
A final capture correction makes a failed command with no handed value produce
an immutable failed FILE instead of successful empty/partial byte output.
Explicitly handed values keep their attested outcome, and a native TOO_LARGE
result is preserved. This is needed when `http prepare` refuses locally before
issuing a value; it adds no grant to the command. On the final image, ordinary
QEMU checks confirm a read-only-root preparation refusal and a Lua producer
that writes bytes then exits unsuccessfully both bind native failed FILEs;
describe reports I/O error, and cat reports failure without returning the prefix.
A subsequent PATCH capture and two reads succeed, with exactly one request
recorded by the fixture. The raw machine output was checked offline after a
textual collector assertion mistakenly matched the name `partial://` in stderr.

Masks, slot admission, immutable-copy retirement and frozen trust were also
source-reviewed. Adversarial/lifetime qualification (CUT/exit, admission pressure,
malformed replies, cancellation/publication races) goes to Daybreak through the
owner. No native B7 result or allocation-failure injection is claimed. Owned
QEMU/GDB/fixture jobs are stopped after each cohort.

## Native reference and throughput requirement

The owner's [B6 A/B′](capability-http.md#owner-native-ab-prime-2026-10-15) is the
pre-B7 ThinkPad reference: three interleaved pairs, TSC, B′ whole GET 13.650 ms
[12.919–15.064], payload 0.967 ms [0.730–1.023], whole POST 29.753 ms
[28.960–31.041]. It does not qualify configured endpoints.

**Pyxis must be able to hit good network targets overall in the future.**
Current native HTTP reference: 73.26 MiB/s whole GET / 33.61 MiB/s whole POST;
TCP: 85 MiB/s receive / 70.5 MiB/s send (ThinkPad, 2026-10-09). Measurements, not
contracts or nested targets. Track the new route's gap with native evidence;
retain the [throughput revisit point](https://git.internal/PyxisOS/pyxis-os/issues/1451).
