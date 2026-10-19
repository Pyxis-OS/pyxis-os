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

Exact-head CI3180 at `e2df9c0c` subsequently passed all four jobs. Its kernel and
matching archive (artifact 13868), with only the private configuration/program
added, repeated preflight and lifecycle on four CPUs and one CPU. The latter
again started with only the backup header valid. Both passed; GDB observed
feature mask one, writable/healthy state, zero staged images and EMPTY journal.
Stopped-image fsck passed. `final-SHA256SUMS` records those artifacts. All own
QEMU/GDB processes were stopped afterward.

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
The initial candidate nested transfer medians were 23–43% higher. The follow-up
below retains that observation but does not reproduce a fixed per-WRITE penalty. Source
inspection finds no added per-WRITE guard or header I/O: count work is on namespace
and cleanup paths, and these runs never call LINK or gain the feature. The
namespace proxies and occasional individual sync samples have substantial host
variance; complete sample ranges and clock-call calibration remain in the raw
transcripts. Task 4 retains matched owner/native
measurements before milestone completion; these initial timings alone establish
neither a native regression nor a speedup.

## Regression attribution follow-up

At the reviewer's request, repeated the same workloads against current main
`b52533d9` (A, exact CI3177 kernel artifact 13858) and PR code `a75b9223` (B, the
code-identical CI3180 kernel). Both use the **same** current archive, private
configuration, disk seed, compiler settings, q35/KVM/four-CPU/4-GiB configuration
and `cache=writeback`. Each fresh boot waits five seconds after Remote starts,
then runs the three namespace batches and both workloads with 15 verified samples
plus warmup. No kernel instrumentation or runtime source change was made.

The table preserves execution order. `cost-*` uses the original `/shared`
VirtIO-FS backing. `cost-local-*` changes only the disposable raw disk's backing
location to local `/home` Btrfs. `cost-kvm-*` returns to `/shared` and additionally
reads QEMU's existing cumulative KVM counters outside command timing.

| Run | Grow ms | Grow sync ms | Prepared ms | Prepared sync ms |
| --- | ---: | ---: | ---: | ---: |
| cost-A1 | 166.878 | 26.636 | 122.168 | 16.042 |
| cost-B1 | 156.880 | 35.692 | 156.136 | 27.045 |
| cost-A2 | 130.886 | 34.058 | 128.461 | 25.675 |
| cost-B2 | 125.324 | 24.953 | 209.675 | 30.905 |
| cost-B3 | 146.479 | 42.532 | 142.727 | 18.201 |
| cost-A3 | 129.674 | 34.461 | 126.879 | 25.586 |
| cost-local-A1 | 137.315 | 42.436 | 131.967 | 27.650 |
| cost-local-B1 | 134.900 | 30.472 | 124.867 | 19.415 |
| cost-local-B2 | 134.696 | 41.439 | 125.378 | 19.523 |
| cost-local-A2 | 134.618 | 42.606 | 131.856 | 33.989 |
| cost-B4 | 151.076 | 34.600 | 120.541 | 14.934 |
| cost-A4 | 126.923 | 34.348 | 124.326 | 25.570 |
| cost-kvm-A | 147.076 | 34.877 | 143.087 | 14.964 |
| cost-kvm-B | 127.892 | 33.288 | 122.953 | 15.862 |

All 420 measured payload samples verified, all commands exited zero, and all
14 stopped pools passed fsck. Records are under `cost-followup/` and the named
run directories beside the earlier records. QMP block statistics and Linux host
thread scheduling counters are captured before/after commands, outside the
in-guest measured intervals. The raw transcripts retain individual sample ranges.
All measurement QEMU processes were stopped.

**Finding:** a fixed 23–43% hard-link write-path cost is not reproduced. With the
same B binary on the same `/shared` backing, prepared medians changed from
209.675 to 120.541 ms; the latter is faster than every original A control in this
series. The local-backing ABBA controls are also neutral/faster for B (prepared
124.867/125.378 versus A 131.967/131.856 ms). A later shared-backing counter pair
reverses the penalty: A 143.087 versus B 122.953 ms. Grow and sync likewise do not
show a consistent candidate penalty. These reversals identify the original
percentage as a nested-environment-sensitive observation, not a stable added
per-message cost; they do not establish native performance or prove VirtIO-FS
alone caused it.

**I/O attribution:** every one of the 14 complete prepared commands issued exactly
64 reads / 262,144 bytes, 738 writes / 34,488,320 bytes and 128 flushes, including
preparation/warmup/verification. Thus extra header/inode I/O or count metadata writes cannot explain the
apparent prepared slowdown; reservation work is compared separately below. No run
called LINK or activated the feature.

**Execution attribution:** normalized machine instructions match for WRITE,
writeback, cache lookup/reservation, transaction reservation, resize, file sync,
inode encode/decode and clock/deadline helpers. Inode/cached-inode/pool sizes and
offsets are unchanged; the new pool boolean occupies padding. The worker's
clock/deadline call sites and wait/yield loop are unchanged; it gains LINK routing.
The actual additional grow-path work is a count invariant once per truncate
cleanup batch and zero-count reclamation checks, with no added mapping scan or
I/O. Prepared overwrite does not initiate that shrink cleanup. Namespace count
changes occur outside its transfer loop.

The KVM counter pair offers a measured explanation of the variable execution
work: total prepared-command MMIO exits were 679,891 in the slower A versus
570,364 in B. BSP counts were nearly identical (516,502/516,310); most of the
extra exits were on CPU1 (156,165/43,476). Host QEMU thread CPU time was
10.765/8.970 seconds for the complete command. Both boots retained the direct
HPET clock. This demonstrates variable AP-side VM-exit work rather than extra
BSP disk work; it does **not** identify the individual wait/scheduling caller or
prove a particular hypervisor defect. Function comparisons rule out an added
clock read in the PR's hot path. No production optimization is proposed from
these measurements. Keep the native gate; do not optimize a hard-link operation
that these workloads never call.

## Remaining gates

Task 3 still owns SDK/libc/ln and public link-count metadata. Task 4 owns native
first-use/durability qualification, count-limit refusal and committed replay/
checkpoint checks for linked mutations. No physical-media, power-loss, injected
I/O-failure or native performance result is claimed here. Interrupted-header
reopening above is a stopped QEMU disk copy after a real completed flush, not a
power-loss simulation. No symbolic links or repair-by-guessing.
