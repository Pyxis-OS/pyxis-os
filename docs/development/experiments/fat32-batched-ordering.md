# Mounted FAT32 batched ordering

Task 3 of [explicit durability](../../wip/explicit-durability.md), 2026-10-19.
Owner accepted eager cluster reservation and the existing FAT32 crash/host-repair
limits. Implementation comparison follows this pre-change baseline.

## Baseline

Pyxis code `19c3cfc8` (documentation head `4c97c87b`), fs `d2fb97c5`, userland
`1836d2a`; published `pyxis-llvm23.1.3-3d69e83`, no compiler rebuild. Ordinary
kernel/fs-tools build passed; matching SDK/archive from main's CI run 3326.
The local build used host Kconfig generation and the existing compiler container;
initial container setup failures are excluded. No kernel instrumentation.

Nested QEMU q35/KVM, CPU=max, four CPUs, 4 GiB, OVMF, modern VirtIO block/net,
`cache=writeback`, raw backing on local `/home` Btrfs. Each repetition restores a
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
These are shared-host nested observations, not native USB performance. Candidate
results, phase-boundary and interrupted-checkpoint checks remain pending.
