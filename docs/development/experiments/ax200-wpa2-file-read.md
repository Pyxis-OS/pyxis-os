# WPA2 FILE read cost attribution

2026-10-22. Native ThinkPad A=`d2cf7091`, B=`4a768238` (userland `afa8836`,
ports `96a0cdf0`), existing builder `3d69e83`. Three interleaved A/B pairs,
12 CPUs, TSC; identical effective live/network configuration and Kconfig,
DEBUG ceiling/INFO console/UDP on. READ/SCAN-only, no connection or credential.
The orchestrator reported horse busy with agent VMs. The timed FILE payload is
local to the ThinkPad; TCP fixtures and bootstrap depend on horse.

Shipped command, repeated three times per cohort, warmup plus ten measured reads:

```text
bin://iobench.pxe read boot://share/iobench.bin --bytes 1048576 --buffer 4088 --rounds 10
```

| Native phase (90 samples/side) | A median [min,max], us | B median [min,max], us |
| --- | --- | --- |
| OPEN | 5.781 [5.631,13.736] | 5.801 [5.610,22.722] |
| Payload | 209.749 [204.314,403.447] | 475.022 [471.003,753.122] |
| Whole | 217.128 [211.466,420.469] | 484.339 [480.161,779.611] |

Every cohort reproduced the payload separation. Extra payload time is about
265 us, or 1.03 us per data READ. Observed TCP B/A ratios were TX8k=1.009,
TX2k=1.005, RX=0.999; these are not a quiet-host TCP requalification.
Native records: `wifi-ax200-stage4/wired-matched-before-fix/results/summary.json` under
`/shared/present/`.

## Cause and isolation

Userland commit `fa2bb896` in the WPA2 stack added an unconditional full reply
scratch erase to `lib/file.c:file_read`. `libc/memory.c:memset_explicit` clears
through volatile byte stores. Each READ erases all 4,096 bytes, even on a short
read or failure; the 257 data reads add 1,052,672 explicit byte stores per MiB.
The EOF check adds one more erase outside the payload interval. This was a generic per-read wipe of caller-owned scratch, adding cost to every FILE.
The benchmark source is unchanged between A and B.

Diagnostic nested QEMU: q35/KVM, CPU max, four CPUs, 4 GiB, kernel B fixed,
same public boot FILE and read-only virtio-fs program launch. Existing iobench
objects/SDK were linked with two scratch copies of the FILE helper differing
only by the erase call. Interleaved 30 samples/variant: current helper payload
305.765 [300.810,351.670] us; erase omitted 199.015 [190.370,247.430] us.
A second interleaved check ran archived A and production B executables on that
same B kernel, alongside the diagnostic erase-omitted build:

| Same-kernel diagnostic (30 samples each) | Payload median [min,max], us |
| --- | --- |
| Archived A executable | 194.170 [186.480,221.120] |
| Production B executable | 308.385 [299.630,355.950] |
| B helper with only erase omitted | 199.235 [190.690,247.050] |

All fixture bytes/EOF checks passed. These runs were under host/agent-VM load:
they isolate the call's effect, not a clean performance baseline or a native
percentage estimate. The erase-omitted executable is diagnostic-only, used with
public data, never shipped or proposed as the fix. Production libc/SDK sources
were unchanged during the intervention.

Clock-cache #1080 is in B and held fixed in this comparison. KVM-clock #1084,
sparse VM #1083 and automatic package roots #1085 are not ancestors of B at
`4a768238`. The intervention attributes the reproduced regression to WPA2's SDK
erase, rather than those kernel/package changes. Raw public artifacts and exact
commands are in `/shared/build/wifi-ax200-stage4/file-read-attribution/`.

## Correction (chronium, 2026-10-22)

Remove the generic wipe from `file_read`; the reply scratch belongs to the
caller process. Keep targeted PSK/PMK/PTK and broker key-material erasure. Libc
still supplies `memset_explicit` for the pinned supplicant and native key
handling: ordinary `memset` followed by a pointer compiler barrier
(`asm volatile("" : : "r"(p) : "memory")`), replacing volatile byte stores.
Production-object disassembly shows vectorized stores; the key erase remains
non-elidable.

The same fixed-kernel QEMU setup repeated three interleaved before/after pairs
(30 samples per side), with unchanged iobench objects, SDK inputs and public FILE:

| Diagnostic phase | Before median [min,max], us | After median [min,max], us |
| --- | --- | --- |
| OPEN | 43.215 [40.040,206.530] | 40.780 [38.510,50.110] |
| Payload | 330.540 [310.450,766.850] | 212.480 [192.580,525.040] |
| Whole | 413.465 [389.010,1097.620] | 289.660 [269.260,607.470] |

All byte/EOF checks passed. These are diagnostic under host load, not a native
result or clean performance baseline. Sources measured are byte-identical after
the userland rebase onto `c0b9883` (fix head `4d515173`). Raw records are in
`/shared/build/wifi-ax200-stage4/file-read-fix/`. Native after-fix costs remain
unmeasured; no native/PXE action accompanied this correction.


## Corrected native comparison (2026-10-22)

ThinkPad A `d2cf7091`, B `181f74ab` (userland `4d515173`, ports `96a0cdf0`),
same builder `3d69e83`, Kconfig, effective READ/SCAN-only live/network configuration,
workload/tools and A1 B1 A2 B2 A3 B3 order. Twelve CPUs, TSC, AC, RTL8111 cable,
idle input; horse busy with agent VMs/builds/QEMU. B includes intervening main
commits, so this is a whole-revision comparison. All six cohorts completed;
the orchestrator verified byte-for-byte PXE restoration.

`run.py baseline --cohort A1 --conditions '<actual conditions>'` runs the fixed
`controls.txt` and three 64 MiB ttcp transfers per mode. Repeat for the six
cohorts after their matching reboot; `run.py summarize` checks revision, images,
CPU/clock, successful outcomes and identical workload/conditions before pooling.
The FILE command is unchanged from the attribution above; Lua OPEN times only
`io.open("boot://share/iobench.bin", "r")` between two native-clock wrappers,
with close/output outside that interval. Launch measures `echo -n` through exit.

| Native control | A median [min,max] | B median [min,max] | B/A; samples/side |
| --- | --- | --- | --- |
| FILE OPEN, us | 5.776 [5.641,22.432] | 5.811 [5.651,22.793] | 1.006; 90 |
| FILE payload, us | 208.822 [206.548,406.351] | 205.350 [198.432,310.282] | 0.983; 90 |
| FILE whole, us | 216.326 [213.922,432.590] | 212.775 [205.645,334.588] | 0.984; 90 |
| Lua OPEN, us | 5.856 [5.651,40.345] | 13.015 [5.811,202.901] | 2.222; 90 |
| Launch/exit, us | 848.812 [722.593,1451.366] | 831.029 [701.284,1385.470] | 0.979; 90 |
| Clock wrapper, ns/call | 991.460 [978.884,2111.026] | 1021.647 [1000.906,1640.724] | 1.030; 9 loop averages |
| TCP TX8k, MiB/s | 105.038 [102.077,106.459] | 104.028 [33.869,105.798] | 0.990; 9 |
| TCP TX2k, MiB/s | 92.195 [91.989,92.671] | 92.668 [92.159,93.162] | 1.005; 9 |
| TCP RX, MiB/s | 99.963 [88.350,100.281] | 97.914 [87.403,99.848] | 0.980; 9 |

The native FILE fix is confirmed. Lua OPEN is noisy and unattributed: processes
cluster near 5.8 or 13 us on both sides in different proportions; the previous
run favored B. Chronium requested no further investigation (2026-10-22).
Busy horse and the wide TCP range preclude a quiet-host throughput claim.
This profile measures neither protected WLAN nor idle-supplicant/BSP CPU cost.

Corrected packet/results: `wifi-ax200-stage4/wired-matched/results/summary.json`
under `/shared/present/`. Original images/scripts/results remain unchanged in
`wired-matched-before-fix/`. The delivery restack onto installer main changes
integration heads, not the measured packet or Wi-Fi/FILE-fix source.
