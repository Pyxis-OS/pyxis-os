# Installer planner/executor — QEMU, 2026-10-11

Baseline captured before code changes. Implementation/qualification is in progress.

Baseline source: parent `b8b0b7d82337` (accepted documentation on main
`a59a4ef3`), userland `d2437266d459`, ports `df3210b04142`, fs `b427df29f865`.
Full SDK/image/fs-tools build used the existing LLVM49 container; no compiler
container rebuild. The revision in the baseline image is `b8b0b7d82337`.

Linux `6.19.10-300.fc44.x86_64`, QEMU 10.2.2, q35 nested KVM, CPU `max`,
four vCPUs, 2 GiB, HPET, UTC RTC, fresh OVMF variables. VirtIO block: 2 GiB
raw target, 512-byte sectors, writeback, one queue; VirtIO entropy; no NIC or
HDA. No task-owned build ran during timing. Shared-host load is uncontrolled.

Install used a fresh blank target, Read the room and the default 12 MiB journal.
Update restored the previous task's pristine installation at `94d0320d4134`;
it staged a different revision, avoiding same-revision clearing costs.
Hardware breakpoints with function calls disabled recorded source preparation,
inventory completion, the first exclusive claim boundary and restart offer.
Host monotonic timestamps and `/proc/<qemu-pid>/stat` record whole-QEMU CPU,
including device emulation; this is not guest BSP or native stick cost.

| Flow | Samples | Inspection wall s | Apply wall s | Whole-QEMU apply CPU s |
| --- | --- | --- | --- | --- |
| Baseline Install | 1 | 0.696 | 153.871 | 127.71 |
| Baseline Update | 1 | 0.595 | 141.955 | 115.44 |

Inspection ends at target selection; manual Review/journal/consent time is
excluded from apply. Both runs copied and compared 2,211 regular files /
42,053,098 bytes, then passed EFI 5/5 raw path/payload verification. Program
verification remains ordinary filesystem-view reads and may use cached pages.
No physical-media or power-loss result is claimed. Raw images/logs remain local.

Build: `make -j16 image fs-tools BOOT_MENU_TIMEOUT=60` in the existing builder.
Run with `make run CPUS=4 MEMORY=2G ACCEL=kvm BOOT_MENU_TIMEOUT=60`, using the
matching OVMF pair under `/usr/share/edk2/ovmf/`, a restored VirtIO target and
no network. Select Install Pyxis, then Install / Read the room or Update.
