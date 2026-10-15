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
The latest qualified source is parent `2f471d54` / userland `6946cc3`, based
on main `390b3139` with merged shared stager #880, audio #881 and idle blanking
#886. It pins fs `35582347` (including merged #39), ports `318c5c39` and
unchanged lwIP. All four exact-head CI checks pass. Earlier source revisions
are identified per flow below; the epoch read proof/invalidation is preserved.

Private ISO menus select the ordinary trusted installer; only scratch NVMe
qualification adds `nvme.write=1`. The latest private menu also disables idle
blanking. No compiler rebuild or modified installer probe is used. All runs
below are functional, with
debugger pauses/concurrent host work, **not matched performance samples**.

| Flow | Revision | Observed result |
| --- | --- | --- |
| Classic 512 Install | `3f180be8` / `91f2fb6` | Review/Cancel leaves blank 2-GiB image zero; confirmed Apply copies/verifies 2,228 files / 48,094,844 bytes and fixed boot payloads; installed UEFI shell and clean poweroff. This predates final deadline/capacity/profile-grant corrections. |
| Classic 4Kn Install | `5b210c20` / `ad4572b` | Corrected source copies 2,228 files / 48,129,564 bytes; full verification and unchanged interval pass; installed UEFI shell and clean poweroff. |
| Healthy 512 Update | `5b210c20` / `ad4572b` | Native preservation Update completes; unrelated `EFI/FOREIGN/keep.txt` unchanged; new kernel equals source. |
| Synthetic NVMe coexistence Install | `d52f3529` / `6b04b54` | Both payload verification passes complete; saved manual entry printed; existing boot menu unchanged. Staged GUID entry boots the matching installed `bin` revision via VirtIO and powers off cleanly. NVMe-attached boot stalls in Limine before Caelum. |
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

Copied-ESP/bootable-Fedora qualification and matched after-costs remain gates.
The owner supplied a signed-source Fedora 44 Cloud Base Generic 44-1.7 image,
grown from 5 to 24 GiB with the backup GPT moved, and independently booted it
as NVMe under q35/KVM/OVMF to the serial login prompt. Read-only inspection
finds a 100 MiB **FAT16** ESP: 512-byte sectors, four sectors per cluster,
512 root entries and 51,078 data clusters. Its existing EFI files total
22,491,504 bytes, leaving about 82 MiB free. The accepted native path supports
FAT32 only, and two retained approximately 47 MB attempts exceed that free
space. No supplied image has been modified or admitted for writing.

**Fixture accepted by the owner, 2026-10-15:** prepare a separate derivative
with a **600 MiB FAT32 ESP** and the same Fedora EFI file contents, preserving
the provided originals. Boot Fedora on that derivative as NVMe before freezing
its baseline; then qualify coexistence Install and Update plus both Fedora
reboots. Label it as a modified cloud-image fixture, not the original 100 MiB
ESP or vela's 1 GiB ESP. The original FAT16 layout is also a read-only Review
refusal case, with a clear unsupported-profile reason and no writes. Insufficient
ESP capacity must likewise refuse before Apply. This changes no product format
support or rebuild authority. These checks are pending.

The [NVMe-attached Limine observation](limine-nvme-read.md) is not reproduced
in subsequent controlled boots, including the original payload and UEFI
variables, MDTS 6/7, a minimal archive and a 4Kn namespace. A subsequent Fedora pre-install boot exposes an OVMF NVMe-driver assertion
with a matching relocated PC after FLUSH timeout/reset; its initiating cause
remains unestablished. No native or Fedora coexistence boot is claimed. Native FILE
staging is substantially slower than the earlier raw bulk path in functional
runs. Source inspection finds growing-chain validation and prepared single-sector
writes per bounded file write. QMP counters over roughly 35 MiB of staging show
827,763,200 read bytes / 534,196 reads and 71,312,896 write bytes / 139,283 writes
(512 bytes per backend write). These are functional-run deltas, not matched
costs or NVMe command histograms. QMP backend flush accounting cannot establish
whether NVMe FLUSH commands ran. Alpha owns the accepted append-proof and
phase-batching work; this task does not duplicate it.
No durability/authority rule has been weakened. No physical-media writes,
firmware edits or foreign boot-manager changes have been performed. Raw
images/captures remain local under ignored `build/`; task-owned jobs are
stopped at checkpoints.

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
600 MiB derivative still reaches the firmware assertion and remains unqualified;
see the separate [firmware comparison](limine-nvme-read.md#owner-firmware-and-device-comparison).
