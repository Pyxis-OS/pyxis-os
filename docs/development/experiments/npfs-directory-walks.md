# npfs directory page walks

Accepted #1495 first step, 2026-10-29: map and fetch each visited directory page
once per uninterrupted scan. The [metadata-cache reference](../../devices/npfs-metadata-cache.md)
defines scratch lifetime and remaining linear lookup costs. Full scans,
duplicate rejection, cursor/child validation, rm safety and durability are unchanged.

## Revisions and configuration

Before: clean main `12eb79b94a28`; after: implementation `d49db71297f2`, based
on main `2b168e38`. Those mains have identical kernel/ABI sources. Both comparison
images use the same frozen archive (userland `95709ee6f291`), fs `f3f22b6e07e7`,
lwIP `4fb74e1b9213`, compiler `pyxis-llvm23.1.3-8bdf136` and kernel configuration.
The PR's normal CI separately builds its current userland pin `294727e8827f`.

Nested KVM/q35, host CPU, four vCPUs, 4 GiB, fresh OVMF variables and a fresh
reflink of the same VM-local Btrfs disk per sample; VirtIO block, 512-byte sectors,
64 KiB transport maximum, cache=writeback, 2 GiB npfs pool, 16 MiB journal and
unchanged 30-second background flushing. No concurrent own build/debugger/profile
in timed runs. Each command runs through the existing machine-mode Remote client
in a sole network-owning space with RAM cwd/home and a disposable writable pool.

The retained public clang fixture has 8,777 files, 503 directories and
364,934,961 data bytes, plus its tree wrapper. This is not the original
8,781-entry native package. Separate flat directories hold 1,024/2,048/4,096
empty files. Commands: `rm -r work://tree` (or `flat1024`, `flat2048`, `flat4096`),
then a separately timed `sync work://`, each awaited to successful completion.

## Results

Two fresh-copy samples per implementation, elapsed seconds:

| Fixture | Before removal | After removal | Before final sync | After final sync |
| --- | --- | --- | --- | --- |
| Clang tree | 4.219 / 2.951 | 2.569 / 3.018 | 0.036 / 0.030 | 0.028 / 0.013 |
| Flat 1,024 | 1.478 / 0.874 | 0.531 / 0.512 | 0.024 / 0.013 | 0.017 / 0.013 |
| Flat 2,048 | 5.702 / 3.887 | 2.058 / 1.409 | 0.020 / 0.011 | 0.023 / 0.012 |
| Flat 4,096 | 30.156 / 15.683 | 4.737 / 4.457 | 0.160 / 0.148 | 0.013 / 0.211 |

Flat-directory ranges improve distinctly. Tree ranges overlap, so the small
sample does not establish a precise tree speedup. Both tree removals retain
60 device flushes and 1,800 writes. Background timing changes individual flat
flush counts; the policy is unchanged. These are nested measurements, not a
native SanDisk speed prediction.

Separate private counter builds retain the same worker-call and directory-record
counts. Metadata-block requests fall from 2,392,370 to 268,841 for the tree and
59,352,807 to 779,056 for flat 4,096. Its 33,872,889 record reads remain unchanged;
full name-find time falls from 13.789 to 2.690 seconds. Timers are inclusive and
nested; store counters can include maintenance before the quiescent snapshot.
These profiles do not replace uninstrumented timings or merge instrumentation.

Both after disks and the after-profile disk pass stopped-copy host fsck with
replay allowed. Manual mkdir/copy/rename/link, file contents, nonempty-rmdir
refusal, held-cwd removal refusal and nested removal/sync passed. Duplicate and
malformed-record behavior is source-reviewed, without corruption injection.

## Limits

Linear name comparisons still give quadratic wide-directory behavior; a coherent
name index needs a later decision. #1495 remains open for that residual limit and
native attribution. No native packet is sealed without approved writable scratch
authority on vela; historical ThinkPad/other-stick selectors are not reused.
No ABI, disk format, fs submodule, compiler container or consumer rebuild change;
rebuild kernel/images. Raw counters, logs, hashes and packet-feasibility evidence
remain in `/shared/present/rm-debt-1495/` and the PR, not in this source record.
