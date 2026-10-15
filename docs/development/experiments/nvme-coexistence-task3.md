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
