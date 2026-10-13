# FAT32 mutation preflight cost

Owner-assigned investigation, 2026-10-13, before installer task 5 activates
installed configuration publication. **Proposal only; no optimization is
accepted or implemented.** The [baseline](../development/experiments/fat32-preflight-cost.md)
records the exact-main measurements, including the final geometry run after
the owner lifted the container-build timing hold. No native FAT32 timing is claimed.

## Evidence

Mounted mutations perform two full FAT sweeps: `full_check` validates every
chain/directory, detached root and retained object, then `begin_plan` scans all
clusters again to build its free vector (`fs/fat32/mounted.c`). Each FAT cache
miss reads one 512-byte sector and its mirror separately (`volume.c`). The
context/cache is reset for every operation (`base.c`). Native backing callbacks
submit, wait and collect each request serially (`kernel/fs/fat32.c`).

On nearly empty scratch volumes, an empty-file CREATE's preflight admits about
2,525 / 8,073 / 32,269 reads for 40 / 128 / 512 MiB with 512-byte clusters.
Debugger-observed CREATE ranges are 1.20–1.21 / 3.99–7.55 / 14.77–14.84 s.
Only about 15–18 additional requests implement ordinary empty creation. Explicit
sync admits one flush and takes milliseconds in those runs. These are profiled
nested-QEMU observations, with shared-host variation; they are not native rates.

The volume term follows cluster count, not MiB alone. A 512-MiB volume with
4-KiB clusters has 130,811 clusters and about 4,116 preflight requests, much
closer to the small volumes than to its 512-byte-cluster twin. At 1 GiB with
4-KiB clusters, create/write/resize/sync/retire succeed; CREATE is 4.03–6.26 s
with about 8,204 preflight requests. Namespace collision validation can also be
quadratic in directory entries, and each held-object revalidation clears a
volume-sized reachability bitmap. They remain real costs, but the tiny fixtures
isolate the dominant repeated FAT-sweep term.

Workspace also scales with geometry: the complete free vector and conservative
full-FAT prepared-image capacity use about 20.35 MiB at dense 512 MiB. A tiny
write then returns LIMIT because old/new workspace overlap exceeds the shared
32-MiB budget. Dense 1 GiB requests about 40.66 MiB and RW admission refuses
LIMIT after validation. Increasing the lease cannot fix either capacity refusal.

The editor's [128-MiB qualification](../../userspace/config-editor/qualification/README.md)
reported timeout at committed-record sync. Source inspection explains how an
admitted rename can drain past the fixed interval deadline; the following sync
is then refused at admission, before any flush. A separate expired-interval
check confirmed zero block admissions for refused sync; it included operator
idle and is not a reproduction of work alone consuming the lease.

## Revised recommendation (proposal, 2026-10-13)

Per-mutation whole-volume validation is an implementation inherited from the
raw writer, not a requirement to preserve. The owner asks for one validated RW
admission, allocation state maintained for the mount lifetime, local mutation
checks, and an explicit full check at publication start. These changes remain
**proposed; no FAT32 implementation is authorized**.

Consolidate RW admission's existing checks into one full mounted-volume check.
Read FAT copies through bounded windows, initially up to eight sectors per copy
when the backing callback permits it. Compare the same valid entries/mirrors;
keep chain, namespace, orphan and detached-root validation. Build an exact free
bitmap/count from the successful sweep; FSInfo remains a hint. Charge this
retained state to the existing shared 32-MiB backing cap. A dense 1-GiB fixture's
2,064,848 clusters need about 252 KiB for the free bitmap, rather than an
8-MiB vector of every cluster.

The retained RW extent excludes other writers while mounted. Maintain the free
map/count through successful Pyxis transitions; bound candidate searches and
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
recovery backing and cannot be cleared by CHECK. No automatic repair, retry or
reopen-as-reset. Use the owned dirty snapshot and detached roots in revalidation.

Expose that explicit full check on the bound RW mount control. Proposed default:
require both WRITE and INTERVAL, plus the calling process's active delivered
interval. The installer/config publisher calls it once immediately after its
existing BEGIN, before publication effects, inside the same non-extending
30-second lease. A successful check publishes replacement allocation state
atomically; failure preserves recovery and leaves proof invalid. Neither BEGIN
nor SYNC silently becomes a check or renews the lease. The engine already has
`fat32_mounted_check`; the native control operation/helper and publisher call
are new work, coordinated with alpha through the owner.

Keep operation-sized candidates and sector-and-phase images under 32 MiB:
new-directory allocation, parent growth, touched/shrunk/reclaimed chains,
namespace images, dirty marking, FSInfo and sealing all count. Preserve old
workspace on failed replacement allocation and charge old/new overlap. Large
actual mutations may legitimately return LIMIT; dense admission and tiny writes
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
With mirrored FATs and S valid FAT sectors, eight-sector windows reduce the FAT
sweep's backing requests toward 2*ceil(S/8), plus namespace reads; this is a
projection, not a speedup measurement. Complete 128-MiB editor publication must
fit the original lease, including its explicit check, read-back and cleanup.

## Decisions (three proposed defaults)

1. **Validation lifetime. Default:** one full RW-admission check, bounded FAT
   windows, retained free bitmap/count maintained by Pyxis, and local touched
   checks thereafter. Accept the delayed detection of unrelated silent corruption.
2. **Invalidation and publication check. Default:** observable proof-invalid state;
   explicit bound-control CHECK requiring WRITE|INTERVAL and its active owner;
   publisher invokes it after BEGIN once per publication. CHECK may rebuild after
   known pre-effect invalidation, but never resets an uncertain stopped writer.
3. **Backing budget. Default:** retained allocation map and operation-sized
   candidates/prepared images share the unchanged 32-MiB ceiling, including
   replacement overlap and complete uncertainty retention. No volume-size-driven
   prepared capacity for tiny writes and no larger lease.

After this task the owner can: choose the bounded fix before installed
publication. A separate implementation go and alpha coordination are required:
fs, kernel/ABI/helper, then publisher integration. Qualification uses disposable
40/128/512-MiB/1-GiB media, both cluster geometries, dense 1-GiB tiny writes,
fragmented/near-full allocation, held/detached objects, touched corruption, stale
locators, allocation/I/O/uncertain failures and proof rebuild/refusal. Check
corruption outside touched structures at explicit check/remount. Run the actual
128-MiB editor publication within its original lease, verify ordered durability,
power seal/resume and fsck, then owner-scheduled native scratch checks if useful.
No real ESP writes are authorized.
