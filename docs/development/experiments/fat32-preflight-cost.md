# FAT32 preflight cost baseline

2026-10-13; untouched main `e46c51b79e4654af9f43a7a252b9a9bcef70aad9`, userland
`06812e7d`, ports `1b171610`, fs `414be0ac`, lwIP `a1aadb91`.
`make -j8 image LOG_LEVEL=info` with `pyxis-llvm23.1.3-4c23a20` passed; no
kernel, userland, filesystem or pin changes. Kernel SHA-256
`8398ee9e1a6d25afb76a8fa12aa97c7afc8a4e7da8cdf6f843ae8fee3b01906d`;
initrd `aca6e1b7c47468675bf012d48ef52fa0c84606a8c31fa9000c16ff9b3538336d`.

QEMU 10.2.2, Q35/nested KVM/host CPU, four CPUs, 1 GiB RAM, fresh OVMF/UTC,
modern VirtIO CD/block/RNG, no NIC/USB. Scratch backend `cache=none`, one disk
per boot. GPT entry 1 starts at LBA 2048, exactly the listed partition size,
512-byte sectors, two mirrored FATs. `mkfs.fat -F32 -S512 -s1` or `-s8`;
all fixtures start with the same tiny original text file. No owner media.

Boot `init=boot://init-fat32-mount-write.pxe boot.install=1`, focus the existing
trusted scratch writer, explicitly select disk 1 / partition 1. Commands:
three empty creates `/a`, `/b`, `/c`; hold `/a` RW; write `abc` three times at
offset 0; FILE sync; resize to zero; drop; quit/retire. Dense 512 MiB stopped
writes after the first LIMIT. Dense 1 GiB never obtained RW admission.

Read-only GDB (`may-call-functions off`) observes `fat32_perform`, mounted
`begin_plan` and `fat32_check_context_extra` entry/return. Host monotonic time
between boundary stops includes debugger overhead; nested intervals overlap
and must not be added. Existing VirtIO `generation` deltas count admitted READ,
WRITE and FLUSH requests, excluding FULL retries. Only this scratch disk has
I/O; the successful check/plan boundaries are read-only. Counts are not sectors,
completion counts or measured request-size bins. Source inspection supplies the
single-sector FAT shape. These are profiled baselines, not uninstrumented/native
throughput. Host load was unisolated; keep the large observed ranges.

| Partition / cluster bytes | Clusters | FAT sectors/copy | CREATE seconds (3) | Typical plan admissions | Outcome |
| --- | ---: | ---: | --- | ---: | --- |
| 40 MiB / 512 | 80,628 | 630 | 1.206 / 1.201 / 1.205 | 2,525 | CREATE/write/resize/sync/retire OK |
| 128 MiB / 512 | 258,078 | 2,017 | 6.589 / 7.551 / 3.989 | 8,073 | CREATE/write/resize/sync/retire OK |
| 512 MiB / 512 | 1,032,408 | 8,066 | 14.826 / 14.774 / 14.837 | 32,269 | CREATE OK; 3-byte write LIMIT, zero admissions |
| 1 GiB / 512 | 2,064,848 | 16,132 | RW admission LIMIT after 14.946 s | — | 32,314 admission requests, no writes |
| 512 MiB / 4,096 | 130,811 | 1,024 | 2.068 / 2.041 / 4.120 | 4,116 | CREATE/write/resize/sync/retire OK |
| 1 GiB / 4,096 | 261,627 | 2,048 | 6.256 / 6.218 / 4.033 | 8,204 | CREATE/write/resize/sync/retire OK after hold lifted |

First mutations have additional dirty-mark preparation; held-object/directory
reads also change counts. At 40 MiB the checker uses 1,259 reads, its plan
2,525–2,527, and CREATE 2,540–2,545 total admissions: almost all work precedes
effects. At 128 MiB the analogous check uses 4,033 and plan 8,073–8,075.
WRITE samples: 40 MiB 1.211–1.239 s, 128 MiB 3.436–6.854 s,
512 MiB/4-KiB clusters 1.971–1.983 s; 1 GiB/4-KiB clusters
4.147 / 5.821 / 8.164 s. Successful explicit sync admits one flush
and takes 4–9 ms (debugger overhead included), independently of those sweeps.

A 128-MiB BEGIN/rename check, followed by operator delay beyond expiry, reports
TIMED_OUT for later rename and sync, zero block admissions on those mutation
jobs. This proves refusal before an issued sync, not work-only lease exhaustion
or a new complete editor publication. Original editor evidence remains in
[userland qualification](../../../userspace/config-editor/qualification/README.md).

Target DWARF/source sizing: prepared sectors are 528 bytes. Conservative
mutation workspace is about 1.63 / 5.12 / 20.35 / 40.66 MiB for dense
40/128/512/1024 MiB; replacement growth temporarily charges both old and new.
Dense 512 MiB therefore cannot grow even for the small write, while dense
1 GiB exceeds the shared 32-MiB cap at admission. Its whole disk hash is
unchanged. `fsck.fat -n` is clean after normal retirement on the five completed
mutable fixtures. No repair, uncertain-write injection or native ESP run.

Timing stopped immediately on the owner's container-build notice. The entire
in-progress 1-GiB/4-KiB-cluster timing capture is excluded; functional-only
commands subsequently hit the ordinary request deadline under contention; the
whole scratch disk remained byte-for-byte unchanged.
After the owner lifted the hold, a fresh fixture completed the full matched
sequence: all operations OK, one-flush sync and clean retirement/fsck. Only that
new capture supplies the final table row. Earlier samples predate the notice,
not necessarily all shared-host contention; the resumed run also varies widely.
Native scratch timing is not collected; source/count evidence is stronger than
an extrapolation to physical media.

Raw images, commands, debugger scripts/captures and excluded records stay local.
The following implementation record updates the [accepted contract](../../wip/fat32-preflight-cost.md); the preceding measurements describe the original implementation.


## 2026-10-14 retained allocation proof and local checks

Untouched control `5fab8d39`, fs `414be0ac`, userland `ec234db4`, ports
`2296a06c`, lwIP `a1aadb91`; changed kernel code `0b2f65dd`, fs `c52ab584`
([fs #37](https://git.internal/PyxisOS/pyxis-fs/pulls/37)), userland `7156ff6a`
([userland #257](https://git.internal/PyxisOS/pyxis-userland/pulls/257)).
Both ordinary default images passed `make -j8 image LOG_LEVEL=info` with
`pyxis-llvm23.1.3-e0801c2`. Changed-build gitlinks were subsequently committed;
only documentation changed on main during qualification. No compiler rebuild.

The QEMU configuration, fixture selection and primary command sequence match
those above: four CPUs, 1 GiB, Q35/nested KVM/host CPU, fresh OVMF, VirtIO
scratch `cache=none`, no NIC/USB. The existing read-only GDB observer additionally
records allocation-map/workspace charges. Times include debugger overhead and
shared-host variation, and nested intervals must not be added. No native rate
is inferred. Raw images, hashes, commands and captures remain outside the repo.

The refreshed 128-MiB control admits 8,144 requests at RW open, including two
4,033-request full checks. CREATE admits 8,093/8,088/8,088 requests, writes
8,090/8,087/8,087, sync one. Dense 1-GiB admission returns LIMIT after 32,314
requests, with unchanged disk bytes and 40.66-MiB conservative preparation.
Only admission (3.968 s) and first CREATE (3.861 s) predate a concurrent ordinary
build; all later control timings are excluded from matched comparisons. Counts
and functional outcomes remain usable. The changed timing runs had no concurrent
task-owned builds; the host remains unisolated.

| Partition / cluster bytes | RW admission requests | Map bytes | Observed charged backing bytes | CREATE requests (3) | CREATE milliseconds (3) |
| --- | ---: | ---: | ---: | --- | --- |
| 40 MiB / 512 | 229 | 10,079 | 66,895 | 25 / 20 / 20 | 51.7 / 53.9 / 49.3 |
| 128 MiB / 512 | 577 | 32,260 | 89,080 | 25 / 20 / 20 | 72.4 / 69.2 / 25.5 |
| 512 MiB / 512 | 2,089 | 129,052 | 273,596 | 25 / 20 / 20 | 78.5 / 82.4 / 38.1 |
| 1 GiB / 512 | 4,105 | 258,107 | 531,707 | 25 / 20 / 20 | 53.2 / 40.0 / 49.3 |
| 512 MiB / 4,096 | 363 | 16,352 | 88,992 | 56 / 59 / 67 | 66.2 / 48.4 / 55.3 |
| 1 GiB / 4,096 | 619 | 32,704 | 105,344 | 56 / 59 / 67 | 119.3 / 76.6 / 167.1 |

Backing is the maximum observed at operation boundaries, including held chains;
it is not total resident memory or a sampled allocator peak. Replacement overlap
remains charged before allocation. All six supported geometries complete
CREATE/write/sync/resize/drop/retirement; each retirement returns charged backing
and active instances to zero. Detached host `fsck.fat` passes. The 40/128-MiB
4-KiB-cluster fixtures are below FAT32's minimum cluster count: admission returns
UNAVAILABLE after 40 reads, retains no backing and leaves disk bytes unchanged.

At 128 MiB, CHECK without BEGIN is DENIED with no I/O; under the owner's active
interval it uses 513 requests in 244–263 ms. Held replacement preserves old and
new FILE contents, and CHECK validates the detached root. Root close/reopen and
CHECK succeed; every check preserves the original absolute deadline. After
operator delay beyond expiry, CHECK is TIMED_OUT with zero admissions. Dense
1-GiB admission takes 1.932 s; its explicit CHECK uses 4,041 requests in 1.700 s.
Its mutation workspace is 273,584 bytes, so tiny writes no longer reserve all
FAT sectors. Sync still admits one flush; neither BEGIN nor SYNC runs a check.

Host library qualification separately covers six geometries with three fresh
sequences, zero-fill/held replacement, proof rebuild and stopped-writer refusal.
Touched corruption/stale parent chains and unexpected detached-directory entries
refuse before effects. Capacity refusals before CHECK validation retain proof.
Truncated backing invalidates it; real host ENOSPC retains the uncertain writer,
failed workspace and diagnostic records unchanged across refused operations.
A mtools-seeded full FAT with sixteen interleaved holes allocates exactly those
independently located candidates. Further allocation returns NO_SPACE with valid
proof, zero writes/flushes and unchanged media; cleanup/CHECK/fsck agree.

A separate functional clean-power run holds an attached RW FILE and active
publication interval. Physical Ctrl+Alt+Delete and clean shutdown complete;
GDB observes seal OK and the owned dirty mark cleared while both objects,
backing and the same interval/deadline remain held. Detached host fsck is clean
and reads the written bytes. A second run holds a removed allocated FILE and
active interval: clean shutdown also succeeds, seal performs one flush and
retains the dirty mark, views and deadline. Both FAT copies remain dirty;
read-only fsck reports the expected orphan/dirty warnings without changing
partition bytes. No repair was performed. These are QEMU observations, not
physical power-loss evidence.

The allocation proof is not an ownership proof for unrelated objects. Coherent
in-session cross-links may be detected only at CHECK/remount. Actual kernel
physical-memory exhaustion and native media/power loss are not qualified here.
The scratch-writer sequence is separate from the actual editor publication
qualified below. No real ESP writes were performed.

## Actual 128-MiB editor publication, 2026-10-14

Publisher code `fb9b767d`, parent `2d76c70c`, fs `c52ab584`, other pins unchanged.
Ordinary default image build passes. QEMU uses the original pristine editor
128-MiB ESP on a 160-MiB disk, 512-byte clusters/two FATs, four CPUs and 512 MiB,
matching the original editor profile; current archive has only the existing
private scratch grant override. No concurrent task-owned builds during timing.

Confirmed `config edit` adds three missing boot entries and explicitly adopts
four existing values. Read-only GDB observes exactly one BEGIN → CHECK → END,
same process and delivered active interval, no intervening publication effect,
and unchanged absolute deadline. CHECK succeeds with 616 block admissions in
291 ms. BEGIN through successful END takes 4.711 s including debugger/shared-host
overhead, within the original 30 seconds. All 30 observed interval operations
succeed: swaps, sync, committed verification, cleanup and release.

`config validate` selects the complete committed ESP triple. After clean
physical-overlay shutdown, host extraction matches all four selected sizes/hashes
to the committed record and all three previous files to pristine originals.
Limine is byte-identical, and foreign spans/owner values remain preserved.
`fsck.fat -n` is clean (17 files/27 allocated clusters), both FAT copies clean.
This closes the large-volume publication gate; native ESP publication remains
unqualified. All owned QEMU/GDB jobs stopped. The
[userland record](../../../userspace/config-editor/qualification/README.md)
retains the earlier failure and the new result with its source-reviewed failure
path; no failing-CHECK injection was added.

## 2026-10-14 review follow-up: advisory hints and geometry-derived backing

Kernel `5c201ecf`, fs `d84f8084`, userland `6f19f494`; ordinary default build
passes with the same `e0801c2` compiler. The userland rebase changes no publisher
code. The fixed aggregate 32-MiB setting above describes the earlier revision;
current mounts charge geometry-derived map/scratch/chain backing and return
NO_MEMORY on allocation failure. CHECK converts ownership bits into free bits
in its existing scratch, then copies only on complete success: two full bitmaps,
not three. Host regression covers retained-map preservation on a failed check,
restoration and bad-cluster conversion with callback limits 1/8/9/128/256.

Same Q35/nested KVM/four-CPU/1-GiB/VirtIO scratch configuration, fresh OVMF,
no NIC/USB. Passive GDB observes admitted requests, allocator calls and charged
backing. These are functional profiled runs on a shared host, not matched/native
performance measurements. Initial eight-sector full checks time out at the
unchanged 30-second admission deadline, without writes or retained charges.
Current full checks use callback/hardware-bounded 64-KiB reads independently of
the public 4-KiB DISK payload; local mutation windows and writes remain unchanged.

A sparse 49-GiB partition with 512-byte clusters has 101,179,472 clusters and
790,465 FAT sectors per copy. Each bitmap is 12,647,435 bytes (the former
three-map total is 37,942,305 bytes). Full-check workspace is 12,794,016 bytes.
RW admission succeeds with 12,420 requests, no write/flush callbacks. BEGIN →
CHECK → CREATE → held three-byte WRITE → FILE SYNC → END → DROP → RETIRE all
succeed. CHECK uses 12,359 requests, zero allocator/write/flush calls and preserves
the original absolute lease deadline. CREATE/WRITE use 25/24 requests;
file sync uses one flush. Full/local windows are observed as 128/8 sectors.
Measured charged-backing peak is 25,441,467 bytes; release leaves mount/global
charges and instance count zero. This is charged backing, not total resident
memory. Operator-delayed expiry separately refuses CREATE without I/O.

A fresh 64-MiB partition has 129,022 clusters. Both signed FSInfo copies start
with free count 129,023 and next-free 1 (both out of range). RW admission succeeds
with 84 requests, no writes/flushes; CHECK uses 23 requests and no allocation,
write or flush, preserving the original lease deadline. CREATE, three-byte WRITE,
FILE SYNC, END, DROP and RETIRE succeed. Measured charged-backing peak is 178,848
bytes; final charges and instances are zero. Invalid hints do not weaken metadata
signature, mirrored-FAT, chain or namespace validation. After retirement,
independent FAT scans find 101,179,470 / 129,020 free clusters respectively;
both FSInfo copies match those counts and set next-free to UINT32_MAX. Both FAT
copies match and are clean; fsck succeeds and mtools reads `/a` as `abc` on both.
Large media/firmware were reused, without a cold-cache reset. Earlier editor and
power qualifications remain the separately dated results above. No native media,
real ESP or actual kernel physical-memory-exhaustion run; all owned jobs stopped.

## Owner-present native follow-up

Native ESP steps require the owner's explicit approval and presence. This sheet
adds no permission to write the real ESP. Keep GUIDs/serials in owner-local
configuration; verify the exact selected disk/partition before running commands.

1. Record the merged kernel/fs/userland revisions and stage a compatible build
   through the owner. First use disposable supported scratch FAT media, with
   declared scoped config directories and the same RW control's WRITE|INTERVAL.
2. Run `config plan`, inspect the selected target and proposed changes, then
   `config edit` with explicit confirmation or apply that saved frame. Record
   native statuses and elapsed time; publication must complete under the original
   lease, including CHECK, read-back, sync and interval release. On failure retain
   diagnostics/recovery artifacts and stop; do not retry or repair automatically.
3. Run `config validate`, read back the selected files and retained previous
   hashes, then cleanly shut down with a file still open. Reboot and verify the
   committed triple. Any detached-chain dirty mark is an expected recovery case,
   not permission to repair during this check.
4. Perform the separately scheduled real-ESP
   [RO run sheet](../../devices/fat32-volumes.md#owner-present-native-esp-run-sheet):
   observe, list/read/hash, close and release. Do not run editor Apply there.
5. Report exact revisions/media/profile/results and restore the ordinary owner
   profile. Real ESP publication is a separate owner-approved consumer run.
