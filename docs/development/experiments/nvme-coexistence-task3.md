# NVMe coexistence task 3 — installer qualification

Baseline captured before implementation, 2026-10-15: main/kernel `14f900785097`,
userland `f239d4796b79`, fs `101e1f68f7cf`, ports `0e760128869e`, lwIP
`a1aadb91a503`. All four component bundles came from successful main CI API run
2652. `make -j8 image PREBUILT="kernel sdk userspace ports" BOOT_MENU_TIMEOUT=5`
verified their inputs/payloads. A separate exact-source installer link map was
built with the pinned LLVM `df87791` container; its P1F bytes matched the image's
installer. No compiler-container rebuild or new measurement infrastructure.

QEMU 10.2.2, q35/nested KVM, host CPU, four vCPUs, 1 GiB, fresh OVMF variables,
modern VirtIO block with `cache=none`, VirtIO entropy, SCSI boot CD and xHCI USB
keyboard, no NIC/audio. Private Limine selection starts stock
`init=boot://init-install.pxe boot.install=1`; no source or program changes.
No task-owned build runs during measurement. Shared-host load is uncontrolled.

Install uses a blank sparse 2 GiB disk, Read the room, 12 MiB journal, confirmed
Review and `wipe`. Update uses a disposable prior installation at
`fecd14395813`, with its original normal/rescue menu restored after the earlier
rescue-only qualification, confirmed Review and `update`. Preserve these exact
starting fixtures for matched/interleaved after-runs. Only scratch images are
attached; no native disk/ESP access.

Hardware execution breakpoints at the exact link-map addresses of
`install_plan_execute` and `offer_restart` record host monotonic time. No target
function calls or memory writes; manual Review/typing/restart time is excluded.
Existing QMP block statistics record cumulative target I/O through completion,
including read-only discovery/planning. End-of-run PMM backing is read through
the monitor; it is whole-system allocated backing, not process RSS or a peak.

| First baseline sample | Apply seconds | Target read bytes | Target write bytes | Write operations | Flush operations | End allocated frames |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Install | 163.748 | 136429568 | 257016320 | 44516 | 18506 | 53998 |
| Update | 168.489 | 145795072 | 257002496 | 44523 | 18514 | 53998 |

Each flow copies/verifies 2,228 regular files / 47,957,417 bytes and passes raw
EFI verification (5/5 paths) before reporting success. Update recognizes the
prior installation without recovery/legacy-scan consent; its plan reports five
updated roots and 48 unchanged. This first pair establishes the control, with
no variation estimate yet. Further A/B repetitions belong with integration;
these are not native NVMe throughput or hardware-write qualification results.

Raw images, link map, serial/monitor/debugger captures and screenshots remain
local in ignored `build/baseline/`. Baseline QEMU/GDB jobs are stopped.

## Authority and engine qualification

Authority checks at kernel `4586455d` and userland `5f29575` use the same
four-CPU/1-GiB guest with a disposable 2-GiB NVMe image and private
`nvme.write=1`. The synthetic ESP/MSR/Windows-data GPT is not copied native
media or bootable Fedora. The private interactive ABI probe is absent from
shipped sources and archives.

- APPEND succeeds on 512/4096-byte devices; occupied and overlapping proposals
  refuse. Both GPTs verify; only the chosen slot and required CRC fields change.
- Returned pool authority stays confined through release, aliases and reclaim.
  Stale/copy-consumed review grants refuse. Trusted coexistence admits APPEND
  and denies ERASE; classic does the inverse. Ordinary whole-disk and
  undelegated raw-partition opening refuse.
- Read-only selection and Cancel leave the whole image unchanged. Confined raw
  bootstrap creates/rereads a file; independent FAT check and mtools agree,
  with every outside-partition byte unchanged.

Daybreak separately qualified the later released-handle read correction; this
record does not claim to rerun Daybreak's adversarial reproduction. Uncertain
write quarantine has source review without injected-failure qualification.
[512/4096-byte engine and native-adapter checks](fat32-4kn.md) use fs `35582347`.

## Integrated installer qualification — 2026-10-15

The common frontend/frozen plan/executor uses native FAT files: classic fixed
paths until task 5, coexistence immutable attempts plus saved/printed manual
entries. Ordinary `make -j8 image` passes with existing builder `df87791`.
The earlier integrated source is parent `2f471d54` / userland `6946cc3`, based
on main `390b3139` with merged shared stager #880, audio #881 and idle blanking
#886. It pins fs `35582347` (including merged #39), ports `318c5c39` and
unchanged lwIP. All four exact-head CI checks pass. Earlier source revisions
are identified per flow below; the epoch read proof/invalidation is preserved.

Private ISO menus select the ordinary trusted installer; only scratch NVMe
qualification adds `nvme.write=1`. The private menu also disables idle
blanking. No compiler rebuild or modified installer probe is used. All runs
below are functional, with
debugger pauses/concurrent host work, **not matched performance samples**.

| Flow | Revision | Observed result |
| --- | --- | --- |
| Classic 512 Install | `3f180be8` / `91f2fb6` | Review/Cancel leaves blank 2-GiB image zero; confirmed Apply copies/verifies 2,228 files / 48,094,844 bytes and fixed boot payloads; installed UEFI shell and clean poweroff. This predates final deadline/capacity/profile-grant corrections. |
| Classic 4Kn Install | `5b210c20` / `ad4572b` | Corrected source copies 2,228 files / 48,129,564 bytes; full verification and unchanged interval pass; installed UEFI shell and clean poweroff. |
| Healthy 512 Update | `5b210c20` / `ad4572b` | Native preservation Update completes; unrelated `EFI/FOREIGN/keep.txt` unchanged; new kernel equals source. |
| Synthetic NVMe coexistence Install | `d52f3529` / `6b04b54` | Both payload verification passes complete; saved manual entry printed; existing boot menu unchanged. Staged GUID entry boots the matching installed `bin` revision via VirtIO and powers off cleanly. An initial NVMe-attached boot stops before Caelum; later controlled NVMe loads pass (see the separate firmware record). |
| Explicit classic 4Kn recovery | `425f95f1` / `9f5b02b` | Fresh reviewed recovery on a copy of the retained failed image completes both hash passes and END within the original interval; target-only installed UEFI boot and clean poweroff pass. |
| Synthetic NVMe coexistence Update | `2f471d54` / `6946cc3` | Selected pool 4; 2,228 files / 48,129,580 bytes; both verification passes and original interval pass; old attempt and foreign files preserved. Installer and selector exit 0. New manual entry boots the matching installed revision via VirtIO and powers off cleanly. |

Every listed completed staging run retires with zero FAT instances/charged workspace
in GDB. Independent `sgdisk --verify`, `fsck.fat -n` and mtools agree; installed
kernels equal their ISO sources. Classic menus contain exactly normal/rescue
entries and fixed paths, with no attempt reference. Target-only classic boots
use explicit VirtIO boot order and `-nic none` to avoid firmware PXE fallback.

The coexistence comparison independently checks every disk byte outside the
new pool and precisely admitted additions: only GPT slot 4 and CRC words,
FSInfo hint fields, new FAT allocations and the added owned ESP tree differ.
Original GPT entries/padding, PMBR, foreign partition data, allocated FAT chains
and all four foreign-file sentinels are unchanged. Standard newc parsing
confirms expected program removals; all 1,040 untransformed retained archive
records equal the ISO source, with only the intended installed-configuration
serialization and pool selector 2-to-4 change. Ownership-record hashes agree
with actual files; that agreement is integrity checking, not independent
ownership authority. This fixture is synthetic and not bootable Fedora.

Update compares the entire GPT unchanged and preserves all 18 prior paths and
11,383 allocated clusters, including the earlier attempt. Exactly seven new
paths and 11,379 new clusters are admitted; 777,481,240 other bytes compare
equal. The 6,025,936-byte kernel equals the source ISO; independent archive
parsing verifies the same 1,040 unchanged records and the permitted pool-4
configuration. The separately reviewed 4Kn recovery also preserves both GPT
copies and every byte outside its ESP/pool extents. Independent FAT checks
report clean, matching mirrors after both successful runs.

## Incomplete checks and limits

At `d52f3529` / `6b04b54`, explicit 4Kn recovery verifies programs and reaches
archive staging, then a five-second VirtIO request timeout stops Apply. The
writer stays stopped and dirty, retaining its instance and 383,129 charged
workspace bytes; no retry occurs. Read-only fsck reports the dirty bit and a
free-count hint off by one cluster. The failed image is preserved; the cause
is unestablished. It is not a performance sample or attributed regression.

Userland `9f5b02b` corrects a separate classification refusal: native RO admission
does not establish writable health. Readable-but-dirty classic media may use
only separately reviewed recovery, still subject to the captured foreign-menu
binding veto. The first recheck is interrupted by host Codex SIGTERM during
program verification, before ESP recovery. A detached fresh-copy run then
completes, as recorded above. The original timeout is not reproduced there;
its cause remains unestablished. Neither the timeout nor the host interruption
is counted as a successful run or a timing sample.

The coexistence installer child reports exit 0 on the Install console, while
the outer bootstrap reports selector exit 1 on serial. Source review traces
this to the selector closing its borrowed libc stderr handle before libc
finalization, not to a conflicting Apply result. Userland `6946cc3` delegates
a separate owned WRITE copy. Profile Cancel, coexistence Update dry-run and
confirmed Update now return 0 through the outer bootstrap. Dry-run leaves the
whole disk byte-identical to its before snapshot.

The initial native-FILE staging observation at `2f471d54` predates alpha's
append-proof and bounded phase-write changes. Growing-chain validation and
single-sector preparation were source-inspected leads; its functional QMP
counts are not current-source costs or matched measurements. The current fs
stack consumes those shared changes instead of duplicating them. No lease,
durability or authority rule is weakened.

The [Limine/OVMF comparison](limine-nvme-read.md) separates the initial bounded
no-progress observation, a failed experimental Fedora derivative and successful
controlled NVMe boots. The copied owner firmware is byte-identical to the active
pair; a different OVMF build is not established as the cause. No firmware or
Limine change is made. Separate read-only native firmware checks remain pending
on vela and the ThinkPad; QEMU establishes no native write qualification.

## Checked ESP capacity and shared write bounds

Current source build at parent `269ce682`, userland `8bfc5b0`, fs `03c2c6b`
passes `make -j8 image`; the ordinary host filesystem tools build too. The
filesystem dependency is stacked on alpha's `f58c0f9` append-proof and bounded
phase-write work, preserving actual 512/4096-byte sector strides. Independent
source review found no concrete defect in the batch limits or proof failure
invalidation. Current-source mutation regression remains pending.

MOUNT_OBSERVE now exposes checked cluster/free counts with explicit validity,
not FSInfo guesses or a reservation. Review re-observes after its final content
reads, fingerprints the result and budgets a fresh immutable attempt before
Apply. Unknown capacity, FAT16 or insufficient free clusters refuse without
rebuilding or removing old attempts. The 100 MiB synthetic FAT32 control keeps
the original EFI files plus a clearly labelled 50 MiB filler and has 28,300,800
bytes free; this is a capacity refusal input, not Fedora's original layout.

The current built source refuses both selected 24 GiB NVMe inputs in QEMU:
the original 100 MiB FAT16 ESP reports the unsupported FAT12/FAT16 profile;
the FAT32 no-space control reports 46,735,360 required bytes against 28,300,800
checked free bytes. Both stop during Review, before Apply. QMP records zero
target writes and flushes; GDB observes zero FAT instances/charged workspace
after failure. The FAT32 whole-image post-comparison is pending a quiet shared host. The
FAT16 scratch copy subsequently boots Fedora and changes under Linux, so its
pre-boot no-write evidence is QMP rather than a retained post-Review hash.

With the owner's byte-identical OVMF pair and simple NVMe device configuration,
the original Fedora fixture reaches login and powers down cleanly, and Limine
loads the synthetic attempt's kernel/archive from NVMe into Caelum/rescue. The
failed experimental derivative is superseded by the working owner fixture below;
see the separate [firmware comparison](limine-nvme-read.md#owner-firmware-and-device-comparison).

## Bootable Fedora coexistence — current-source functional checks

The owner supplies a boot-tested derivative of the signed Fedora 44 Cloud Base
Generic 44-1.7 image, SHA-256
`b463c5359b79640907971459f91912b3e32c89dde4b968483a37ff1cd622afde`.
This is a 24 GiB cloud-image fixture, not vela's 1 GiB ESP or a capture of either
laptop. Its old 100 MiB FAT16 ESP is wiped/deleted, leaving a gap before the
unchanged root; a new **600 MiB FAT32 ESP at GPT entry 4** holds the unchanged
Fedora EFI tree and original FAT volume serial. Supplied originals stay pristine;
all mutation and Fedora boots use separate copies.

Both installer runs use parent source `269ce682`, userland `8bfc5b0`, fs
`03c2c6b` stacked on `f58c0f9`; the subsequent parent rebase/records do not change
these source bytes. QEMU uses four host-CPU vCPUs, 1 GiB, q35/nested KVM,
cache-none NVMe `nvme,drive=target,serial=fixture`, SCSI installer CD, fresh owner
OVMF variables and no NIC. Coexistence Install explicitly selects disk 1,
a 2 GiB pool and 16 MiB journal; Update explicitly selects existing pool entry 2.
The accepted 30-second verification interval completes without extension. Both
installer and selector exit 0; GDB observes zero retained FAT instances/workspace.
These runs include debugger pauses and shared-host work, so are functional checks,
**not matched launch or installation timings**.

| Independent post-operation check | Install | Update |
| --- | --- | --- |
| GPT | Only slot 2 and required header/array CRC fields change; pool starts at LBA 11716608, 4194304 sectors | Entire GPT and reviewed pool identity unchanged |
| Existing ESP objects | 23 paths / 5,504 allocated clusters preserved | 31 paths / 16,920 clusters, including the old attempt, preserved |
| Added ESP objects | 8 paths / 11,416 clusters | 6 paths / 11,414 clusters under the same revision, new attempt |
| Other bytes compared exactly | 23,575,468,512 | 23,575,476,928 |
| Frozen post-operation image SHA-256 | `9aaed65b898c45d03b3db06f63ab263601937f899cfebd7c309f1f790c24351e` | `77770dfddf0c40465540e837b3508e5fff4a5ec8497b5c0ed20fa85c9e50ebd2` |

Preservation is independently checked before booting Linux. The allowed changes
are the exact new pool extent, new directory entries/clusters and their FAT
entries, and eight FSInfo hint bytes per copy. No whole-ESP, FAT-sector or GPT-sector
mask hides unrelated changes. Old file bytes, allocated chains, raw directory
slots, ESP trailing sectors, BIOS/root partitions and unused gaps compare exactly.
Install checks the fresh pool GUID is nonzero/unique; its random value was not
captured separately before Apply, so this does not claim an independent exact
Review-nonce comparison. Update checks the frozen existing GUID exactly.

Independent `sgdisk --verify`, `fsck.fat -n`, mtools and raw FAT parsing agree.
Staged kernels equal the ISO's 6,165,208-byte kernel; the filtered 40,563,044-byte
archive equals the expected source transformation, including all 1,041 retained
records byte-for-byte. The existing configuration already selects pool entry 2,
so needs no reserialization. Saved normal/rescue manual entries name the new
attempt; source parsing checks their paths and binding independently of the
installed ownership hashes. No Fedora/GRUB/fallback file or menu is changed.
Update honestly reports the prior active revision unknown because task 3 never
activates its manual entry, and skips old-revision cleanup.

Fedora boot checks use the **actual resulting disks**, fresh owner VARS and the
same firmware CODE as the successful control, 2 GiB, no NIC/CD and exactly
`nvme,drive=d0,serial=fixture`:

| Resulting disk | Accelerator / CPU | Fedora result |
| --- | --- | --- |
| Install | Nested KVM / four host-CPU vCPUs | `multi-user.target`, serial login, clean QMP powerdown |
| Update | TCG / four `max` vCPUs | `multi-user.target`, serial login, clean QMP powerdown |

The post-Install Linux shutdown is frozen anew before Update; its root changes
are baseline state, not attributed to Pyxis. Frozen pre-Linux images preserve
both operations' independent comparisons. No boot timing comparison is made.

The first post-Update nested-KVM boot instead reports OVMF `No bootable option`.
A diagnostic repeat captures NVMe command timeouts/reset before that refusal;
read-only GDB validates the FAT volume signature/backpointers, `Valid=1`,
`DiskError=1` and a 4,096-byte root at cluster 2. Its apparent empty firmware
listing is not proof of an empty directory on disk. A fresh writable copy of the
**pre-Update** baseline also times out and reaches OVMF's pool-free assertion;
MDTS 6 does not rescue the post-Update boot. These failures have only 131,072
backend read bytes / 49 operations and zero completed writes/flushes. The actual
post-Update disk then boots through NVMe under TCG without any media repair,
firmware change or Limine modification. The failure is therefore not established
as an Update regression; the nested-KVM/host-CPU versus TCG/`max` configuration is
isolated, not its underlying mechanism. A 40-bit KVM control boots the disk after
that Linux run, but a fresh frozen post-Update copy still times out at 40 bits;
it therefore establishes no address-width/MMIO workaround. Native checks remain
separate.

Matched after-costs and the final restacked 4Kn mutation regression remain gates.
Raw images, independent scripts, screenshots and QMP/GDB/serial captures stay
local under the task's private qualification directory. No physical-media write,
firmware edit or foreign boot-manager change is performed.
