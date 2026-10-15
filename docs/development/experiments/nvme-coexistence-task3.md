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
