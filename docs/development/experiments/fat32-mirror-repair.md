# FAT32 mounted mirror repair qualification

Owner accepted both [scope defaults](../../wip/fat32-preflight-cost.md#assigned-follow-up-fat-mirror-repair)
on 2026-10-14. No real ESP writes are authorized.

## Baseline before implementation

Untouched main `ba5e134d054faa60ef2178e2d046dccaf9342a38`, fs `d84f8084`,
userland `220b8076`, ports `efe8d877`, lwIP `a1aadb91`. Exact-main CI API run
2578 (UI run 2491) supplies verified kernel/SDK/userland/ports bundles; each
manifest matches these revisions and payload verification passes. Kernel
SHA-256 `8a429bc0ac764a481a7d8427a19ba2030bb667ea1e04b1ef4437fea68bdaa208`.
Existing df87791 CI builder; no compiler rebuild. Earlier artifacts fetched with
the UI number as an API ID were rejected by their revision manifests before
any FAT operation; they supply no baseline sample.

Q35/nested KVM/host CPU, four CPUs, 1 GiB, fresh OVMF, modern VirtIO block,
`cache=none`, no NIC/USB. Pristine GPT scratch partition: 128 MiB, 512-byte
clusters, two FATs, 258,078 clusters; same fixture as the preflight qualification.
Boot the trusted `init-fat32-mount-write.pxe boot.install=1`, explicitly select
disk 1/GPT entry 1. Read-only GDB boundary observers (`may-call-functions off`)
record request admission deltas and host monotonic elapsed time. Debugger
stops perturb time; nested spans overlap. Host load remains unisolated. These
are profiled functional/count comparisons, not native throughput.

Sequence: CREATE `/a`, `/b`, `/c`; hold `/a` RW; write `abc` at offset zero three
times; FILE sync; BEGIN/CHECK three times; drop; quit (ends the interval and
retires). No media edits or function calls through GDB.

| Operation | Block admissions | Host milliseconds |
| --- | --- | --- |
| RW admission | 100 | 58.56 |
| CREATE (3) | 25 / 20 / 20 | 28.85 / 30.85 / 31.82 |
| three-byte WRITE (3) | 24 / 21 / 21 | 39.52 / 35.28 / 33.67 |
| FILE sync | 1 | 4.72 |
| CHECK (3) | 39 / 39 / 39 | 30.22 / 30.21 / 30.96 |
| RETIRE | 12 | 18.33 |

All measured operations succeed; detached `fsck.fat -n` is clean, with five
files/three allocated clusters. One manually entered END used an incorrect
opaque identifier and is excluded from the comparison; quit performs the valid
END before successful retirement. Raw fixture, commands and GDB captures stay
outside the repo under `/shared/present/fat32-mirror-repair/baseline/`.
All baseline QEMU/GDB jobs stopped before implementation.


## Implementation and matched after-run

Implementation `a28031a0`, fs [#38](https://git.internal/PyxisOS/pyxis-fs/pulls/38)
`101e1f68`; other pins unchanged. Ordinary kernel and full default-image source
builds pass, `make -j8`, existing e0801c2 container. No compiler/container or public
ABI change. Only existing spawn shadow and upstream build warnings remain.
The local functional kernel SHA-256 is
`a337c0b58ca5fd95f381c3558dd4e745cd7766f06bba0933cb9c8a8fb80055bc`.

The initial record misidentified the baseline CI builder as e0801c2. Its bundle
manifest actually says df87791. The local e0801c2 after-run therefore remains
functional/count evidence only; its timings are excluded. For the corrected
comparison, verified exact-head CI API run 2589 (UI 2501), revision `d0c28bab`
(only docs after the implementation), supplies a clean df87791 kernel, SHA-256
`e763842edfaa372dae6fe83307cacea80bfbe20ad09cd60afa7d7eb4c069e713`.

The controlled comparison replaces only the kernel in the baseline archive/ISO.
Both kernels use df87791 and identical configurations. The same pristine fixture,
machine, observers and operation sequence apply; both use reused host artifacts
without a cold-cache reset. No task-owned build or other QEMU was active during
these final runs. After B, repeat untouched A as a control. All operations pass;
both images retire and pass detached `fsck.fat -n`. Raw records are in `ci-after/`
and `ci-control/` beside the original baseline. Earlier runs overlapping CI or
using the different compiler are excluded from timing claims.

| Operation | A → B admissions | Original A ms | B ms | Repeated A ms |
| --- | --- | --- | --- | --- |
| RW admission | 100 → 100 | 58.56 | 64.25 | 68.64 |
| CREATE (3) | 25/20/20 → 24/20/20 | 28.85 / 30.85 / 31.82 | 37.56 / 35.20 / 41.23 | 43.64 / 26.50 / 33.84 |
| three-byte WRITE (3) | 24/21/21 → 23/21/21 | 39.52 / 35.28 / 33.67 | 35.72 / 75.51 / 26.54 | 44.12 / 36.90 / 43.82 |
| FILE sync | 1 → 1 | 4.72 | 4.91 | 4.57 |
| CHECK (3) | 39/39/39 → 39/39/39 | 30.22 / 30.21 / 30.96 | 31.12 / 34.42 / 45.42 | 34.33 / 32.41 / 32.23 |
| RETIRE | 12 → 11 | 18.33 | 16.13 | 16.67 |

Preparing FAT2 from FAT1 avoids the secondary image read on first marking/first
touched FAT sector/cleaning. The repeated A reproduces the request counts and
widens the CREATE range to overlap B. B has a 75.51-ms WRITE outlier and a slower
CHECK sample; neither is attributed. Debugger stops/shared-host variation remain
unisolated, so these profiled spans establish no native speedup or stable
regression. The new 128-MiB-volume tracking bitmap costs 253 charged bytes,
separate from free-map and workspace backing. One-FAT media need no bitmap.
The functional matrix below uses the local e0801c2 source-built images; the
power checks also boot its freshly built complete default image.

## Scratch functional qualification

A pristine GPT clone has only FAT2 changed: its flags/root entry and entries
5,000/200,000 disagree, spanning valid-entry sectors 0/39/1,562. FAT1 is valid.
Native RO admission, enumeration, file reads, root close/reopen and retirement
succeed; the whole image hash is unchanged, no WRITE/FLUSH callback executes,
one mismatch report appears, and retirement releases all charges/instances.

RW CHECK under BEGIN, sync, unchanged resize, same-object alias rename,
already-existing CREATE refusal and BUSY retirement leave all three sectors
pending and image bytes unchanged. Separate first-effective empty CREATE and
file-growth WRITE runs show: FAT1 MARK write → MARK flush → only the three
FAT2 repair writes → repair flush → ordinary data/namespace effects. Growth
writes `abc` at offset 5,000; held FILE reads still return it after rename and
unlink. CHECK validates the detached chain, final drop reclaims it, and a new
file reads `xyz`. After normal retirement both FAT copies are byte-identical,
independent fsck is clean, mtools reads the new file, and all mount backing and
instance counts are zero. The healthy after fixture also passes fsck.

A converse fixture has a root loop in FAT1 with the original valid FAT2. RW
admission returns IO, without writes/flushes or byte changes, and retains no
instance/backing. No selection of FAT2 or chain reconstruction occurs.

Physical Ctrl+Alt+Delete → Shut down on the new full image succeeds with two
data views and an active interval held after a repaired mutation. Passive GDB
observes successful seal, no retirement/revocation, unchanged held views/interval,
and the owned dirty mark cleared. QEMU exits through S5 and detached fsck is
clean. A second power-off run holds the still-unrepaired mount and interval:
seal succeeds with all three sectors pending, no write/flush callback, and a
byte-identical disk image. Native hardware/power-loss/resume are not inferred
from these checks.

The fs dependency's separate manual host checks build all archives/tools with
warnings as errors; strict inspector and raw writer refuse mismatch without
changing bytes. A late-sector-only mismatch forces sector-zero repair after
primary marking. One-FAT admission/create/CHECK/seal succeeds without MIRROR.
Repair read/write/flush failures preserve stopped state, pending bits, plan,
streamed primary buffer, phase and failed sector across refused CHECK/sync/seal,
forget and reopen. These failure checks are host-library observations, not
kernel transport-failure injection or native media qualification. No permanent
test infrastructure or source mirror is added. Independent source reviews find
no blocker in ordering/budgets/failure retention or kernel accounting/lifetime.

Raw images, commands and captures remain private under
`/shared/present/fat32-mirror-repair/`. No real ESP/native stick was accessed.

All owned QEMU/GDB/build jobs are stopped.
