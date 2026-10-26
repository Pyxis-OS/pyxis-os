# Caller-CPU MEMORY and frame batches: native vela A/B/C

Chronium/orchestrator ran the sealed packet hands-free on vela on 2026-10-26,
**one fresh boot and one sample per workload per variant**. A, B and C all
returned the collector's PASS. PXE was restored by the orchestrator. This closes
the native packet's functional check for BSP-removal tasks 1–2, not the later
invalidation or launch/reaper work.

## Builds and environment

| Variant | Kernel source | Meaning |
| --- | --- | --- |
| A | `742817b0c5b3341f71571d418d82ba08740fad9a` | Before caller-CPU MEMORY (#1254) |
| B | `c7028007a8d19724699b37ac623fc2b855bf052e` | Caller-CPU MEMORY, unchanged PMM |
| C | `1c71d1bbcd330f70565a41999bd1b2716af5dc4e` | Main including bounded frame batches (#1263) |

Each uses the identical live archive SHA-256
`89019393f6b2bc7802674f68c6aa63551c1a13e8ff8085a97a38c8476f9af727`,
identical kernel configuration and unchanged probe programs. LOG_LEVEL=debug,
Clang23 builder `pyxis-llvm23.1.3-3d69e83`. All three report 16 online CPUs,
confirmed AC, HWP applied=16 with zero failures, eager XSAVE `0x7`/832 bytes,
and a CPUID-0x15 TSC reference of 3686.400 MHz, RDTSCP+LFENCE. That is the clock
reference, not a measurement of the core's operating frequency.

The live config fixes Remote and children to **dense CPU1**, while network
startup stays on CPU0. All report matching CPUID/core-type fields. A services
MEMORY on CPU0; B/C service it on the caller CPU1. On hybrid hardware that
comparison includes service-core differences. The Go/sibling check shares the
fixed CPU: no native multi-CPU scaling or contention claim follows.

Live RAM only: no installed-pool mount, `nvme.write=0`, no raw-device-data writes.
The collector compares target-clock durations; host frame-arrival times are not
used as timings. Command line:
`init=boot://boot-init.pxe nvme.write=0 display.timing=off display.idle_blank=0 log.udp=1 remote.beacon=vela`.
No attended input or visual judgement was part of this check.

## Single-run elapsed results

Milliseconds, one sample in every cell; these are not medians over repeated runs.
The same existing allocbench uses 256 allocation/release pairs in the small and
long-control cases, and 16 pairs in the short large case.

| Workload | A | B | C |
| --- | ---: | ---: | ---: |
| 4 KiB, live16, rounds16 | 3.964 | 2.808 | 2.807 |
| 1 MiB, live4, rounds4 | 6.646 | 6.027 | 6.022 |
| 1 MiB, live4, rounds64 | 104.976 | 95.582 | 94.662 |

Task 1's observed A→B improvement is clearest in the small-request case.
B/C are close; the 0.920 ms difference in the long control does not establish a
repeatable native batch speedup. No variance or statistical confidence can be
inferred from these singles. The separate
[matched nested frame-batch record](../frame-batches/README.md) covers concurrent
mutators; it is not substituted for native data.

All workload allocation/release counts match, with zero allocation failures.
Every boot also passes the packet's functional checks: large/self-contained and
sparse operations, provisional pin unwind, withdrawal/staged I/O, YMM state,
Go parallel/copied-stack/GC work, 128 locked-native-thread retirements and waited
process stop. These are collector-checked results, not simply a successful boot.

## Evidence and comparison correction

Evidence stays outside Git at `/shared/present/bsp-removal/native/`:

- `results-vela-A-20261026T144604/`
- `results-vela-B-20261026T144706/`
- `results-vela-C-20261026T144756/`
- `vela-ab-20261026.txt`, `vela-bc-20261026.txt`, `vela-abc-20261026.txt`
- `qualification-20261026/evidence.json` and identifier-scrubbed
  `vela-{A,B,C}-kernel-ring-masked.log`, obtained over Remote using `log`.

The separate masked **UDP transport capture has not been located/attached**.
The retained Remote kernel-ring captures are not mislabelled as UDP evidence;
that attachment remains open. No raw log or hardware serial/MAC is committed.

The original host `compare.py` assumed positional labels A/B and therefore
refused B/C despite compatible captured records. It now derives any distinct
variant labels from the records and emits `MEMORY-BC` for B/C. Revision/manifest,
AC/CPU/HWP/XSAVE, clock and sample-count checks stay enforced. A/B, B/C, reversed
C/B and A/B/C pass on the actual native records; duplicate labels and a copied
host record with a changed XCR0 are refused. No guest/kernel fault was injected.
The operator packet was resealed after this host-only tool correction; its
kernel/archive/config bytes and native result manifests are unchanged.

Remaining qualification: native unrelated audio/display mixed-load stress,
native simultaneous multi-CPU mutators, and the task 3 failure/retirement protocol.
No further native run is requested by this record.
