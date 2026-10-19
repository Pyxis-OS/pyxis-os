# Native hard links: transaction and lifetime integration

2026-10-19, task 2 of [the milestone](../../../wip/npfs-hard-links.md).
Baseline Pyxis `6e83cfa6`, candidate code `1140b5bc` (rebased without filesystem
code changes as `008a3b0e` on main `b52533d9`); fs `d2fb97c5` ([fs #44](https://git.internal/PyxisOS/pyxis-fs/pulls/44)).
No compiler rebuild: existing `pyxis-llvm23.1.3-3d69e83`.

## Configuration and records

Ordinary target kernel and fs host-tool builds passed. Baseline kernel/archive
came from exact CI3168; candidate kernel was built with the same published
compiler. Private archive configuration exposes writable disposable `bench` and
`other` npfs volumes plus RAM home, with a Remote shell. No owner media is used.
QEMU uses q35/KVM, OVMF, 4 GiB, modern VirtIO block/net, one or four CPUs and a
130 MiB GPT disk with a 128 MiB legacy pool and 8 MiB journal. Filesystem worker
and allocator ownership are unchanged; no new klog lines or shipped test hooks.

`/shared/present/npfs-hard-links-task2/` contains source/commands, launch arguments,
build logs, Remote transcripts and GDB records. `launch.py`, `commands.py` and
`link-qualification.c` are private qualification inputs, not installed tools or
new repository test infrastructure. The temporary program calls the public LINK
ABI directly until task 3 adds its ordinary helpers. An initial fixture used the
wrong host CRC algorithm and was refused as corrupt; the corrected CRC32C seed
passed host fsck before qualification.

## Functional observations

Both one- and four-CPU runs passed source LOOKUP/READ_FILES/WRITE_FILES and
destination CREATE refusals, missing/existing names, directory source,
cross-volume and RAM backends, wrong destination type, malformed/overlong names
and a closed destination handle. After these rejected operations, GDB observed
feature mask zero: no first-use activation. Destination CREATE alone then sufficed
for a valid LINK.

The lifecycle check passed shared INFO identity, reads and writes, cross-directory
aliases, nonfinal unlink, same-inode REPLACE and NO_REPLACE no-ops preserving both
names, shared shrink/EOF, alias move, replacement of a multiply linked victim,
and last unlink with held files still readable/writable. The old victim retained
its own contents through its other alias and held handle. The final persisted
pair reported one inode, count two and matching bytes; stopped-pool fsck passed.
There is no orphan-held-file LINK form.

GDB at LINK publication observed live size 65,540, staged size 65,536 and both
counts two: adding a name did not publish the dirty appended tail. At final
handle release it observed count zero, DETACHED and positive held references;
cleanup waited for those references. The detached dirty victim likewise retained
its live bytes while its durable prefix remained shorter.

The one-CPU seed deliberately had an invalid primary header and a valid backup.
At first LINK, GDB paused after the opposite header's completed flush, before
the second write, and copied the quiescent disk. Both captured headers were valid;
they differed only in byte 88 (the hard-link bit) and CRC bytes 108–111. Reserved
byte 128 remained 77 in both. The feature-bearing primary was selected on restart.
Read-only reopening read the original 65,536-byte source and preserved the entire
disk SHA-256. Writable reopening reached `repair_header` before admission, copied
the selected raw header to its lagging partner and flushed. Both headers then
matched byte-for-byte with the reserve preserved; the lifecycle check and fsck
passed again. Writable-upgrade repair shares this helper but was source-inspected,
not separately exercised through a live read-only mount upgrade.

The shared additive-header helper also passed host GDB checks for mask addition,
raw reserve preservation, selector acceptance and a zero-gain byte-identical
result. This is codec evidence, separate from the native interruption check.

## Matched nested-VM observations

Before implementation, A1 captured three 100-file create/write/close/rename/remove
batches and existing iobench grow/prepared overwrite workloads (1 MiB, 4,080-byte
buffer, warmup plus five verified samples, explicit file sync). A1–B1–A2–B2 used
fresh identical pools, four CPUs, the same archive and no kernel profiling.
Each run completed all commands with status zero and passed stopped-pool fsck.

| Median, ms (five samples per cell) | A1 | B1 | A2 | B2 |
| --- | ---: | ---: | ---: | ---: |
| Grow transfer | 122.375 | 152.375 | 126.498 | 165.996 |
| Grow file sync | 23.398 | 30.339 | 24.570 | 33.187 |
| Prepared overwrite transfer | 121.215 | 173.651 | 123.000 | 151.567 |
| Prepared file sync | 14.745 | 20.518 | 15.212 | 26.522 |

Namespace batch host wall proxies (including Remote/launch/scheduling), ms:
A1 336/330/314; B1 401/321/413; A2 538/955/810; B2 352/396/453.
The candidate's nested transfer medians are 24–43% higher; this is an observed
regression, without a proven source attribution or native extrapolation. Source
inspection finds no added per-WRITE guard or header I/O: count work is on namespace
and cleanup paths, and these runs never call LINK or gain the feature. The
namespace proxies and occasional individual sync samples have substantial host
variance; complete sample ranges and clock-call calibration remain in the raw
transcripts. Task 4 must resolve the practical cost with matched owner/native
measurements before milestone completion; this task does not claim a speedup or
neutral performance.

## Remaining gates

Task 3 still owns SDK/libc/ln and public link-count metadata. Task 4 owns native
first-use/durability qualification, count-limit refusal and committed replay/
checkpoint checks for linked mutations. No physical-media, power-loss, injected
I/O-failure or native performance result is claimed here. Interrupted-header
reopening above is a stopped QEMU disk copy after a real completed flush, not a
power-loss simulation. No symbolic links or repair-by-guessing.
