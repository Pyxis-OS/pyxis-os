# NVMe coexistence task 3 — installer baseline

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

## Authority checkpoint

The first implementation checkpoint uses kernel `4586455d` (plus reference-only
`d2bb8355`) and userland helpers `ba1e6ed`, selector `9d4f0c8` and read-only
candidate selection `5f29575`. Ordinary source `make -j8 image` passes with the
existing builder. This is incomplete task-3 work, not installer qualification.

Interactive QEMU uses the same four-CPU/1-GiB configuration with a disposable
2-GiB NVMe device and private `nvme.write=1`. Synthetic GPTs contain an ESP,
MSR and Windows-data entry plus an unused gap; these are authority fixtures,
not copied Fedora ESP trees or exact native-target layouts. A private interactive
probe exercises the real ABI; it is absent from the shipped archive/source.

- Appending one unused entry succeeds with both 512-byte and 4096-byte logical
  sectors. The 4096-byte case also refuses occupied-slot and overlapping-range
  proposals before effects. Both GPT copies and CRCs verify offline. Whole-image comparisons find differences only
  in the selected-entry array sector and header sector in each copy; original
  entries and every other disk byte match the input.
- Returned pool handles reject outside reads/writes and neighbour volume
  operations. After release, whole-disk/neighbour reclaim refuses while own-entry
  reclaim succeeds. Consuming a copied append review cannot repeat the operation.
- Erase review becomes stale after a separately authorized append and refuses
  admission. A fresh review with `wipe` succeeds without changing disk bytes;
  incorrect consent, reuse through an alias and reclaim after release refuse.
- Actual trusted bootstrap, with the probe replacing only the installer payload:
  coexist permits APPEND and denies ERASE; classic permits ERASE and denies APPEND.
  Both ordinary whole-disk and undelegated raw-partition openings refuse.
- Read-only candidate selection reports all three partitions, the one ESP,
  unused slot 4 and the expected MiB-aligned largest gap/default. Whole-image
  comparison confirms no writes. The existing raw FAT bootstrap opens only the
  selected partition, validates GPT through retained read-only metadata, creates
  and rereads a 9-byte file and passes whole-volume check; offline mtools and
  `fsck.fat -n` agree, with all outside-partition bytes unchanged.
- GDB hardware breakpoints observe the reviewed candidate and the worker-owned
  whole-device extent immediately before allocation-free narrowing/publication.
  Debugger samples are correctness evidence only, not timings.

Full installer Review, the shared immutable ESP stager, executor integration,
matched after-runs and faithful copied-ESP/native-layout qualification remain.
No native disk/ESP write, firmware-variable edit or foreign boot-manager change
has been performed. Uncertain-write quarantine has source review, without injected-failure runtime
qualification. Private QEMU/GDB jobs are stopped after each check.

## Reviewed ESP recovery integration checkpoint

The accepted recovery grant now has kernel/ABI and userland-helper code, with
classic-only issuer delegation. Kernel and SDK source builds and the installer
link pass with builder `df87791`. Complete target Review, selected-pool inspection,
literal archive rebinding, attempt-aware plans and native classic activation
are integration modules; the frontend/executor do not yet consume them. These
build checks do not qualify the new recovery operation at runtime.

Integration first completes the accepted [classic sector-compatibility prerequisite](../../wip/nvme-coexistence.md#classic-sector-compatibility--accepted-by-the-owner-2026-10-15):
classic supports 512/4096-byte logical sectors, while native FAT admission
requires 512-byte device/FAT sectors. No source or runtime change narrows that
contract at this checkpoint. Disposable end-to-end recovery/coexistence runs,
matched after-runs and native target writes have not been performed.

## Native executor integration — 2026-10-15, in progress

The owner accepted classic fixed boot paths through native FAT files until
installer task 5 adds committed boot-binding evidence; coexistence uses immutable
attempts and a saved/printed manual entry now. The common frontend/plan/executor
is wired. The [512/4096-byte prerequisite](fat32-4kn.md) supersedes the sector
restriction in the earlier integration checkpoint above.

Ordinary source `make -j8 image` passes at parent `5b210c20`, userland `ad4572b`,
fs `35582347`, ports `318c5c39`, lwIP unchanged, with builder `df87791`.
Subsequent rebases onto merged #882 and shared stager #272 preserve the complete
kernel/ABI and userland source trees. Qualification uses a private installer
selection in that image, not a modified installer or compiler container.

The first complete classic 512-byte Install uses parent `3f180be8` / userland
`91f2fb6` and the baseline four-CPU/1-GiB VirtIO configuration. Review/Cancel leaves
a blank 2-GiB image entirely zero-filled. A separately confirmed Install copies
2,228 programs / 48,094,844 bytes, stages and fully verifies the fixed boot files,
then reports `installed`. GDB observes zero FAT instances and charged workspace
after retirement. Offline `sgdisk --verify`, `fsck.fat -n` and mtools agree;
the installed kernel equals its ISO source. The menu contains only normal and
rescue entries with fixed paths, with no attempt reference. Target-only UEFI
boot reaches the installed shell with the matching `bin` revision; shell
`poweroff` exits QEMU cleanly. Boot verification uses explicit VirtIO boot order
and `-nic none` to avoid firmware PXE fallback. This initial run predates the
final deadline/capacity/profile-grant corrections; corrected-image Update and
4Kn checks remain separate gates.

These are functional checks, with debugger pauses and concurrent host work,
not matched performance samples. Native FILE staging is substantially slower
than raw bulk ESP replacement; repeated full-chain validation and durable phase
flushes per bounded write are source-inspected cost candidates, not measured
attribution. Matched costs, complete Update/recovery/coexistence preservation
and a bootable Fedora guest remain required before readiness. No physical-media
writes or Fedora boot result are claimed.

The corrected image at `5b210c20` / `ad4572b` also completes classic 4Kn Install,
including the original non-extending interval. Independent fsck/mtools and the
4Kn-aware host inspector pass, the installed kernel matches its source, and
GDB sees zero instances/workspace after retirement. Target-only 4Kn UEFI boot
reaches its matching installed `bin` revision and shell poweroff exits cleanly.
A separate damaged-boot-sector copy reaches explicit recovery Review; no
recovery Apply result is claimed yet. Synthetic three-partition NVMe media
reaches coexistence Review with pool entry 4 and both foreign EFI directories
shown; cancelling leaves the entire image byte-identical. This is neither a
bootable Fedora fixture nor a copied native ESP.

The shared stager's subsequent canonical-name fix is integrated in userland
#270: parent and file opens in classic and coexistence refuse alias-only paths,
with explicit per-view enumeration grants. Focused installer compilation/link
passes; its refreshed-image runtime qualification is pending.
