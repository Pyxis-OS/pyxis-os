# FAT32 mutation preflight cost

Owner-assigned investigation, 2026-10-13, before installer task 5 activates
installed configuration publication. **All three revised defaults accepted, 2026-10-13; implementation authorized.**
The retained-map engine, bounded windows and native CHECK are implemented and
qualified on disposable media. The publisher runs CHECK immediately after
BEGIN; actual 128-MiB editor publication completes under the original lease. The [baseline](../development/experiments/fat32-preflight-cost.md)
records the exact-main measurements, including the final geometry run after
the owner lifted the container-build timing hold. No native FAT32 timing is claimed.

## Evidence

The [baseline and implementation record](../development/experiments/fat32-preflight-cost.md)
separates the original investigation, exact-main refresh and changed-build runs.
Main repeats a volume-wide check and free collection on every mutation; dense
1-GiB admission exceeds the 32-MiB workspace cap. The implementation performs
one admission check, then local checks: at 128 MiB, empty CREATE uses 20–25
requests instead of about 8,088–8,093. Dense 1-GiB admission and tiny writes
succeeded within the then-current cap. These are profiled nested-QEMU results,
not native hardware rates or complete editor publication.

## Accepted contract (2026-10-13)

Per-mutation whole-volume validation is an implementation inherited from the
raw writer, not a requirement to preserve. The owner asks for one validated RW
admission, allocation state maintained for the mount lifetime, local mutation
checks, and an explicit full check at publication start. The owner accepted this contract and authorized its implementation on 2026-10-13.

Consolidate RW admission's existing checks into one full mounted-volume check.
Read FAT copies through callback-bounded windows of up to 64 KiB per copy for
full checks; local mutations keep eight-sector windows. Full-check buffers add
at most 128 KiB of caller-owned scratch. Compare the same valid entries/mirrors;
keep chain, namespace, orphan and detached-root validation. Build an exact free
bitmap/count from the successful sweep. FSInfo signatures remain validated, but
free-count/next-free hints are advisory even when out of range; derive allocation
from the FAT and correct hints with the next confirmed mutation. A dense 1-GiB fixture's
2,064,848 clusters need about 252 KiB for the free bitmap, rather than an
8-MiB vector of every cluster.

The retained RW extent excludes other writers while mounted. Maintain the free
map/count only after confirmed Pyxis transitions; bound candidate searches and
check selected candidates against their FAT entries before effects. Detached
held chains stay allocated until durable final-reference reclamation. Each
mutation checks affected chains, parent directories and introduced/removed
names, and revalidates affected held objects. It does not traverse unrelated
objects or clear a volume-sized bitmap once per held object. Local checks must
still reject loops, invalid entries, stale locators and detected inconsistency.

Any backing I/O error, detected structural inconsistency or uncertain outcome
invalidates the allocation proof. Record this separately from the existing
uncertain, permanently stopped writer. Refusals before effects for authority,
capacity, existing/missing names or no space do not themselves invalidate proof.
An invalid proof permits no mutation/reclamation or clean-mark clearing. A
known pre-effect invalidation may be recovered only by an explicit successful
full mounted check/rebuild; an uncertain mutation keeps its stopped writer and
recovery backing and cannot be cleared by CHECK. At power seal, invalid proof
leaves the dirty mark set after flush; it alone does not refuse clean power.
Unresolved mutation failure still does. No automatic repair, retry or
reopen-as-reset. Use the owned dirty snapshot and detached roots in revalidation.

Expose that explicit full check on the bound RW mount control. Accepted authority:
require both WRITE and INTERVAL, plus the calling process's active delivered
interval. The installer/config publisher calls it once immediately after its
existing BEGIN, before publication effects, inside the same non-extending
30-second lease. CHECK rebuilds in the existing reachability scratch and copies
into retained backing only after full and held-object validation succeeds. No
third full map is needed; admission already reserves the full-check scratch.
Failure preserves allocation bytes/recovery and leaves proof invalid. Neither BEGIN
nor SYNC silently becomes a check or renews the lease. The native `MOUNT_CHECK` operation and `mount_check` helper are implemented.
The config-editor publisher calls it once in the same process, stops honestly
on failure and releases the interval through its existing cleanup path.

Owner update, accepted 2026-10-14: remove the fixed aggregate 32-MiB cap. Charge
backing to its mount: one retained free map, current workspace and actual held
chain capacity. Each map is one bit per cluster and legal geometry bounds a
copy to about 32 MiB. The per-volume sanity ceiling derives from cluster count,
maximum legal operation sizing and the existing wrapper limit, including real
replacement overlap; it is not an allocation target or reservation. Machine
allocation failure returns NO_MEMORY. CHECK needs no additional bitmap allocation.
The work allowance includes the mandatory FAT sweep in addition to the existing
namespace allowance; the deadline and lease stay unchanged.

Keep operation-sized candidates and sector-and-phase images:
new-directory allocation, parent growth, touched/shrunk/reclaimed chains,
namespace images, dirty marking, FSInfo and sealing all count. Preserve old
workspace on failed replacement allocation and charge old/new overlap. Large
actual mutations may exhaust memory or finite engine work bounds; tiny writes
must not reserve every FAT sector. Preserve durable phase ordering, dirty marking,
failed-state retention, held handles, clean-power sealing and retirement rules.

## Residual risk and expected cost

This relies on exclusive ownership and correct Pyxis transitions between full
checks. A free bitmap proves availability, not every object's allocation owner.
A Pyxis bug causing silent in-session corruption, including a coherent cross-link
to an unrelated allocated chain, may be detected only at explicit full check or
remount. Local checks do not reprove whole-volume consistency. Foreign changes
between boots are examined at admission; external concurrent modification while
mounted is unsupported.

Ordinary mutation work should follow touched chains, parent directories and
changed sector-and-phase images, plus free-map searches, rather than two full
FAT sweeps. Explicit check/admission still costs O(volume + namespace/chain work).
With mirrored FATs and S valid FAT sectors, W-sector callback-bounded windows
reduce the FAT sweep's backing requests toward 2*ceil(S/W), plus namespace reads;
W is at most 128 for full checks. This is a
projection, not a speedup measurement. Complete 128-MiB editor publication must
fit the original lease, including its explicit check, read-back and cleanup.

## Decisions (accepted 2026-10-13)

1. **Validation lifetime. Accepted:** one full RW-admission check, bounded FAT
   windows, retained free bitmap/count maintained by Pyxis, and local touched
   checks thereafter. Accept the delayed detection of unrelated silent corruption.
2. **Invalidation and publication check. Accepted:** observable proof-invalid state;
   explicit bound-control CHECK requiring WRITE|INTERVAL and its active owner;
   publisher invokes it after BEGIN once per publication. CHECK may rebuild after
   known pre-effect invalidation, but never resets an uncertain stopped writer.
3. **Backing budget. Accepted 2026-10-14:** per-volume geometry-derived accounting
   and sanity bounds replace the arbitrary aggregate cap; maps use one bit per
   cluster, with two copies needed for admission/CHECK. Charge operation-sized
   candidates/images, retained chain capacity, replacement overlap and complete
   uncertainty retention; actual allocation failure is NO_MEMORY. No volume-size-driven
   prepared capacity for tiny writes and no larger lease.

After this task the owner can: publish configuration on supported large scratch
ESPs under the original lease. Implementation is authorized; delivery order is
fs, kernel/ABI/helper, then publisher integration coordinated with alpha through
the owner. Qualification uses disposable
40/128/512-MiB/1-GiB media, both cluster geometries, dense 1-GiB tiny writes,
fragmented/near-full allocation, held/detached objects, touched corruption, stale
locators, allocation/I/O/uncertain failures and proof rebuild/refusal. Check
corruption outside touched structures at explicit check/remount. Run the actual
128-MiB editor publication within its original lease, verify ordered durability,
power seal/resume and fsck, then owner-scheduled native scratch checks if useful.
No real ESP writes are authorized.

- [x] Implement and qualify the retained allocation proof and bounded workspaces
  in pyxis-fs.
- [x] Add observable invalid state and the native bound-control CHECK ABI/helper.
- [x] Coordinate the once-per-publication CHECK with alpha and qualify the actual
  128-MiB editor publication under the unchanged lease.


## Assigned follow-up: FAT mirror repair

Owner direction accepted 2026-10-14: when mirrored FATs disagree, FAT1 is
primary; a mismatch alone must not refuse RW admission. Report the mismatch
once and repair FAT2 from FAT1 with the next confirmed mutation. CHECK still
validates FAT1's own structure. **This follow-up is not implemented.**

Inspected baseline: Pyxis `dabfc7ca`, fs `d84f8084` (also dependency main).
`fat32_fat_entry` in [volume.c](../../fs/fat32/volume.c) currently compares the
valid 28-bit entries and returns CORRUPT on disagreement, including during
geometry admission. The mounted engine subsequently uses the same reader for
CHECK, chain reads and local mutations. Its
[write planner](../../fs/fat32/mounted.c) writes both mirrors and durably marks
dirty before effects. Those dirty/seal, allocation-proof, interval, retained
handle and uncertain-writer contracts stay in force. FAT1 corruption, bad
geometry or actual backing I/O failure must still refuse honestly; no choosing
a more convenient FAT or reconstructing chains from FAT2.

Small scope: a mounted-view policy in the shared engine and kernel adapter,
with no new authority or public mount operation. Retain a bounded map of
mismatching sectors containing valid FAT entries, charged to the mount from
geometry; unused overprovisioned FAT sectors must not inflate it. Reader and
CHECK results expose pending mismatch information to the adapter, which logs
once per mount without putting logging in the freestanding engine. Repair
uses bounded caller-owned buffers, not a whole-FAT image or extra allocation
bitmap. Allocate/charge needed backing before effects; failure remains honest
NO_MEMORY/LIMIT. Healthy mirrors keep their ordinary mutation path.

Two proposed defaults await the owner before code:

1. **Where tolerance applies. Default:** native mounted RO and RW views read
   FAT1 and retain/report a mismatching FAT2. RO never repairs, including CHECK,
   sync or release. Keep the standalone strict inspector and raw scratch writer
   strict, preserving their independent validation role; the installer's lenient
   recovery reader remains separate. This confines the accepted mounted recovery
   policy without silently changing every shared-engine caller.
2. **Repair boundary. Default:** finish ordinary local validation and resource
   preparation first. On the first effective mutation, durably mark dirty, repair
   only the mismatching FAT2 sectors from current FAT1 through bounded buffers,
   and flush the repair before publishing file/namespace effects. Prepared
   mirrored updates must derive from the repaired primary image, not stale FAT2.
   Clear pending state only after confirmed repair; unconfirmed repair write or
   flush freezes the writer with its phase/sector and recovery backing, as today.
   A rejected/no-op operation, CHECK, ordinary sync, power seal or retirement
   does not initiate repair. No fallback, automatic retry or lease extension.

After this task the owner can: mount a structurally valid FAT1 despite a stale
mirror, read it without repair, and have its first authorized mutation reconcile
the mirror durably. This is not general fsck, dirty-volume recovery or native
ESP write authorization.

Delivery: fs dependency PR first, then the parent pin/kernel integration. No
userland/ports dependency is expected unless inspection finds a real helper
consumer change. Baseline the healthy admission/CHECK/tiny-write path before
code and repeat matched runs after. Qualification uses disposable GPT VirtIO
images: mismatched allocation/chain and reserved clean-bit entries, valid FAT1
with invalid FAT2, invalid FAT1 with valid FAT2, one-FAT media, RO no-write
comparison, first-mutation repair/independent fsck, later mutation/CHECK,
held-file and clean-power behaviour, and repair failure retention through the
existing inspection tools. Keep validation/work bounds and confined extents;
no writes to a real ESP or native stick. Timing hold is lifted, but no baseline
or implementation starts before the two scope defaults are settled.
