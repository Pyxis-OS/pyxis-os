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
Native records: `wifi-ax200-stage4/wired-matched/results/summary.json` under
`/shared/present/`.

## Cause and isolation

Userland commit `fa2bb896` in the WPA2 stack added an unconditional full reply
scratch erase to `lib/file.c:file_read`. `libc/memory.c:memset_explicit` clears
through volatile byte stores. Each READ erases all 4,096 bytes, even on a short
read or failure; the 257 data reads add 1,052,672 explicit byte stores per MiB.
The EOF check adds one more erase outside the payload interval. This erasure
protects secret FILE reads, but the slow implementation affects every FILE.
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

Proposed correction: make explicit clearing efficient while retaining its full,
non-elidable erase contract, then repeat the matched native check. No production
performance correction is included in this record.
