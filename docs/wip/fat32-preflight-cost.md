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

## Smallest recommended change

Retain a fresh whole-volume preflight for every mutation. Collect required free
candidates during the checker's existing final consistency sweep. Store only the
operation's bounded allocation demand, but continue the entire sweep for the
exact free count and global validation; consume candidates only after all
namespace, orphan, mirror and held-object checks succeed. Remove the redundant
second FAT sweep. Preserve ascending allocation order. Do not replace the preflight with a persistent validation proof
or trust FSInfo's free count as authority.

Add an operation-local FAT window: initially up to eight sectors per copy,
bounded by callback transfer limits, FAT-copy bounds and remaining valid entry
span. Read primary/mirror windows in bounded runs and compare the same valid
low-28-bit entries, including reserved entries. Invalidate on context reset and
revalidation. Preserve per-entry work charges and actual-sector accounting.
Two 4-KiB windows add bounded scratch, not retained mount cache or new authority.

Also size allocation candidates and prepared sector images for the operation's
actual growth/touched chains rather than reserving every FAT sector. Allocation
bounds include new-directory allocation and up to two parent-growth clusters;
detached victim chains remain allocated until durable final-reference reclamation.
Prepared capacity counts sector-and-phase images, since one sector can retain
different versions across phases. Include shrink/reclamation chains, namespace,
dirty marking, FSInfo and sealing, not just payload growth. Determine
and validate bounds before effects; retain all prepared images and recovery
backing after uncertainty. Preserve successful workspace on failed growth and
charge replacement overlap within the existing global cap. This must address
the tiny-write growth refusal, rather than hiding it with larger clusters or
raising the cap. It requires explicit engine/adapter sizing work, not just a
larger cache constant. Dense admission and tiny operations should avoid
volume-sized candidate/image reservations; large actual mutations may still
legitimately return LIMIT under the unchanged cap.

For mirrored media with S valid FAT sectors, the dominant sweep request count
could fall from about 4S to 2*ceil(S/8) when the callback accepts eight-sector
transfers, plus namespace/held-object/preparation reads. CPU validation remains O(cluster count + namespace/chain work), and
mutation effects/barriers depend on changed data. This is roughly a 16-fold
reduction of the volume-sized request term, not a measured speedup. For the
small 128-MiB operation, several hundred milliseconds rather than several
seconds is an estimate to validate; complete editor publication must still fit
the unchanged 30-second interval with read-back and cleanup included.

## Decisions (proposed defaults)

1. **Validation strategy. Default:** retain full per-mutation validation; merge
   free collection into its sweep and use operation-local bounded FAT windows.
   A persistent mount allocation/validation proof could remove volume-sized work,
   but changes when unrelated corruption is detected; park it for this fix.
2. **Workspace scope. Default:** include work-sized candidate/prepared-image
   capacity and growth in this first fix, keeping the shared 32-MiB ceiling and
   failed-state retention. A latency-only slice would leave measured large-volume
   RW refusals; explicitly document those if the owner chooses that narrower scope.

Publication authority, same-process ownership, non-extending lease, durable
phase barriers, admission/drain ordering, held-handle semantics and clean-power
sealing stay as accepted. No publication-semantic change is proposed. Route any
later semantic finding through the owner to alpha; do not silently change the
editor or extend its lease.

After this task the owner can: review the measured bottleneck and choose the
bounded fix before installed publication. Implementation needs a separate go:
fs dependency first, then the kernel/SDK integration. Qualification must include
all four capacities, geometry contrasts, dense tiny writes, malformed metadata,
allocation/uncertain-outcome preservation and the actual editor's complete
128-MiB publication under the original lease. No real ESP is written; native
scratch qualification is owner-scheduled if needed.
