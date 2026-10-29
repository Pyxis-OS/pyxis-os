# Caller-CPU preparation and RAM retirement

Task 4 of [BSP removal](../../wip/bsp-removal.md), accepted placement/lifetime
rules in #1550. This record separates source inspection, nested QEMU results
and native qualification. No new guest benchmark or profiling code is
part of the implementation.

## Pre-code baseline

Captured before kernel edits at clean main `af35537d69f2`, using builder
`pyxis-llvm23.1.3-8bdf136` (Clang 23.1.3), `LOG_LEVEL=info`, ordinary `-O2`
kernel flags and the same effective Kconfig. Dependency pins: userland
`b714efef7652`, ports `f24ee205ca55`, fs `f3f22b6e07e7`, lwIP `4fb74e1b9213`.
Current matching finished SDK/userland/ports artifacts were verified and reused;
no compiler container was rebuilt. The independent existing Go qualifier retains
its original provenance (`01cf32642ff9`), rather than claiming a fresh Go build.

Q35/nested KVM, host CPU with invariant TSC, one or four vCPUs, 4 GiB, fresh OVMF
variables per boot, modern VirtIO network/RNG/SCSI, RAM-backed home/state. Boot:
`init=boot://boot-init.pxe nvme.write=0 display.timing=off log.level=info`.
Development/network setup is configured on CPU0; Remote allows CPU0 for the
one-CPU boot or CPUs 1/3 for four CPUs. One complete warmup and three retained
fresh boots for each configuration; two independent concurrent Remote sessions
repeat launches/pages in four-CPU boots. The timings do not prove those baseline
launchers actually occupied distinct CPUs. A separate untimed inspection of the
implementation establishes actor placement; it is not timing evidence.

Commands use existing tools:

```text
session boot://lua.pxb -e "for i=1,16 do assert(pyxis.run{'echo','-n'}==0) end; print('launches=16')"
session boot://lua.pxb -e "for i=1,1024 do assert(pyxis.run{'echo','-n'}==0) end; print('launches=1024')"
boot://allocbench.pxe pages --size 65536 --live 8 --rounds 100
session boot://ipcbench.pxe call --size 64 --messages 256 --rounds 100
session boot://ipcbench.pxe call --size 4096 --messages 256 --rounds 100
session boot://ipcbench.pxe send --size 64 --messages 256 --rounds 100
session boot://ipcbench.pxe send --size 4096 --messages 256 --rounds 100
session boot://threadprobe.pxe lifecycle
session boot://threadprobe.pxe tls
boot://cpu-go.pxb
boot://nvim.pxb --headless -u NONE -i NONE "+qall!"
```

Every launch waits for real child completion; each command additionally checks
Remote FINAL with exited/0 and complete drainage. Counts, page releases, IPC
verification and Go's 32-worker total 5,280,000 passed. Neovim supplies the
available larger-image launch/exit control. A failed initial warmup parser and
an unsupported second named Remote listener were preserved and excluded before
retained samples; they do not change the matched workload.

Median (range), milliseconds across three fresh boots:

| Workload | 1 CPU | 4 CPUs |
| --- | --- | --- |
| 1,024 launches, client wall through FINAL | 1152.413 (957.651–1223.291) | 1051.447 (1003.820–1163.890) |
| Private pages, guest elapsed | 19.191 (18.471–28.061) | 18.238 (17.511–20.446) |
| CALL64, summed guest completion intervals | 133.587 (127.553–153.591) | 147.005 (145.133–158.784) |
| CALL4096, summed guest completion intervals | 153.308 (150.197–161.357) | 161.104 (151.555–241.533) |
| Thread lifecycle, client wall | 58.267 (46.275–63.888) | 34.307 (30.586–39.852) |
| TLS lifecycle, client wall | 89.515 (75.446–90.942) | 74.440 (72.597–74.574) |

Concurrent four-CPU launch median 1758.123 (1746.826–2281.867) ms, and pages
24.527 (23.275–28.341) ms, across six worker observations. These are shared-host
nested results: host load was about 9 on 12 CPUs, with unrelated agents' work;
no own build or debugger overlapped a retained timing. End-to-end client wall
includes connection/transport/cleanup and is not phase attribution. IPC intervals
exclude setup/retirement and retain their tool's own warmup. Clock cost is not
subtracted. Native speedup is not established by these numbers.

Small baseline evidence and full hash/command manifest:
`/shared/present/bsp-removal/task4/baseline/`. Kernel SHA-256:
`db23ee2d81f98d864ca8c990067c7d45705e5b5bb52af2f1f49880bbe9d45390`.
Builds and frozen images remain VM-local.

## Implementation qualification

The ordinary four-CPU cohort passes after integration, including two independent
concurrent launchers. A first boot exposed missing shared-owner admission in the
split fixed space phase; a subsequent concurrent run exposed try-only admission
for the small task-stack reservation. Both were corrected before matched after
measurement. Source review corrected departure-CPU accounting independently of
BSP queue order and preserved group attribution around early capture unwind.
These debugging runs are functional evidence, not retained timing samples.

Untimed GDB and existing commands at clean pre-rebase implementation `3093a00b0`
(kernel SHA `5cb9730491462dab18d819cc91bf3ab8efa28e52daf1b66d2e8e379c146954b7`)
confirm simultaneous retained preparation on CPUs 1 and 3, and RAM cleanup on
recorded CPUs 1/3. One process prepared on CPU1 retired on CPU3; both its VM and
record phases ran on CPU3. Copied echo and 32 copied Neovim launches exercise
nonempty external captures (7,845,582-byte image, 7,847,936-byte backing), AP read
closure and confirmed shared retirement. All inspected task/process/prepared/range
queues drained; no retired or quarantined range remained. A rejected second
pipeline stage transferred an intact PREPARING rollback to its preparation CPU.
Existing `threadprobe refusal`, `stop`, `close` and real `exhaust` passed; the last
reached 3,950 regions / 4,093,829,120 bytes, observed allocation/CREATE exhaustion,
then successfully reused native/libc contexts. No deliberate timeout injection.

That focused record predates the selected-group early-capture attribution fix
and the unrelated Wi-Fi rebase; it is not labelled exact final-head qualification.
Small evidence: `/shared/present/bsp-removal/task4/focused-3093/`.

## Matched after measurements

Same frozen archive/config, ordinary flags and collector workloads at clean
`61072fb67c8c`, before rebasing the unchanged task-4 implementation onto the
subsequent RTL Wi-Fi merges. Kernel SHA:
`cffc601c6c635abe88ba911c744f613987a52f2c22df5a5df003925593064c43`.
Both warmups and all six retained fresh boots passed. During the four-CPU series,
an unrelated Mesa build saturated the shared host; no own build/debugger ran.
Raw original before/after timings therefore do not establish a speedup or isolate
a regression. All values below are median (range), ms:

| Workload | After, 1 CPU | After, 4 CPUs |
| --- | --- | --- |
| 1,024 launches, client wall | 1020.136 (906.544–1089.019) | 1365.092 (953.325–1843.157) |
| Private pages, guest elapsed | 19.055 (18.473–19.969) | 18.694 (18.025–23.840) |
| CALL64 completion sum | 139.778 (130.189–162.024) | 919.296 (495.358–1168.250) |
| CALL4096 completion sum | 158.052 (146.236–234.191) | 1055.630 (515.266–6181.114) |

A focused four-CPU A1/B1/A2/B2/A3/B3 rerun retained the same full cohort on each
fresh boot: A is frozen main `af35537d6`, B is `61072fb67`. Shared-host load still
varied; exact loads, raw samples and outcome checks are retained. These are
nested QEMU results, not native results or phase timings:

| Workload | A | B |
| --- | --- | --- |
| 1,024 launches, client wall | 1556.739 (1382.663–6234.505) | 1559.371 (1465.743–2775.680) |
| Private pages, guest elapsed | 21.168 (19.461–26.136) | 19.419 (18.793–21.401) |
| CALL64 completion sum | 210.935 (181.934–345.271) | 583.038 (500.177–912.606) |
| CALL4096 completion sum | 283.986 (205.438–330.632) | 996.526 (497.494–1104.818) |
| Concurrent launches, six client observations | 2379.112 (2233.003–3263.232) | 1296.778 (1057.460–2501.956) |

The four-CPU IPC increase reproduces with nonoverlapping ranges. A separate
untimed GDB comparison attributes a placement mechanism: with BSP preparation,
the sender is parked and both APs appear idle at receiver publication; the
existing parent tie-break initially selects its CPU. With caller preparation,
the sender is running on that AP and the same least-loaded rule selects the
other idle AP. Twelve observed A publications all initially chose the sender CPU,
but existing idle stealing moved six receivers before first dispatch. Twelve B
receivers first dispatched on the other AP. Debugger stops perturb stealing;
these counts do not establish an ordinary placement frequency or quantify the
whole timing increase. The extra cross-CPU traffic explains the direction, and
the native measurement below checks the same IPC workloads. No scheduler scoring, affinity,
migration or priority policy was changed to hide the cost.

Raw after samples/comparison and placement evidence live under
`/shared/present/bsp-removal/task4/`; large artifacts remain local.

## Native handoff

The hands-free vela pair uses clean main `79ccea04f` as A and rebased task-4
code `a83e821b9` as B, so unrelated RTL Wi-Fi and readiness changes are present
on both sides.
Both use identical deliberately frozen workload archive bytes from the original
`af35537d6` / userland `b714efef765` baseline, plus the existing identity qualifier.
Their consumed launch/memory/IPC/system-info ABI is unchanged; this is not claimed
to be a freshly rebuilt newer userland. Compiler/config/flags match. The packet
records AC/HWP/XSAVE/TSC state and requires three interleaved fresh boots each.
Reverse Remote admits one client, so native cohorts are sequential; QEMU supplies
the independent-preparation evidence. The sealed pair is at `/shared/present/bsp-removal/task4/native/`; its numbered
README gives the existing Remote commands and interleaved order. Both exact
four-CPU nested-QEMU collectors pass, including runtime compatibility of the old
identity qualifier and every workload's FINAL/drain checks. This qualifies the
rebased runtime code, not native performance. MACs are masked from saved output;
output-event payloads are omitted from event logs. Both guests/helpers stopped.

A ELF SHA: `33cf1718dd056b6002f13d32ffbe2f23727d089a723049349d58d79530d90ae7`.
B ELF SHA: `8b46292f81bec986fcefc4a1b25f1c49e5f5d9e12dd6b6eace94a356133c0d5d`.
Common archive SHA: `93b4b48660b55d0822545dc2e60b78062fece6f8e8bbac8e86d9e21937fe9704`.
Full config/builder/program identities and qualification evidence are sealed with
SHA256SUMS. Subsequent experiment/WIP documentation commits change no runtime
code from B.

### Native results — vela, 2026-10-28

The orchestrator completed six fresh boots in order A1/B1/A2/B2/A3/B3; every
cohort, outcome/count check and FINAL/drain check passed. Both images used the
PXE default entry with the identical `resolution: 2880x1800x32` line, the same
command line/archive/config/compiler flags and RAM backing. All six recorded AC
online, 16 CPUs with HWP applied on all 16, eager XSAVE/XCR0=7 (832 bytes), and
3686.4 MHz TSC from CPUID 0x15 using RDTSCP+LFENCE. Remote/children retained the
same {1,3} CPU allowance; actual sender/receiver placement was not traced natively.
The orchestrator restored the plain PXE default after the run.

Launch and page values are target-program intervals, not host Remote wall time.
IPC values below are milliseconds per 256-message timed pass: each tool command
runs one warmup and 100 verified timed passes. The table reports the median
(range) across the three per-boot medians. Nested tables above instead sum all
100 completion intervals, so their absolute values must not be compared with
these native per-pass medians.

| Target interval, ms | A | B | Median change |
| --- | --- | --- | --- |
| 1,024 launches | 1050.917 (1048.565–1087.575) | 927.027 (925.158–931.245) | −11.8% |
| Private pages | 25.515 (25.500–25.998) | 25.854 (25.062–25.856) | +1.3%, overlapping ranges |
| CALL64 completion | 1.2350 (1.2015–1.2585) | 1.6370 (1.6355–1.6395) | +32.6% |
| CALL4096 completion | 1.4230 (1.4060–1.4230) | 2.0690 (2.0590–2.0885) | +45.4% |
| SEND64 completion | 0.6365 (0.6205–0.6490) | 0.7485 (0.7460–0.7575) | +17.6% |
| SEND4096 completion | 0.6780 (0.6775–0.6880) | 1.1980 (1.1925–1.2015) | +76.7% |
| SEND64 admission | 0.0740 (0.0710–0.0750) | 0.0920 (0.0910–0.0930) | +24.3% |
| SEND4096 admission | 0.0975 (0.0960–0.0985) | 0.2320 (0.2310–0.2335) | +137.9% |

The native packet includes exactly the nested CALL/SEND 64/4096 commands shown
above, with `--messages 256 --rounds 100`; no additional native IPC run is needed
to establish the performance cost. CALL64 adds about 1.57 microseconds per round
trip and CALL4096 about 2.52 microseconds, from these pass medians. Launch is
faster and page allocation effectively flat, but the IPC increase is real and
must accompany the merge assessment. The QEMU placement investigation explains
a mechanism; it does not quantitatively attribute the whole native difference.
No scheduler scoring/affinity change was introduced or implied by the results.

The client-wall controls also passed: thread lifecycle 35.312 → 34.316 ms,
TLS lifecycle 66.409 → 66.520 ms, Go lifecycle 15.654 → 14.824 ms, and larger
Neovim launch/exit 33.434 → 28.424 ms. These include transport and cleanup;
they are not target-only phase timings.

Native evidence: `native/orchestrator-runs.txt`, `native/comparison.json` and
`native/results/` under `/shared/present/bsp-removal/task4/`. The run log identifies
all six staged boots and the restored default; comparison preserves individual
samples and the common environment checks. No boot-time/multi-client scaling,
physical-device cleanup stress or native uncertain-ACK fault claim is added.

## IPC regression attribution

The native regression blocks merge; chronium requires no material IPC regression
against main (2026-10-28). A launch gain does not offset this cost.

A configuration-only nested-KVM isolation reused the exact final A/B kernels and
archive above, changing only Remote's allowed CPUs from `{1,3}` to `{1}`. All four
vCPUs remain online; Development/network remains on CPU0. Each configuration has
one discarded warmup boot per variant and three retained interleaved A/B pairs.
No debugger runs during timing. Host one-minute load was 0.46–1.10. Existing IPC
commands/counts/outcomes and final drainage all pass. Values are median (range)
of summed 100-pass guest intervals, milliseconds:

| CALL completion | Default `{1,3}`, A → B | CPU1-only, A → B |
| --- | --- | --- |
| 64 bytes | 138.928 (133.817–145.792) → 448.546 (446.988–455.123) | 121.346 (119.861–123.981) → 119.027 (118.351–119.348) |
| 4096 bytes | 144.873 (143.483–165.856) → 473.370 (471.741–477.173) | 138.952 (137.292–142.520) → 132.989 (132.715–136.272) |

SEND completion penalties likewise change from +56.2%/+69.0% with `{1,3}` to
−4.2%/−4.2% with CPU1-only. Separate untimed GDB observations find both peers on
CPU1 in all 12 samples per variant under that restriction. With `{1,3}`, all 12
B samples run the receiver on the other AP; A has five same-CPU and seven
cross-CPU first dispatches. Debugger stops can affect idle stealing; these are
not natural placement frequencies. The unchanged-check control identifies
placement as a material cause, not a per-check cycle cost or a native fix.

Source and exact-artifact disassembly audit finds no new unconditional per-user-
message guard. The two RAM-worker wait checks are bypassed by the existing user-
task branch. VM dispatch/syscall/borrow, endpoint and wakeup guards protect root
drainage, user-copy lifetime, delivery/cancellation and queue ownership as before.
No new capture ACK, pin/unpin or process-retirement fence enters this timed loop:
the benchmark launches its receiver from initrd, prepares it before the clock
bracket, and stops/joins it afterward. The root-depart pending-service check
actually loses its THREAD case. Existing wakeups can now require a remote IPI
instead of a local queue operation; endpoint/queue cache ownership can cross CPUs.
Those mechanisms explain the source-level connection to placement, but their
individual costs have not been separated by the available tools. In particular,
equal instruction counts and improved CPU1-only totals do not prove zero guard
cost. No check was removed or redundant-check speedup claimed.

Reports, exact source references, disassembly counts, hashes and small raw
captures: `/shared/present/bsp-removal/task4/ipc-isolation/`. The default remains
unfixed in these measured artifacts; the accepted
[placement rule](../../wip/bsp-removal.md#initial-placement-after-caller-preparation--accepted)
discounts only the executing user preparation actor and awaits matched
qualification. Fixed RAM owners and confirmed capture retirement
do not inherently require placing ordinary IPC peers on different CPUs.

### Accepted placement correction

At clean signed `4c4b6625e398874309402f49471f34d130600c39`, initial
process/thread placement discounts only the executing user preparation caller's
one running contribution. Both candidate load and sibling-idleness use that
view. Actual scheduler accounting, queued batch children, affinity and idle
stealing are unchanged. Boot, fixed-worker and normal scheduler rebalancing
receive no discount. No foreign CPU's current-task pointer is read, and no IPC
guard is removed. Chronium accepted this scoring rule on 2026-10-28.

Ordinary kernel build and exact CI4799 pass. B ELF SHA:
`91d700ea6703a73228bcb2e438ec3f754779cdfb6cb30c3d2d1471673263c0e8`.
A remains main `79ccea04f`, with the same ELF/archive/config/builder as above.
Normal `{1,3}` allowance, four-vCPU nested KVM; one discarded warmup per variant,
then three interleaved fresh A/B pairs, reversing order in pair 2. Existing
commands/counts/FINAL drainage pass. No debugger during timing. Summed 100-pass
guest intervals, milliseconds, median (range):

| IPC interval | A | Corrected B |
| --- | --- | --- |
| CALL64 completion | 286.930 (176.334–849.457) | 144.261 (143.010–160.912) |
| CALL4096 completion | 338.826 (189.170–500.547) | 219.540 (170.749–221.849) |
| SEND64 completion | 100.080 (77.989–108.352) | 79.460 (68.231–92.587) |
| SEND4096 completion | 116.562 (95.863–128.003) | 84.933 (77.624–111.454) |
| SEND64 admission | 10.913 (8.715–11.709) | 8.871 (7.504–10.617) |
| SEND4096 admission | 19.884 (17.002–23.217) | 16.019 (13.380–20.991) |

The old repeated threefold CALL penalty is absent; these medians are not a
claimed large speedup. A has substantial variation and a CALL64 outlier; the
first pair's CALL4096 and SEND completion intervals are higher in B. Host
one-minute load was 1.13–2.57. Single additional full-cohort observations give
1,024 target launches A1522.999/B1142.873 ms and pages A18.633/B18.275 ms;
they do not replace a repeated native comparison. Both full existing cohorts,
including thread/TLS/Go and Neovim, and both actual sealed-packet CLI collectors
pass with confirmed final drainage. Separate GDB placement observation is untimed.

Small reports and raw evidence:
`/shared/present/bsp-removal/task4/ipc-after-discount/`. The new sealed pair
`/shared/present/bsp-removal/task4/native-placement-fix/` preserves A and workload
bytes, fixes B at the exact hash above, and keeps the six-boot A1/B1/A2/B2/A3/B3
shape and identical `resolution: 2880x1800x32`. It performs no staging itself.
Native IPC at main's level with the launch gain retained remains the merge
requirement; the corrected native result follows.

### Corrected native result — residual SEND cost

The orchestrator completed all six fresh placement-fix boots on vela, all PASS,
at the exact A/B hashes above. Target launch median 1032.882 → 873.793 ms
(−15.4%); private pages 25.418 → 25.581 ms, overlapping ranges. IPC milliseconds
per 256-message pass, median (range) across three boot medians:

| Interval | A | Corrected B | Change |
| --- | --- | --- | --- |
| CALL64 completion | 1.2430 (1.2270–1.2905) | 1.2595 (1.2510–1.2700) | +1.3% |
| CALL4096 completion | 1.4110 (1.4060–1.4135) | 1.4565 (1.4475–1.4765) | +3.2% |
| SEND64 completion | 0.6225 (0.6190–0.6235) | 0.6805 (0.6770–0.6835) | +9.3% |
| SEND4096 completion | 0.6795 (0.6790–0.6795) | 0.7550 (0.7525–0.7640) | +11.1% |
| SEND64 admission | 0.0720 (0.0720–0.0730) | 0.0790 (0.0785–0.0795) | +9.7% |
| SEND4096 admission | 0.0965 (0.0960–0.0990) | 0.1235 (0.1215–0.1295) | +28.0% |

Chronium assesses CALL as parity; the nonoverlapping SEND cost is not an accepted
trade-off. Paired per-pass completion-minus-admission medians leave about
49.5/50.0 microseconds additional cost outside admission for 64/4096 bytes.
That is about 1.55 microseconds per eight-message group, but this remainder is
not a separate DRAIN timer. The native packet's `comparison.json` and six result
directories retain full samples. Merge remains blocked pending explanation.

### Residual investigation — 2026-10-29

Read-only source audit and separate untimed four-CPU GDB observations find:

- Both variants can migrate SEND receivers after initial placement. A fully
  split sample makes 32 remote notification requests per direction for its
  32 DRAIN control calls. No such request occurs during raw SEND admission;
  data is queued in groups of eight before DRAIN. These stops do not establish
  natural migration frequency or delivered/coalesced interrupt counts.
- In a further bounded B64 observation, no RAM actor was current/ready at six
  stable publication snapshots or the sampled SEND/control points. Two of 192
  DRAIN snapshots could not safely traverse a remotely held readyqueue; other
  actor/pending observations were empty. All 15 observed RAM-actor yields were
  in STOP cleanup with local queued=0. This supplies no causal basis for changing
  the cleanup yield or process WAIT contract.
- No added per-message pin, capture ACK, endpoint/copy/timer path or new guard
  explains the residual. Existing native identity samples identify P-core APICs,
  not the IPC peers' CPU histories or instantaneous frequency. No CPU-frequency
  or SMT explanation is established.

A fresh exact-kernel configuration control uses the existing IPC commands with
normal `{1,3}` versus CPU1-only. Four discarded warmup boots precede three pairs
per configuration. A concurrent compiler burst contaminated the first retained
pair of each initial series; all raw samples remain, and separate pairs 2/3/4
replace those preidentified contaminated pairs. The replacements had 13.2–13.9%
host busy, no own debugger/build, and an unrelated idle QEMU co-tenant. Summed
100-pass completion intervals, milliseconds, median (range):

| Control | Normal A → B | CPU1-only A → B |
| --- | --- | --- |
| SEND64 | 69.095 (67.195–70.086) → 66.676 (64.405–68.344) | 64.016 (62.509–65.292) → 62.113 (61.894–63.040) |
| SEND4096 | 78.980 (75.313–80.506) → 71.198 (69.024–72.502) | 69.809 (69.136–70.878) → 67.639 (67.573–69.030) |

The persistent native residual does not reproduce in these controls. That does
not dismiss it or establish a fix. No justified kernel change follows yet.
Small source, observer and timing evidence is under
`/shared/present/bsp-removal/task4/send-residual-20261029/`. The next native
diagnostic compares the same kernels under normal, CPU1-only and CPU3-only
allowances to distinguish CPU/placement effects; it changes no product policy.

### Native CPU controls and copy-path exclusions

The orchestrator completed the six diagnostic boots in
`/shared/present/bsp-removal/task4/native-send-cpu-control/`, all PASS. Each
profile has one A/B pair; these are descriptive controls, not three independent
repeats of one configuration. Both kernels and all non-configuration workload
bytes remain unchanged. Native completion/admission medians, ms per pass:

| Profile | SEND64 A → B | SEND4096 A → B | SEND4096 admission A → B |
| --- | --- | --- | --- |
| `{1,3}` | 0.6475 → 0.6775 | 0.6990 → 0.7655 | 0.1005 → 0.1275 |
| `{1}` | 0.6120 → 0.6215 | 0.6815 → 0.7270 | 0.0980 → 0.1320 |
| `{3}` | 0.6040 → 0.6180 | 0.6730 → 0.7250 | 0.0960 → 0.1310 |

The fixed-CPU 4 KiB admission increase is 34–35 microseconds per 256 messages,
about 133–137 ns/message, accounting for most of the completion increase.
Cross-CPU placement is not the principal explanation. Launch remains about
18% faster in all three pairs, pages flat and CALL near parity.

Exact frozen A/B disassembly of syscall entry/dispatch, endpoint admission,
copy/lease helpers and page walks matches after relocation normalization. Both
CALL and SEND directly copy user payload to the delivery record, with no extra
SEND temporary or pin. Record stride 9,336 bytes and request offset 1,144 are
unchanged. In separate ordinary four- and 16-vCPU GDB controls, sampled source
offset is 64; destination slot offsets are 1392/2536/3680/728/1872/3016/64/1208
in both variants. The receive destination offset is 3016 in both. Equal-offset
copy cases therefore occur in both, not uniquely B. All sampled 16-CPU pages
select PAT0/PCD0/PWT0 (write-back), with no physical source/destination overlap.
The first B physical frame bases are displaced by 256 KiB; this is a layout
observation, not evidence of a cache penalty. Native live addresses remain
unobserved. Stops establish neither copy latency nor cache-miss rates.

A further existing-tool control keeps Remote on CPU1 and varies online vCPUs
4 versus 16, matching the native reaper count at 16. One discarded warmup per
kernel/count and three interleaved pairs each all pass, with no own debugger or
build during timing. Host has 12 CPUs; 16-vCPU scheduling is oversubscribed, and
early samples vary. SEND4096 summed 100-pass completion medians A/B are
72.184/69.409 ms at four CPUs and 73.967/73.444 ms at 16; admission medians are
12.162/11.399 and 13.525/12.538 ms. No native-like B penalty appears. These
controls exclude no native cache effect and do not establish a speedup.

Small exact reports and raw evidence:
`/shared/present/bsp-removal/task4/send-copy-20261029/`. No justified kernel fix
has emerged. Existing native debugging rejects running break-in/breakpoints,
and the current profiler has no IPC phase events. Chronium subsequently
allowed the temporary native phase measurement below. No guard was removed or
speculative copy rewrite made.

### Native SEND phase result

Chronium approved bounded local-only timing (2026-10-29), never pushed or
merged. A is main `c65430c61e`; B is task 4 cleanly restacked onto it
(`fc0a97e67b`, runtime equivalent to the task-4 patch in `4c4b6625e`). Common
builder `8bdf136`, INFO, config `106bf0ae`, CPU1-only RAM archive `6b2123b4`,
2880×1800 PXE, AC/HWP/XSAVE/RDTSCP-TSC matched. UDP log is off in all cells;
existing LOG snapshots collect ktrace outside timed work. The orchestrator
completed eight fresh vela boots and restored the plain default. All pass
payload/cardinality/FINAL-drain checks. Each row is one independent A/B pair,
not four repetitions; medians of the existing 100 passes, ms per 256 messages:

| Observer mode | SEND4096 completion A → B | Admission A → B |
| --- | --- | --- |
| Uninstrumented | 0.6675 → 0.7310 (+9.5%) | 0.094 → 0.132 |
| Probe disabled | 0.6825 → 0.7215 (+5.7%) | 0.097 → 0.128 |
| Counters only | 0.6695 → 0.7280 (+8.7%) | 0.096 → 0.133 |
| Sampled | 0.6715 → 0.7230 (+7.7%) | 0.098 → 0.132 |

The sampled pair uses identical code/layout to the two probe controls within
each variant, changing only its mode data. Ordered ticks sample every 67th raw
SEND: 385 complete samples out of 25,856 verified 4 KiB sends per variant,
including warmup. All eight delivery slots, exact phase telescoping, histogram
bins/overflow and zero failures are checked. TSC 3686.4 MHz; means in ns include
marker overhead. The immediate bracket (12.39 → 12.32 ns) is not subtracted:

| Adjacent phase | A | B | B − A |
| --- | --- | --- | --- |
| Descriptor validation | 70.96 | 70.36 | −0.60 |
| Reservation/pre-copy | 45.81 | 39.40 | −6.40 |
| Lease acquire | 30.68 | 30.84 | +0.16 |
| Page check | 24.41 | 23.81 | −0.60 |
| Payload copy | 109.17 | 142.48 | +33.31 |
| Lease release | 34.54 | 108.99 | +74.45 |
| Remaining admission | 42.10 | 54.83 | +12.73 |
| Total instrumented admission | 357.66 | 470.70 | +113.05 |

Copy plus release accounts for about 95% of the net sampled phase increase.
The release median is in the same 64–127-tick bin in A/B; B has 10 of 385
samples above 4096 ticks, versus zero in A. Histogram bounds show those ten
contribute 67.68–84.33 ns to B's per-sample mean: the mean increase is mainly
rare tails, not a uniformly slower check. Copy has five such overflow samples
in B, none in A; total admission has 12, none in A, with the same median bin
(1216–1279 ticks). Aggregates cannot correlate individual phase outliers.

Native source offset 64 and every slot's destination low12 match A/B and stay
stable. Exact release instructions, including the existing byte XCHG on the
VM guard, borrower decrement and unlock, match. No extra guard/pin/fence or
ACK is introduced. The locked acquisition after the copy can also pay pending
store completion ([Intel SDM, volume 3A, §9.2.5](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf));
its placement in the release interval does not identify which instruction stalled. The result establishes a tail-heavy data-movement/
release cost, not a proved contention/cache/SMI cause. Uninstrumented admission
medians add 148 ns/message; sampled phase means add 113 ns, but systematic
samples plus warmup and phase-boundary differences are not an exact accounting
of median whole-pass time. Receiver/drain work is not instrumented.

Task 4 does not inherently require this extra per-message work. No particular
corrective change is established by the data, so no guard removal, copy rewrite
or performance waiver follows; merge remains blocked. A focused distinction
between release-lock acquisition/body and explicit store completion is proposed
in the [remaining plan](../../wip/bsp-removal.md#native-send-phase-result--instrumentation-removed),
not implemented. Local timing code/branches and experiment images are removed;
only small evidence/methods remain. Source/product runtime is unchanged.

Evidence: `/shared/present/bsp-removal/task4/native-send-phase/` contains
`comparison.json`, raw results and stage/reboot records. Ordinary final QEMU
controls and host snapshot analysis pass; their variable timings do not explain
this native residual. The directory is retired as a deployable packet.

## Storage and remaining limits

Artifact type inspection: task 840 bytes, process 432 bytes (baseline 392),
request area 6,592 bytes (baseline 6,560), profile 776 bytes. Each mandatory fixed
RAM actor adds a 16 KiB stack and 840-byte task record per online CPU. Queue/owner
arrays add 88 bytes per CPU: RAM-process owner 48, launch queue 16, captured-range
queue 16 and task-queue head 8. That is 17,312 bytes per CPU excluding allocation
headers/page tables; no free-frame cache is introduced. These sizes are build
observations, not ABI contracts.

Device callbacks, endpoint weak lists and actual object destructors retain their
fixed owners. Shared kernel range admission can still serialize the small stack
and captured-range portions. Process/thread completion follows actual RAM work;
groups additionally join attributed deferred device/object cleanup. A failed
shared capture acknowledgement retains VA, backing and group cleanup until
reboot, with no late-ACK retry; intentional timeout injection is not claimed as
qualified by ordinary runs. Task 5's remaining consumer migration is separate.
