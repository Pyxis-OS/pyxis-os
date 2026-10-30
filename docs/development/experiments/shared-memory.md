# Shared-memory baseline and qualification

## Before-code controls — 2026-10-29

Clean main `20c99b3fa89412b0a108a7c38b428c6593e63fa0`, before shared-memory
implementation. Kernel SHA-256
`5e41933334d6f2dd9deb6d93f7ed448ead257bb50152935e75e40573e593c72d`;
effective config `106bf0ae11de93dc8269178a724abdce370cb1a92164690059774b0c69a3cc8e`.
Published compiler `pyxis-llvm23.1.3-8bdf136`; kernel built with
`make -j16 kernel LOG_LEVEL=info` in that container. No instrumentation.

QEMU 10.2.2, nested KVM/q35, `host,+invtsc`, 4 GiB, UEFI, virtio-scsi CD,
virtio RNG/network and localhost Remote; no disk or native/PXE action. Runtime
clock is KVM with shared monotonic floor; HWP unavailable; eager XSAVE `0x7`,
832 bytes. Development/network use CPU0; Remote/children are allowed CPU0 for
the 1-vCPU boot and CPU1 for the 4-vCPU boot. This is not multi-CPU scaling or
proof of actual execution on a particular AP.

The selected existing app bytes are intentionally frozen for later controls,
not rebuilt or claimed to be today's full userland: userland `8cefca401776`,
ports `18b56bf05ec2`, SDK source `397a22bffdd8`, same compiler. Selected bytes
match their original component checksum lists. Relevant MEMORY/IPC/launch/
system-info ABI headers are byte-identical. The older LOG reader is incompatible
with source-stamped records and excluded; the other differing header is unused
Wi-Fi. Exact member/archive/ISO hashes and component records accompany results.
The 1/4-CPU archives differ in the declared Remote CPU allowance.

The existing collector, launch loop and parsers run these unchanged workloads:

- 16-launch warmup, then 1,024 verified `echo` launches through retirement.
- `allocbench pages --size 65536 --live 8 --rounds 100` (800 attempts/releases).
- `session ipcbench {call,send} --size {64,4096} --messages 256 --rounds 100`,
  one warmup then 100 measured passes per case, with exact message/byte counts.

All workloads and FINAL drains pass. Guest elapsed intervals are separate from
Remote wall time. One successful timed boot per configuration; IPC cells are
median [min,max] milliseconds of passes within that boot, not independent boots.

| Guest interval | 1 vCPU | 4 vCPUs |
| --- | --- | --- |
| 1,024 launches, ms | 791.802 | 786.522 |
| Private pages, ms | 18.290 | 17.474 |
| CALL64 completion, ms | 1.270 [1.250, 2.199] | 1.223 [1.214, 1.266] |
| CALL4096 completion, ms | 1.432 [1.385, 1.961] | 1.367 [1.339, 1.408] |
| SEND64 completion, ms | 0.655 [0.638, 0.718] | 0.651 [0.635, 0.667] |
| SEND4096 completion, ms | 0.709 [0.681, 0.771] | 0.7045 [0.675, 0.757] |
| SEND64 admission, ms | 0.077 [0.076, 0.095] | 0.077 [0.076, 0.092] |
| SEND4096 admission, ms | 0.107 [0.100, 0.163] | 0.108 [0.101, 0.131] |

This establishes no boot-to-boot variation, regression verdict or shared-memory
functionality. Three untimed setup failures (omitted session/Remote assets,
missing timezone data and incompatible old LOG reader) are preserved separately;
no timed workload began in them. Boot identity uses the exact staged kernel,
ISO, configuration and QEMU command plus serial captures, not a guest revision
query. Both owned guests stopped.

Small evidence: `/shared/present/shared-memory/baseline/`. Frozen local build,
inputs, existing collector adaptation and full raw records:
`/home/chronium/build/shared-memory/baseline/`. Reproduction uses
`python3 run_baseline.py 1` or `python3 run_baseline.py 4` from that directory
with fresh output paths. Keep identical control bytes for matched after-runs;
no repeated runs merely to increase sample count. Shared-memory behavior and
native qualification remain tasks of the [milestone](../../wip/shared-memory.md).

## Refreshed common base after process listing — 2026-10-29

Before any shared-memory kernel edit, main moved through #1605/#1606 to
`8d4f8b3d1bd75e91dc670d0a2a54c6cb0bef432b`. Process/space metadata and
launch/retirement accounting changed, so this is the comparison baseline for
implementation, not a speedup comparison against the earlier run.
Kernel SHA-256 `5a1face2ea253a4a3ddfdc8e9b518318c3ffa5caf60bea595315a940e47085ae`.
Same compiler/effective kernel config and exact frozen control archives,
Remote client and parsers; MEMORY/IPC/launch/system-info ABI remains compatible.
The new process-view protocol/syscall/header is additive and unused here.

One fresh successful boot each at 1/4 vCPUs, same QEMU/workload configuration;
all counts and FINAL drains pass. No startup failure on this refreshed base.
IPC intervals remain within-boot median [min,max] milliseconds:

| Guest interval | 1 vCPU | 4 vCPUs |
| --- | --- | --- |
| 1,024 launches, ms | 843.982 | 825.786 |
| Private pages, ms | 18.137 | 17.778 |
| CALL64 completion, ms | 1.266 [1.251, 1.579] | 1.230 [1.215, 2.662] |
| CALL4096 completion, ms | 1.421 [1.387, 1.720] | 1.381 [1.340, 1.678] |
| SEND64 completion, ms | 0.654 [0.636, 0.707] | 0.650 [0.632, 0.959] |
| SEND4096 completion, ms | 0.710 [0.680, 0.809] | 0.702 [0.677, 1.264] |
| SEND64 admission, ms | 0.077 [0.076, 0.096] | 0.078 [0.077, 0.128] |
| SEND4096 admission, ms | 0.106 [0.100, 0.177] | 0.110 [0.102, 0.223] |

Both guests stopped; source clean. Earlier 20c results/inputs remain intact.
No boot-variance, regression or native claim. Small refreshed evidence:
`/shared/present/shared-memory/baseline-8d4f/`; frozen local images/source and
full raw records: `/home/chronium/build/shared-memory/baseline-8d4f/`.

## Task 2 storage foundation — nested controls

Candidate `33a0f66f726016ffbc7eeb6db5ab7af5e089a21e`, kernel SHA-256
`97b954938b0e897ea0852194a5588473ca846f3c374f79a4935f572893caff5c`,
same effective config/compiler and exact frozen apps/archives as the 8d4f base.
Ordinary kernel build passes without compiler diagnostics. Source review checks
zero-before-publication, all-prefix failure unwind, retained origin references,
queue handoff and replacement cleanup-token ownership. Creation/mapping authority
is not delegated; the controls do not exercise new region allocation or views.

One matched boot/config passes all counts and FINAL drains. Launch A→B is
843.982→836.481 ms at 1 CPU, 825.786→817.009 ms at 4 CPUs; pages change
+1.49%/−0.13%. Four-CPU IPC completion medians change −0.85% to +1.28%, with
overlapping ranges. One-CPU CALL4096 increases 1.421→1.4865 ms (+4.61%) and
SEND4096 0.710→0.7375 ms (+3.87%); CALL4096 maximum grows 1.720→3.024 ms.
These differences prompted one contemporaneous focused 1-CPU A/B, not more
runs to improve a result. All samples remain separate and retained.

| Focused 1-CPU completion, ms | A median [min,max] | B median [min,max] |
| --- | --- | --- |
| CALL64 | 1.2755 [1.257,1.831] | 1.3570 [1.276,1.870] |
| CALL4096 | 1.4690 [1.417,2.868] | 2.0035 [1.506,2.576] |
| SEND64 | 0.6540 [0.641,0.794] | 1.2645 [0.646,2.275] |
| SEND4096 | 0.9275 [0.746,1.260] | 0.7625 [0.722,1.406] |

The focused pair passes exact counts and drains but shows material mixed
changes: CALL4096 rises 36%, SEND64 93%, while SEND4096 falls 18%. It does not
establish a stable 4 KiB-only effect or a cause. Do not label the differences
noise or claim performance parity. No further timing repeats or speculative
padding/fence/copy changes are justified by these results. Source/disassembly
attribution found no added per-message checks in the inspected IPC hot paths;
the native control pair below resolves the native regression concern without
establishing the cause of these nested differences.

Backing metadata is 88 bytes per originating account and 72 bytes per region
plus its 8-byte physical-frame entry per page, excluding allocator overhead.
A process gains one 8-byte lazy origin pointer; a space two atomic byte counters.
The existing RAM actor gains a 24-byte per-CPU queue, not a new worker. Its empty
checks use the atomic queue head without taking the list guard. No region/create
operation is added to existing IPC paths.

All owned guests stopped. Exact methods, per-pass records/ranges and host
context: `/home/chronium/build/shared-memory/task2/{after,focused-1cpu}/`;
small evidence `/shared/present/shared-memory/task2/`.


## Task 2 native controls — 2026-10-29

The orchestrator ran one fresh A1/B1 pair on vela: A main
`4716f61e14703d21d2a7f58f6ef097d5db974bad`, B task 2
`204f12e7a3205ef13698ac8e900aaa36ca55613d`. Both pass every workload count and
Remote FINAL/drain. Same compiler, effective kernel config, frozen control apps
and RAM archive; no instrumentation. Both report 16 online CPUs, full HWP,
XSAVE `0x7`/832 bytes and TSC `cpuid15`/`rdtscp` at 3686.4 MHz. Development/
network allow CPU0 and Remote/children CPU1; this does not prove actual placement.
The commands are the unchanged launch/pages/CALL/SEND workloads above. Fresh
boot sequence is reported by the orchestrator; qualification captures exact
kernel identity before and after each cohort.

| Guest interval, ms | A | B |
| --- | --- | --- |
| 1,024 launches | 866.979 | 863.484 |
| Private pages | 25.061 | 25.076 |
| CALL64 completion | 1.2295 [1.143, 1.370] | 1.2295 [1.143, 1.359] |
| CALL4096 completion | 1.4300 [1.336, 1.522] | 1.4260 [1.338, 1.561] |
| SEND64 completion | 0.6445 [0.587, 0.808] | 0.6385 [0.592, 0.812] |
| SEND4096 completion | 0.7460 [0.645, 0.901] | 0.7450 [0.673, 0.918] |

IPC entries are median [min,max] across 100 passes in each boot. Launch/pages
have one sample per variant; there is no independent boot-to-boot range.
The measured native controls are at parity: the material nested IPC changes
did not reproduce. Their cause remains unattributed, and the nested samples
are retained. No speculative hot-path change was made.

This qualifies the storage foundation's existing-path controls for review,
not shared-region constructor, mapping or revocation behavior: CREATE/MAP and
creation delegation remain absent. New-region runtime qualification follows
with the connected mapping/withdrawal and consumer tasks. The follow-up commit
records these results and changes documentation only; the tested B kernel is
identified above. Packet, hashes, per-pass samples and comparison:
`/shared/present/shared-memory/task2/native-controls/` (`comparison-native.json`,
`results/A1-20261029T172044/`, `results/B1-20261029T172217/`).


## Task 3 mapping and withdrawal — 2026-10-30

Before-code source is task 2 native B above; its documentation-only successor
`7085662e7ac6` was the task-3 branch base. The refreshed matched control A is
main `f7ec0f7d4b20`. Final code was exercised at `7516edb48090`, then rebased
linearly onto main `fd2e66e407` and built at `c8820a7e7504`; that rebase changes
no kernel/arch/ABI implementation. Later MEMORY-header changes preserve the
released comment lines for the textual ABI gate and change no layout or code.
Same published `8bdf136` compiler; ordinary kernel builds pass. No compiler
rebuild or dependency pin change is needed.

### Functional and ownership checks

Local native-ABI consumers use the existing SDK/runtime with current public
header declarations. Relevant existing ABI inputs are checked against the
frozen runtime; this is not a full current-userland build. The qualification
archive substitutes its own boot-init executable, whose ordinary trusted-root
MEMORY declaration explicitly grants CREATE_SHARED. It gains no authority from
a wrapper. Ordinary installed programs receive no new creation grant.

Uninstrumented 1/4-vCPU KVM/q35 boots, 4 GiB RAM, pass zeroed rounded padding,
R/RW attenuation, malformed-request refusals, distinct aliases with real
release/acquire communication, IPC/launch transfer, handle-close and creator-exit
lifetime, creation-source lineage, RETARGET preserving the old mapping, and
private-memory/guarded-stack refusal. Actual RO and withdrawn loads fault in
disposable children. Explicit/self CUT, automatic controller close/exit,
independent branches and natural MAP/CUT overlap pass. Four-CPU source/target
execution is observed on APIC 0/1 under a CPU1-only target-space ceiling.
A real C11 worker's finite 256 MiB private allocation also completes across CUT;
that result alone does not identify its kernel VM phase at CUT entry.

A focused finite 3 GiB private allocation exercises real OUTCOME_UNKNOWN with
the ordinary one-second CUT budget. Read-only GDB attaches only after that
result: it observes CPU1 still completing the preceding private MEMORY operation,
external withdrawal queued and termination requested. Actual process WAIT then
reports TERMINATED. A later snapshot, after process completion, retains exactly
one quarantined 8 KiB region and its 8 KiB origin/space charge, without a process
or group cleanup join. Late private-operation progress does not release it.
No lost CPU, ACK suppression, register write or kernel fault hook is used.

Separate read-and-continue accounting observations at clean `1646bf41c5bb`
record 20 view disposals, 11 actual region/frame releases (22 pages), and two
origin destructions. Aliases retain one physical charge; creator retirement can
precede recipient MAP; normal final alias/physical/record gauges reach zero.
A 2 MiB+1 variant at `7516edb48` observes private table pages 8→9→10 as two
aliases map. Both unmaps leave those empty tables counted at 10; teardown
reaches the root-only count 1 immediately before actual root free. Counters
exposed a pre-existing PDPT leak at the slot-511/PML4 boundary, fixed in this
change; the original genuine panic frame and failed receipt are retained.

### Matched existing-path controls

Same frozen control applications/1- and 4-CPU archives and workloads as above;
effective config SHA-256
`524590b0d7a5e81e739a9de9517a9784839d0dd499e2f7291b18f0ddeec81ba9`
on both variants. An inherited disabled config line accounts for the change
from the older baseline config. Uninstrumented final B kernel at `c8820a7e7504`
is compared with saved clean A `f7ec0f7d4b20`; A was captured before B, not
interleaved. All message/byte/launch counts and FINAL drains pass. One boot per
variant/configuration; IPC values are within-boot median [min,max], not
independent boot variation.

| Guest interval, ms | 1 CPU A | 1 CPU B | 4 CPUs A | 4 CPUs B |
| --- | --- | --- | --- | --- |
| 1,024 launches | 896.925 | 796.759 | 859.623 | 787.083 |
| Private pages | 19.207 | 18.475 | 17.778 | 18.708 |
| CALL64 completion | 1.280 [1.262, 2.420] | 1.274 [1.256, 1.332] | 1.232 [1.216, 1.659] | 1.240 [1.211, 2.281] |
| CALL4096 completion | 1.627 [1.437, 2.358] | 1.433 [1.394, 1.554] | 1.399 [1.363, 2.238] | 1.445 [1.352, 2.046] |
| SEND64 completion | 0.727 [0.637, 1.144] | 0.656 [0.637, 0.714] | 0.648 [0.630, 1.243] | 0.651 [0.633, 1.128] |
| SEND4096 completion | 0.733 [0.682, 1.265] | 0.738 [0.701, 0.834] | 0.719 [0.685, 1.215] | 0.717 [0.679, 0.803] |

Final IPC medians change −11.9% to +3.25%, with overlapping ranges in every
cell. Private pages change −3.8%/+5.2% (one elapsed sample each); launch changes
−11.2%/−8.4%. These nested differences do not establish a stable speedup or
native parity. The preceding B `1646bf41c5bb` controls, before private-table
accounting and the PDPT fix, remain separate: four-CPU IPC changes −0.9% to
+1.0%, launch +2.5% and pages +4.0%. No speculative IPC hot-path change was made.
Task 5 retains matched native controls with the declared consumer.

### Results and limits

After actual uncertain-target WAIT completes, an uninstrumented final-code
check repeats CUT and still receives OUTCOME_UNKNOWN. MAP through the cut holder
is DENIED; a fresh MAP through the independent original grant succeeds and its
bytes remain readable. Quarantine does not globally revoke independent branches.

No injected lost CPU/ACK, allocator failure, generation exhaustion or complete
stale-physical-frame-reuse proof is claimed. Native new-region behavior, SDK/
general grant integration and the application shared-buffer cost remain later
tasks. Earlier archive-format and observer setup failures remain failed receipts;
the fixed PDPT assertion is not relabelled as a passing run. A post-destruction
GDB print attempted to read an already-cleared address-space pointer and detached;
preceding table/free observations and the functional result completed normally.

Local provenance, immutable consumers, exact kernel/config/ISO hashes and raw
receipts: `/home/chronium/build/shared-memory/task3/` (`qualification/final/`,
`accounting/`, `table-observation/`, `q3g-observe-4cpu-{2,3}/`, `sticky/` and
`controls/{A,B,B-final}/`). Small handoff summary is under
`/shared/present/shared-memory/task3/`. All owned QEMU/debugger jobs stopped;
no native/PXE action or new compiler container.
