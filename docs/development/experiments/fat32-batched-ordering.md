# Mounted FAT32 batched ordering

Task 3 of [explicit durability](../../devices/explicit-durability.md), 2026-10-19.
Owner accepted eager cluster reservation and the existing FAT32 crash/host-repair
limits. The engine is committed as fs `f3f22b6`; adapter code `016e12bc` (based on `3c0e0b54`), qualified as complete kernel
`4e21c105` with that fs pin. No compiler/container change.

## Baseline

Pyxis code `19c3cfc8` (documentation head `4c97c87b`), fs `d2fb97c5`, userland
`1836d2a`; published `pyxis-llvm23.1.3-3d69e83`, no compiler rebuild. Ordinary
kernel/fs-tools build passed; matching SDK/archive from main's CI run 3326.
The local build used host Kconfig generation and the existing compiler container;
initial container setup failures are excluded. No kernel instrumentation.

Nested QEMU q35/KVM, CPU=max, four CPUs, 4 GiB, OVMF, modern VirtIO block/net,
`cache=writeback`, raw backing on the shared `/shared` FUSE mount (not VM-local
Btrfs or tmpfs). A and B use the same path and backing configuration. Each repetition restores a
fresh 514 MiB GPT image with a 512 MiB FAT32 partition, 512-byte sectors,
eight-sector clusters and two FATs. PREPARED.BIN contains a contiguous 40 MiB
source pattern. Fixtures are rebuilt here; opus2's original VM-local fixtures
are unavailable. These are matched staging operations, not a full installer run.

A private native-API consumer copies 40 MiB from the archive in 1 MiB
`file_transfer` calls, performs the final resize and explicit file sync, then
checks every byte against the independently generated pattern. It tests a new
file and an existing prepared file. Transfer/sync clocks exclude verification;
QMP device counts include setup and verification. The exact source, fixtures,
commands and raw records are under `/shared/present/fat32-batched-ordering/`.
The private trusted startup explicitly exports its configured FAT32 write source
as a typed root for Remote; no shipped configuration or authority policy changes.
An initial missing-source profile failed before work and is excluded.

| Fresh boot | Grow transfer s | Grow sync ms | Overwrite transfer s | Overwrite sync ms |
| --- | ---: | ---: | ---: | ---: |
| A1 | 1.495 | 1.228 | 39.390 | 1.070 |
| A2 | 1.640 | 2.087 | 41.044 | 1.218 |
| A3 | 1.403 | 0.934 | 40.801 | 1.045 |

All six staging operations verified the full 41,943,040 bytes and exited zero.
Per boot, grow issued 164 flushes/845 writes/21,922 reads; overwrite issued
121 flushes/760 writes/105,431 reads. The overwrite's extra reads remain visible;
batching barriers alone does not promise to remove chain-validation reads.

A1 also ran the existing `iobench write` on new grow/prepared paths, with
`--buffer 65536 --rounds 5 --sync`, one warmup plus five verified samples:

| Mode | Transfer median [range] ms | Whole-command device flushes |
| --- | --- | ---: |
| Grow | 7774.381 [7679.639–8219.242] | 6209 |
| Prepared | 7632.993 [7483.746–14292.266] | 9558 |

The FILE helper reports short writes and drains them normally; 258 write calls
per 1 MiB pass. Whole-command counts include untimed preparation and verification.
These are shared-host nested observations, not native USB performance. The
subsequent native policy comparison is in the [task-4 record](explicit-durability-measurements.md).

## Batched candidate

Same archive, source pattern, machine and backing configuration; three fresh
boots, all six 40 MiB outputs verified byte-for-byte with zero exit status.
No profiler or GDB was attached during timing. The tests used the implementation
subsequently committed at the revisions above; rebasing changed no tested C code.

| Fresh boot | Grow transfer s | Grow sync ms | Overwrite transfer s | Overwrite sync ms |
| --- | ---: | ---: | ---: | ---: |
| B1 | 1.517 | 54.440 | 40.493 | 35.817 |
| B2 | 1.124 | 17.915 | 37.801 | 42.096 |
| B3 | 1.714 | 67.693 | 42.521 | 15.436 |

All three boots have identical operation counts: grow 58 flushes/712 writes/
21,519 reads; overwrite 45 flushes/682 writes/105,094 reads. Flushes fall from
164/121 to 58/45; transfer ranges overlap. Explicit sync now drains pending
images instead of only flushing already durable mutations, explaining its higher
latency. Prepared overwrite remains dominated by validation reads; batching is
not a solution to that separate path. Shared-host/FUSE timing variation prevents
a native throughput claim.

B3 repeats the existing short-write iobench commands above, with all five samples
verified per mode:

| Mode | Transfer median [range] ms | Whole-command device flushes |
| --- | --- | ---: |
| Grow | 4358.408 [3567.666–8114.182] | 53 |
| Prepared | 4450.790 [4427.642–4529.072] | 57 |

## Functional and debugger checks

Ordinary kernel, filesystem host-tool and source SDK builds passed. Disposable one-CPU
Remote and four-CPU local QEMU checks passed pending visibility after close/rebind,
attenuated RO views, shrink/regrow zeroing, replacing rename with a held victim,
unlink/read/write through a held file, final-reference reclamation, an 8 MiB
single-operation resize exceeding the target, and explicit directory sync.

The four-CPU local bundle also ran authorized CHECK during a publication interval.
GDB found 37 pending images/2048 quarantined clusters before and after CHECK,
with allocation proof valid; QMP counted 94 reads, zero writes and zero flushes.
The subsequent checkpoint emitted DIRECTORY, RECLAIM and FSINFO in order,
retaining quarantine through the last flush before clearing pending state.
Independent host reads verified the synced file's 8192 expected bytes on all
three functional images. `fsck.fat -n` reported only the owned dirty bit, expected
after QMP stop without clean-power sealing, and no structural errors.

A fresh RO-only QEMU boot reopened the synced image and verified the retained
8192-byte file. A separate fixture with a changed secondary-FAT free entry passed
repair plus the functional sequence; after clean power both FAT copies match.
An unsynced `printf background > fatw://background.bin` performed no writes or
flushes before return; the following 31-second sleep saw background writeback
(6 writes/5 flushes). A later unsynced `power.bin` write likewise had no media
writes before local `poweroff`; after ACPI exit both files verified and host fsck
returned zero with no dirty flag or structural warning.

Selected interruption images were copied with all vCPUs stopped at ordinary GDB
breakpoints after DATA, FAT and DIRECTORY flushes of a 6161-image capacity
checkpoint. After DATA, only the owned dirty flag is reported. After FAT, host
fsck identifies 768 unreferenced clusters (3 MiB); after DIRECTORY it identifies
the stale free-count hint, and an independent read verifies the exact 3 MiB
published prefix. These demonstrate the accepted host-repair limits, not replay,
sector-tear coverage or atomic publication. The resumed operation subsequently
verified the complete 40 MiB output. Compressed copies, hashes and host diagnostics
are in the private record directory.

Limits: no native USB or full installer run, 4096-byte-sector runtime run,
fragmentation comparison or injected backing write/flush failure in this slice.
Uncertain-batch ownership is source-inspected; this run does not qualify every
crash boundary or hardware cache behavior. The [task-4 record](explicit-durability-measurements.md)
adds the native matched policy result without extending these crash claims.
