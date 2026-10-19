# Final explicit-durability measurements

Task 4 of [explicit durability](../../wip/explicit-durability.md), assigned
2026-10-19. **Preparation and qualification record; final repeated measurements
and the scheduled native run are not complete.** Earlier results remain in the
[task-1/2/2b record](explicit-durability-baseline.md) and
[FAT32 task-3 record](fat32-batched-ordering.md).

## Matched inputs

The old tables are not a matched four-state series: tasks 1/2/2b used VM-local
Btrfs, task 3 shared FUSE. Earlier extraction controls have only one sample;
the published task-2b numbers predate its staged-home reuse fix. Do not relabel
those results or pool them with new timings.

New inputs recovered from successful project CI:

| State | Kernel/code | CI internal run | Frozen caller group |
| --- | --- | ---: | --- |
| Original | `a29372f8` | 3139 | userland `22a3ee7d`, SDK 3139 |
| Namespace batching | `7ffc03f6` | 3206 | same original archive/SDK |
| Corrected file batching | `89c86f74` | 3243 | same original archive/SDK |
| Before FAT32 batching | `19c3cfc8`, build identity `4c97c87b` | saved qualified build | userland `1836d2ae`, SDK 3326 |
| FAT32 batching | `016e12bc`, build identity `4e21c105` | saved qualified build | same later archive/SDK |

The later caller group reflects the FILE INFO layout change; keep its results
separate. The earlier kernel pair adds unused screenshot protocol/flag values,
without changing the exercised operations. Published archives and SDKs were
recovered together. No independently published compiler/toolchain is rebuilt.
The private installer variants use existing builder
`pyxis-llvm23.1.3-3d69e83` and their corresponding SDK/Lua/Mbed TLS exports.

The original extraction archive is unavailable on this VM. The fixed replacement
is 293,416,960 bytes, SHA-256
`a94062826a98b2f099f0839f078938396ad3857243cae6487ff98c8d0c901787`,
6,488 regular files / 288,272,949 file bytes. It is the retained hosted-driver
payload, used only as extraction data. Its times cannot replace those for the
older 369,326,080-byte payload.

## Installer clock capture

Owner accepted 2026-10-19: labelled, **never-merged** installer measurement
builds, reusing existing clock readings. Stock Install init does not configure
IPv4; `log.udp=1` alone therefore cannot supply the former native sheet's
promised live timestamps. No kernel probe or network authority is added.

The same source patch applies to both frozen caller groups. It retains at most
128 completed phase records and six boundary timestamps, uses only existing
observer BEGIN/END clock reads, and prints selected results after successful
execution. Clock/capacity failure invalidates the observation, not the install.
The summary goes to the existing terminal/diagnostics destinations. No timing
output is added during executor work.

Individual transfer phases end **before** their following file sync. Compare
the aggregate envelopes instead: program copy through the start of verification;
ESP staging through the start of boot-payload verification; and old-revision
removal through the start of cleanup-directory close. These include the relevant
publication barriers and intervening work. They exclude unrelated startup and
final ownership cleanup; no whole-operation wall-time claim is made.
A RAM-workspace copy produces a fourth staging row, so rows are identified by
bytes/order rather than assumed to be exactly three disk writes.

Patch: `/shared/present/durability-baseline/installer-timing-original.patch`
(and byte-identical `installer-timing-later.patch`), SHA-256
`2d7199f319ddd7b2a431d2f8ebb75cd6461fa76c888c0bac785250169a6b3d78`.
The source patch and private archives stay outside production pins/branches.
Build commands/logs, kernel/archive identities and QEMU command lines are under
`/shared/present/durability-measurements/`; native artifact hashes are in
`/shared/present/durability-baseline/SHA256SUMS` and each variant directory.

## Fixture and qualification

The VM's unavailable old fixtures were rebuilt through actual classic Install
with `82f9387d`, then Update with `e9deaa8d`, using the same original archive.
After clean restart: 4 GiB disk, 512 MiB ESP, 28 MiB journal, both pool headers
valid, journal EMPTY, feature masks zero, host fsck passes. The retained revision
has 2,241 files including its manifest, 48,732,059 bytes, 13,291 blocks in 2,396
physical runs; 144 split files, maximum six runs, no holes. This is seed layout,
not the final workload result.

QEMU qualification uses nested KVM, q35/max, four vCPUs, 4 GiB, fresh OVMF,
VirtIO block/net/RNG, 512-byte sectors and a 64 KiB block transfer, raw
cache=writeback backing on **shared FUSE**. Separate QMP monitors allow block
counter sampling and manual UI input without a monitor-client conflict.

- The initial original-writer clock-summary build completed Update and clean
  restart. Its first summary lacked aggregate intervals; it is qualification
  only, not a final matched timing sample.
- The final later-group timing build completed Update on the original 4 GiB
  seed, verified program/boot bytes and produced all three aggregate intervals
  on one summary screen. Clean restart completed. Its preparation overlapped
  host packet work; this is qualification, not a performance comparison.
- A stopped-file capture and exact-geometry restore passed host fsck, GPT
  verification and verification of every restored live byte. Capture requires
  exactly the two seed revision directories and an empty home volume.
- A 4 GiB fixture with GPT backup moved to a 64 GiB image was correctly refused
  by Update as not matching the classic installer layout. It made no install
  changes and is excluded. Native setup uses the real disk geometry instead.

These checks do not qualify physical USB behavior, native performance or
interrupted-restoration recovery. Remaining final runs/verification are pending.

## Native packet and accepted profile

Owner accepted 2026-10-19: green SanDisk 64 GB stick, already disposable scratch
from the hard-links check, in a **direct ThinkPad USB-A port**. The orchestrator
schedules after 14:30 and stages PXE; the agent performs no PXE/media actions.

Packet/run sheet: `/shared/present/durability-baseline/README.md`.
Two untimed seed boots create a classic layout at the stick's actual size.
After clean restart, a read-only exclusive capture runs fsck and saves all ESP,
allocated pool (including zero metadata) and GPT backing. Each measured sample
restores/verifies exactly that view, with unchanged capacity and no feature bit.
All media-changing operations are explicit owner commands against the selected
whole USB disk. No installed pool is converted or used by historical writers.

The full Update matrix has three samples per state, alternating within each
caller group, with a fresh restoration every time. The sheet exposes its cost:
two setup boots plus fifteen measured boots/restores, provisionally 1½–3 hours,
schedulable in chunks. Native extraction is a separate Remote workload; an
Update pass is not silently counted as extraction or fragmentation evidence.

No native numbers have been collected for this record. Do not tick task 4 or
rewrite the milestone as complete until the scheduled evidence and final matched
samples have been recorded, including any remaining coverage limits.
