# Vela LPSS / HID descriptor: task 2

## Source and build, 2026-10-15

Pre-code baseline `32c163d3`: fresh main `2f940a7c` plus actual #891 commits
through `85cb434c`, including the native task 1 record. No submodule changes:
userland `20a98189550e`, ports `318c5c39221e`, fs `22eaf447a98b`, lwIP
`a1aadb91a503`. Baseline kernel SHA-256
`547892afa08b8bbf2a1c78bc942b9ef60699ff6c691938c05970032e78f4d072`;
archive `d2e85a33dd1c79602874631cf7745582f667625c57d14474171ada064ea43c50`.

Ordinary `make -j16 image PREBUILT='sdk userspace ports'` passes in existing
`pyxis-llvm23.1.3-df87791`. Unchanged dependencies are verified CI2699 bundles.
No compiler rebuild, upstream implementation import, probe or new test tooling.
The scope/ownership and source references are in the
[milestone](../../../wip/vela-i2c-touchpad.md#task-2-attachment-and-limits).
Raw logs, images, JSONL and GDB: task worktree `build/task2-*`.

## QEMU controls

Nested KVM Q35, CPU max, 2 GiB, OVMF, standard VGA, VirtIO-net/rng and xHCI
hub/keyboard, stock live spaces. Four fresh 4-CPU boots in A1/B1/A2/B2 order;
A is the pre-code image, B1 `a6ce55de`, B2 `1f6a0aaa` (subsequent source-index
refusal and attempted-timing diagnostics do not run without the device).
No local build or GDB overlaps timed commands; shared host load/CI is uncontrolled.
All existing IPC passes verify; every session exits 0 with complete Remote drain.

Use `build/tools/pyxis-remote --machine --no-shell-echo 127.0.0.1 24489`, one
fresh connection per IPC command:

```text
session bin://ipcbench.pxe call --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe call --size 4096 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 64 --messages 256 --rounds 5
session bin://ipcbench.pxe send --size 4096 --messages 256 --rounds 5
lua -e 'local a={"bin://echo.pxe","-n"}; local r=pyxis.run; local n=pyxis.monotonic_ns; assert(r(a)==0); for i=1,10 do local t=n(); assert(r(a)==0); print(i,n()-t) end'
```

Milliseconds, median (range); five IPC samples and ten launch samples each.

| Workload | A1 | B1 | A2 | B2 |
| --- | --- | --- | --- | --- |
| CALL64 completion | 73.826 (70.361–74.437) | 188.501 (118.556–240.851) | 78.362 (73.858–106.334) | 103.085 (95.057–104.817) |
| CALL4096 completion | 75.427 (73.003–78.054) | 187.417 (130.851–224.976) | 90.025 (73.892–95.455) | 91.277 (85.903–98.333) |
| SEND64 completion | 14.521 (11.749–15.014) | 21.828 (17.612–33.829) | 11.985 (11.689–15.102) | 15.294 (11.669–20.258) |
| SEND64 admission | 1.196 (1.189–1.452) | 1.780 (1.352–2.623) | 1.215 (1.184–1.272) | 1.196 (1.195–1.975) |
| SEND4096 completion | 11.946 (11.771–14.467) | 39.891 (14.232–50.136) | 15.546 (14.764–21.302) | 13.456 (12.842–16.799) |
| SEND4096 admission | 1.263 (1.241–1.300) | 3.287 (1.450–4.578) | 1.364 (1.255–1.794) | 1.247 (1.241–1.597) |
| Echo through exit | 6.837 (2.338–7.747) | 10.473 (5.000–12.991) | 7.901 (2.703–8.836) | 7.709 (3.578–9.349) |

B1 was broadly slower, with clock reads up to 81,966 ns versus roughly 35,000
in A1/B2. CALL64 medians remain higher in B2; do not discard these samples or
claim a measured improvement/no regression. Source inspection and GDB show
LPSS absent, no claim/mapping/worker and no ongoing new path; IPC instructions
are unchanged, though link layout differs. These nested results establish no
native cost or cause. Whole-boot wall time and native idle/transfer cost remain
unmeasured; task 3's idle baseline is still due before polling.

## Functional limits

Four-CPU read-only GDB confirms ACPI ready, HID snapshot ABSENT, LPSS ABSENT,
claim NULL and master base zero. The no-device path stays quiet and all spaces
start. The separate 1-CPU GDB check stops in preparation from `kernel_init`,
IF=0 before `acpi_prepare`/AP startup, and confirms no claim after absence.
After scheduling, ACPI is ready and the same zero native state remains. Remote
echo/exit and five verified CALL64 passes complete; Enter reaches the shared
keyboard path from `usb_hid_collect`. All own QEMU/GDB jobs are stopped.
No QEMU DesignWare/ELAN hardware is present: positive
MMIO, power/reset, NACK/arbitration/deadline, descriptor and failed-cleanup paths
are source-inspected, not claimed exercised by QEMU. Source review corrected
cleanup error propagation, all-ones register handling, counter minima and the
SDA RX-hold workaround before native staging.

## Native owner gate

Pending. Use the packet README and `/shared/present/RUNSHEETS.md`: `stick-deploy
--initrd`, normal installed-menu entry, no attended blanking option, and deployed
`boot://log.pxe -l debug > home://hid-i2c-task2.txt` with no shell quotes. Sync,
then collect the persistent file through `stick-deploy --mount` and restore.
A validated v1.00 descriptor/identity is the check; a named refusal is a failure
to retain, not permission to weaken the PCI or firmware contract. No physical
finger input is needed, and task 3 is not authorized by this record.
