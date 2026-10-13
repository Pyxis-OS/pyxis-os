# FAT32 mutation preflight cost

Owner-assigned investigation, 2026-10-13, before installer task 5 activates
installed configuration publication. **All three revised defaults accepted, 2026-10-13; implementation authorized.**
No optimization is implemented yet. The [baseline](../development/experiments/fat32-preflight-cost.md)
records the exact-main measurements, including the final geometry run after
the owner lifted the container-build timing hold. No native FAT32 timing is claimed.

## Evidence

`full_check` validates every chain/directory and retained object; `begin_plan`
then scans all clusters again for its free vector (`fs/fat32/mounted.c`). Each
miss reads one 512-byte FAT sector and its mirror separately (`volume.c`);
operation contexts reset the cache. Native callbacks submit/wait serially.

An empty CREATE's preflight admits about 2,525 / 8,073 / 32,269 reads on dense
40 / 128 / 512 MiB. Profiled nested-QEMU CREATE ranges are 1.20–1.21 /
3.99–7.55 / 14.77–14.84 s; only about 15–18 further requests implement creation.
Explicit sync admits one flush. With 4-KiB clusters, 1-GiB operations succeed
with about 8,204 preflight requests. Cost follows cluster count, not MiB alone.
These ranges include debugger/shared-host variation; they are not native rates.

The full free vector and conservative prepared capacity use about 20.35 MiB
at dense 512 MiB. A tiny write returns LIMIT because old/new overlap exceeds
32 MiB; dense 1-GiB RW admission requests about 40.66 MiB and refuses LIMIT.
The editor's [128-MiB qualification](../../userspace/config-editor/qualification/README.md)
stopped at committed-record sync: admitted work can finish beyond the lease,
then sync is refused before I/O. The baseline's separate expiry check included
operator idle and is not a complete editor reproduction. A longer lease fixes
neither repeated work nor capacity refusals.

## Accepted contract (2026-10-13)

Per-mutation whole-volume validation is an implementation inherited from the
raw writer, not a requirement to preserve. The owner asks for one validated RW
admission, allocation state maintained for the mount lifetime, local mutation
checks, and an explicit full check at publication start. The owner accepted this contract and authorized its implementation on 2026-10-13.

Consolidate RW admission's existing checks into one full mounted-volume check.
Read FAT copies through bounded windows, initially up to eight sectors per copy
when the backing callback permits it. Compare the same valid entries/mirrors;
keep chain, namespace, orphan and detached-root validation. Build an exact free
bitmap/count from the successful sweep; FSInfo remains a hint. Charge this
retained state to the existing shared 32-MiB backing cap. A dense 1-GiB fixture's
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

## Decisions (accepted 2026-10-13)

1. **Validation lifetime. Accepted:** one full RW-admission check, bounded FAT
   windows, retained free bitmap/count maintained by Pyxis, and local touched
   checks thereafter. Accept the delayed detection of unrelated silent corruption.
2. **Invalidation and publication check. Accepted:** observable proof-invalid state;
   explicit bound-control CHECK requiring WRITE|INTERVAL and its active owner;
   publisher invokes it after BEGIN once per publication. CHECK may rebuild after
   known pre-effect invalidation, but never resets an uncertain stopped writer.
3. **Backing budget. Accepted:** retained allocation map and operation-sized
   candidates/prepared images share the unchanged 32-MiB ceiling, including
   replacement overlap and complete uncertainty retention. No volume-size-driven
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

- [ ] Implement and qualify the retained allocation proof and bounded workspaces
  in pyxis-fs.
- [ ] Add observable invalid state and the native bound-control CHECK ABI/helper.
- [ ] Coordinate the once-per-publication CHECK with alpha and qualify the actual
  128-MiB editor publication under the unchanged lease.
