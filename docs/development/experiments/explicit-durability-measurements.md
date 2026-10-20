# Final explicit-durability measurements

Task 4 of [explicit durability](../../wip/explicit-durability.md), assigned
2026-10-19. **Preparation and qualification record; final repeated measurements
and the scheduled native run are not complete.** Earlier results remain in the
[task-1/2/2b record](explicit-durability-baseline.md) and
[FAT32 task-3 record](fat32-batched-ordering.md).

## Matched inputs

The old tables are not a matched four-state series: tasks 1/2/2b used VM-local
Btrfs, task 3 shared FUSE. Earlier extraction controls have only one sample;
the published task-2b numbers predate its staged-home reuse fix. Do not relabel
those results or pool them with new timings.

New inputs recovered from successful project CI:

| State | Actual kernel commit | fs pin | Artifact / caller group |
| --- | --- | --- | --- |
| Legacy original (native USB-confounded) | `a29372f8dd00` | `25337999c256` | CI 3139; userland `22a3ee7d`, SDK 3139 |
| Clean npfs baseline (replacement) | `321c4791c7d3` | `25337999c256` | local matching-config build; same early callers; QEMU contents qualified |
| Namespace batching | `7ffc03f656d8` | `25337999c256` | CI 3206; same early callers |
| Corrected file batching | `89c86f740d2c` | `25337999c256` | CI 3243; same early callers |
| Before FAT32 batching | `4c97c87b6189` | `d2fb97c5bacc` | saved binary; userland `1836d2ae`, SDK exported at `19c3cfc8` |
| FAT32 batching | `4e21c105b63b` | `f3f22b6e07e7` | saved binary; same later callers |

Each binary has one actual build commit and fs pin. `4c97c87b` is available as
a complete local Git object; its tree differs from reachable `19c3cfc8` only in
the durability WIP document. Its commit object and source tree are preserved in
`/shared/present/durability-measurements/review-correction/`. `016e12bc` still
pins the old fs engine and is not the measured FAT32 candidate's identity.
SDK manifests identify the caller export, not each paired kernel.

The later caller group reflects the FILE INFO layout change; never exchange it
with the early group. The legacy `a29372f8` to task-2b native comparison also
changes xHCI/core event and bulk-transfer paths used by the USB stick. Its native
observations cannot isolate durability and must stay explicitly confounded.
Nested virtio-blk observations do not establish absence of that USB confound.
The replacement `321c4791` to `89c86f74` changes only npfs implementation among
compiled kernel inputs. Its fs/lwIP pins, ABI, compiler, generated configuration
and flags match; only the baseline kernel needs rebuilding. The existing FAT32
pair is clean and reusable. The replacement baseline passed its zero-write
clone plan and full QEMU Update/restart/content qualification on the captured
30,765,219,840-byte geometry. All 2,240 programs matched, FAT and npfs were clean,
and only the expected new/previous revisions remained. Kernel SHA-256:
`4c4fa893ff56668e6f1cdfb0c5a85310e4d877ac99fab961e819b1efde4a61e0`.
Records: `/shared/present/durability-clean321-qualification/`, final collector
`collection-exact-geometry/qualification.json`. This was correctness qualification
under full horse CPU load; its clocks are excluded from performance results.

Published archives and SDKs were recovered together. No independently published
compiler/toolchain is rebuilt. Private builds use existing builder
`pyxis-llvm23.1.3-3d69e83` and matching SDK/Lua/Mbed TLS exports. Builds made under
full horse CPU load are correctness checks; their duration is not a result.

The original extraction archive is unavailable on this VM. The fixed replacement
is 293,416,960 bytes, SHA-256
`a94062826a98b2f099f0839f078938396ad3857243cae6487ff98c8d0c901787`,
6,488 regular files / 288,272,949 file bytes. It is the retained hosted-driver
payload, used only as extraction data. Its times cannot replace those for the
older 369,326,080-byte payload.

## Installer clock capture

Owner accepted 2026-10-19: labelled, **never-merged** installer measurement
builds, reusing existing clock readings. Stock Install init does not configure
IPv4; `log.udp=1` alone therefore cannot supply the former native sheet's
promised live timestamps. No kernel probe or network authority is added.

The same source patch applies to both frozen caller groups. It retains at most
128 completed phase records and six boundary timestamps, uses only existing
observer BEGIN/END clock reads, and prints selected results after successful
execution. Clock/capacity failure invalidates the observation, not the install.
The summary goes to the existing terminal/diagnostics destinations. No timing
output is added during executor work.

Individual transfer phases end **before** their following file sync. Compare
the aggregate envelopes instead: program copy through the start of verification;
ESP staging through the start of boot-payload verification; and old-revision
removal through the start of cleanup-directory close. These include the relevant
publication barriers and intervening work. They exclude unrelated startup and
final ownership cleanup; no whole-operation wall-time claim is made.
A RAM-workspace copy produces a fourth staging row, so rows are identified by
bytes/order rather than assumed to be exactly three disk writes.

Patch: `/shared/present/durability-baseline/installer-timing-original.patch`
(and byte-identical `installer-timing-later.patch`), SHA-256
`2d7199f319ddd7b2a431d2f8ebb75cd6461fa76c888c0bac785250169a6b3d78`.
The source patch and private archives stay outside production pins/branches.
Build commands/logs, kernel/archive identities and QEMU command lines are under
`/shared/present/durability-measurements/`; native artifact hashes are in
`/shared/present/durability-baseline/SHA256SUMS` and each variant directory.

## Fixture and qualification

The VM's unavailable old fixtures were rebuilt through actual classic Install
with `82f9387d`, then Update with `e9deaa8d`, using the same original archive.
After clean restart: 4 GiB disk, 512 MiB ESP, 28 MiB journal, both pool headers
valid, journal EMPTY, feature masks zero, host fsck passes. The retained revision
has 2,241 files including its manifest, 48,732,059 bytes, 13,291 blocks in 2,396
physical runs; 144 split files, maximum six runs, no holes. This is seed layout,
not the final workload result.

QEMU qualification uses nested KVM, q35/max, four vCPUs, 4 GiB, fresh OVMF,
VirtIO block/net/RNG, 512-byte sectors and a 64 KiB block transfer, raw
cache=writeback backing on **shared FUSE**. Separate QMP monitors allow block
counter sampling and manual UI input without a monitor-client conflict.

- The initial original-writer clock-summary build completed Update and clean
  restart. Its first summary lacked aggregate intervals; it is qualification
  only, not a final matched timing sample.
- The final later-group timing build completed Update on the original 4 GiB
  seed, verified program/boot bytes and produced all three aggregate intervals
  on one summary screen. Clean restart completed. Its preparation overlapped
  host packet work; this is qualification, not a performance comparison.
- A stopped-file capture and exact-geometry restore passed host fsck, GPT
  verification and verification of every restored live byte. Capture requires
  exactly the two seed revision directories and an empty home volume.
- A 4 GiB fixture with GPT backup moved to a 64 GiB image was correctly refused
  by Update as not matching the classic installer layout. It made no install
  changes and is excluded. Native setup uses the real disk geometry instead.

The final earlier-group summary also passed Update with corrected task-2b.
After clean restarts, all three stopped qualification pools passed fsck without
replay. Independent host SHA-256 comparison against the frozen source trees
verified every copied program: 2,240 files / 48,336,602 bytes in each original
caller run and 2,246 / 49,696,926 in the later run. FAT kernel hashes match the
selected ELF inputs. Layout reports are retained alongside the raw records;
archive/kernel growth can add FAT chain runs, so this fixture does not imply
that all final files remain contiguous.

The separate extraction archive and paced Remote client also passed with the
corrected task-2b kernel: preparation, mkdir, cd, tar, explicit sync and exit all
completed with status zero and full final output drainage. The qualification's
single tar time was 380.520 s, sync 0.144 s; **not a repeated comparison**, and
host preparation overlapped this run. After guest power-off, fsck passed without
replay. Independent SHA-256 comparison against the pinned tar members verified
all 6,488 files / 288,272,949 bytes. Its measured layout has 73,960 allocated
blocks in 6,889 runs, 281 split files, maximum 46 runs and no holes. These are
qualification/layout observations, not a speedup claim.

Two failed extraction preflights are excluded: a host hard-linked staging copy
produced unsupported newc link records, then a one-space profile omitted network
configuration. The final archive uses independent source inodes and the ordinary
network-owning Remote startup; no production code was changed.

These checks do not qualify physical USB behavior, native performance or
interrupted-restoration recovery. Final matched repeats remain pending.

## Hands-free native harness

Owner accepted decisions 1–3 and confirmed the **32 GB SanDisk is disposable**,
2026-10-19. The [harness contract](durability-native-harness.md) replaces the
hands-on sheets: an off-target controller drives Fedora/BootNext/PXE, collects
nonce-matched results from the scratch ESP and restores the reviewed default at
chunk boundaries. Two samples per compared state, bounded additional thirds,
early regression stops and a ten-minute cleanup reserve are enforced. The owner
extended the 2026-10-19 evening campaign from the original 45-minute/20:00
limits to an absolute **23:00 cleanup-complete deadline**, after one owner F12
boot into Fedora; the controller may then proceed through seeds, capture and
measurements without more owner action. Stop earlier on completion, regression
or insufficient next-phase allowance.

The private kit is `/shared/present/durability-handsfree/`. Its separate control
archive delegates unchanged frozen payloads; no production code, kernel probe,
public ABI, dependency pin or compiler-container change is included in this PR.
Both private caller groups build; the metadata-source warning in the imported
build is recorded in the packet, while source/SDK provenance is explicit.

Nested-QEMU functional checks on exact-geometry 32 GB images passed automated
seed install/update, normal restart, stopped capture/restore and clone-only
portable plan generation with zero target writes. Final samples for both caller groups passed
all frozen program path/size/hash checks (2,240 earlier / 2,246 later), boot-ELF
hash, clean FAT/npfs, layout, exact retained revisions and the current prepared
result protocol.
Trailing plan data and repeated seed-update were refused with no target writes,
result publication or restart. The RAM-only preflight passed a real Remote nonce
echo and normal restart. The controller's 71 local checks use a fake transport;
Fedora read-only prerequisites passed natively, and BootNext reached the RAM-only
Pyxis preflight. Automated nonce proof failed; manual Remote recovery returned
Fedora, and explicit cleanup verified the restored normal PXE/default order.
The first failed attempt remains unaccepted; it created no native seed or timing sample.
The second attempt did echo the nonce and reboot back to fresh Fedora, but the
helper falsely required TCP EOF after native reset. It too stopped at a verified
clean boundary with no seeds/samples. The corrected retry retains its client
until the controller observes fresh Fedora; no TCP EOF or horse sudo is needed.
The third campaign subsequently passed preflight, both untimed native seeds
and fixture capture. Its first measured sample failed during the Fedora restore
helper, before the measured boot; no native timing sample is accepted. Normal
PXE source/menu restoration passed. The orchestrator subsequently verified
firmware recovery (PXE-first, empty BootNext, Fedora running) and a read-only
comparison of every captured live byte. The restore failure remains unattributed;
authentication expiry is not confirmed. The campaign is parked without retry.
It uses fresh nonces/beacon and leaves the previous failed ledgers unchanged; see the
[harness record](durability-native-harness.md#qualification-and-remaining-native-gate).
Private hook patches (applied after the common timing patch) are recorded in
`patches/SHA256SUMS`: earlier
`13e46ebc759d29be3aec66cf3153fdadd733ed54ca4f95d4bddc47e76bfb775d`, later
`13e0ac19e54011acbf60b3b614b1dac8e7090ba8e83b2b992cdc40acb299595b`.
The packet's qualification summary binds these to its frozen binaries; no
functional-run times enter performance tables.

QEMU repeated measurements were paused for the harness work: one completed
Update each for original, task 2 and corrected task 2b. All three restarted
cleanly, passed fsck without replay, matched every source program and the FAT
kernel, and retained exactly the new and previous revisions. These single
samples are not a repeated comparison; the remaining Update/extraction repeats
and native evidence are incomplete. Raw records are under
`/shared/present/durability-measurements/repeat-update-*/`.

Task 4 stays unchecked; completion rewrite/move awaits the final evidence and
explicitly recorded coverage limits. The orchestrator schedules/stages native
chunks after Wi-Fi-priority work; only the explicitly authorized private harness
performs native media/PXE actions.

Owner authorized the 2026-10-20 measured-only resume until noon, with the same
cleanup reserve and early-stop thresholds. Validated seeds/fixture/preflight are
imported through hashed receipts into a fresh ledger; no old failed operation or
measured sample is reused. The logged agent deploys separately from the frozen
kit. See the [resume contract](durability-native-harness.md#accepted-measured-resume-owner-2026-10-20).
Native measurements remain pending until accepted from that campaign.

The Oct20 measured-only attempt stopped before its first measured boot. Logged
BLKRRPART/EBUSY was traced to reopening the helper's exclusively held disk;
firmware/source cleanup verified at08:02:42, with zero accepted samples. A new
fresh-ledger campaign fixes the descriptor use and verifies the kernel partition
view; it retains all measured payloads, seeds and fixture identities. Its local
Linux loop checks are functional evidence, not native performance measurements.

Round2's corrected native restore passed. Its sample was rejected for absent
guest result after the reported network wedge; source/firmware cleanup passed
at08:13:40. The owner authorized a fresh router-LAN campaign about08:57, retaining
all frozen measured payloads and fixture identities. No native timing sample is
accepted from either morning attempt. See the harness record for routing and
provenance checks; switch failure remains the owner's hardware hypothesis.

## Review checkpoint (2026-10-20)

The r3 native run was stopped at a clean phase boundary for the baseline
confound. Two samples completed full content/cleanup checks; they remain
exploratory observations, not durability-attributable results. No sample3
started. Source/menu and firmware defaults were verified restored at09:19:25,
with Fedora running. The owner confirmed AC for that run; no automatic AC
measurement existed, so do not relabel it as instrumented evidence.

Owner accepted the revised policy on 2026-10-20: ABBA, one primary
envelope per pair (`copy_sync` npfs; `esp_sync` FAT32), existing early-stop
thresholds on that primary only, AC-online checks before/after every sample and
a fixed 30-second idle after restore. Secondary envelopes remain descriptive.
Before/after Fedora AC checks cannot prove uninterrupted power during Pyxis.
The replacement private harness implements these controls. Local fake-transport
checks pass: 71 policy, 26 resume, 15 AC-context, 14 partition refresh and seven
child-logging checks. On hardware release at about 09:55, the fresh campaign
`/shared/present/durability-handsfree-abba/` was prepared with imported validated
seeds/fixture, historical nonce quarantine and a newly hashed helper bundle.
Native acceptance remains separate; no sample is claimed before collection.

All three native envelope clocks cover ThinkPad RAM-archive reads, USB writes
and local syncs. PXE/TFTP, controller wait and collection are outside them.
Horse-dependent wall times and QEMU timings taken during its full CPU build
must be excluded or labelled loaded/noncomparable; no such new timing result
is claimed here.
