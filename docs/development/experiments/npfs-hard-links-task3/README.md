# Native hard links: SDK, libc and ln

2026-10-19, [task 3](../../../interfaces/native-hard-links.md). Baseline Pyxis `91154a33`;
qualified integration `7269ac93` (kernel code `59f92ef9`), userland code `7d2a493`
plus documentation `4e9770b`, fs `d2fb97c5`. The candidate rebases on main
`10ee4a78` to retain the merged package client; the baseline predates that merge.
Existing compiler `pyxis-llvm23.1.3-3d69e83`; no compiler change.

## Configuration and checks

Ordinary SDK/runtime/kernel/ln and host-tool builds passed. Exact CI3195 rebuilt
the complete image and SDK/app/port closure for the 56-byte INFO reply. QEMU uses
q35/KVM, OVMF, modern VirtIO block/net, 4 GiB and one/four CPUs. Disposable GPT
media contains a 128 MiB legacy pool, an 8 MiB journal and separate `bench`/`other`
volumes; RAM home and the archive exercise unknown counts. Disk backing is local
`/home` Btrfs. No owner media, new shipped checks, klog or instrumentation.

`/shared/present/npfs-hard-links-task3/` holds build logs, private hand-run SDK/libc
qualification source, commands, Remote transcripts and GDB records. Initial
harness corrections supplied the required creation mode 0666, expected archive
source EACCES before backend selection, and held both FILE references during
identity comparison. Earlier failed harness runs remain labelled separately.

One- and four-CPU runs passed missing source LOOKUP/READ_FILES/WRITE_FILES and
destination CREATE, existing/missing names, directory/trailing-slash source,
cross-volume and RAM refusal. Destination CREATE alone sufficed for the component
operation; path traversal also required LOOKUP. Libc returned EACCES, EEXIST,
EINVAL, ENOENT and ENOTSUP according to the documented native profile. `stat`
identified a directory through LOOKUP-only traversal while refusing an unreadable
file. Relative links used the retained cwd; shared writes and held-file identity
matched. Counts progressed from legacy one through aliases and unlink; last
unlink reported valid zero while the held file remained readable. RAM/archive
counts stayed unavailable.

Ordinary shell `ln` passed exact target, directory destination, multiple sources,
literal leading-dash names after `--`, existing-target refusal, and rejection of
`-s`/`-f`. CLI failures returned nonzero without replacing existing contents.
The stable-name check linked a new version to a temporary name, replaced the
stable name with rename, explicitly synced its parent, and kept the old FILE
readable. A fresh boot preserved both new names, count two and bytes; identity
was compared only while holding both files. Stopped-pool fsck passed.

Read-only GDB observed count zero with one retained inode reference and an INFO
reply carrying LINK_COUNT_VALID and zero. After explicit sync it observed the
feature bit set, no failed pool, zero staged images and EMPTY control state.

## Nested timing observations

Each fresh four-CPU boot runs three Lua batches of 100 create/write/close/rename/
remove cycles, then 1 MiB grow/prepared iobench writes with 4080-byte buffers,
five verified samples plus warmup and explicit sync. Configuration, disk seed,
local backing and launch arguments match; archives are rebuilt for their ABI.
The Remote host batch proxy includes dispatch/client latency. Debugger inspection
happens separately; the debugger-interrupted first candidate run is excluded.

| Boot | Namespace batch ms (three) | Grow median [range] ms | Grow sync ms | Prepared median [range] ms | Prepared sync ms |
| --- | --- | --- | ---: | --- | ---: |
| A1 baseline | 326.4 / 331.0 / 319.0 | 137.8 [129.3–141.5] | 41.8 | 134.5 [133.8–136.3] | 19.3 |
| B1 candidate | 333.2 / 324.5 / 320.1 | 150.2 [137.6–159.3] | 39.3 | 112.1 [111.3–114.9] | 17.7 |
| A2 baseline | 332.1 / 323.0 / 319.8 | 133.9 [130.8–142.6] | 42.0 | 122.0 [120.5–122.8] | 18.0 |
| B2 candidate | 333.8 / 330.0 / 326.3 | 129.7 [125.7–131.8] | 27.5 | 123.8 [123.1–125.7] | 18.4 |

Namespace timings overlap. Grow changes direction; prepared differences also
vary across cycles. No consistent transfer penalty or speedup is established by
these nested runs. Source inspection finds only optional count export in the
kernel, not a changed WRITE/transaction path. This is no native performance
claim; task 4 retains native/durability qualification, count-limit refusal and
committed-replay checks. Full sync ranges remain in the raw transcripts.

## Deployment limit

INFO grows 48→56 bytes and libc `struct stat` grows: rebuild the complete
SDK/runtime/application/port closure. Independently published toolchain bundles
compiled against the previous metadata layout must be rebuilt and republished
before use with this kernel. The compiler container itself is unchanged.
