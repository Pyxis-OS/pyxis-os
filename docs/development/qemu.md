# QEMU boot troubleshooting

## AHCI CD-ROM crash before kernel entry

QEMU 10.2.2 can crash while OVMF/Limine reads the boot ISO through the emulated
SATA CD-ROM. The observed host backtrace starts with `ahci_pio_transfer`, then
`ide_transfer_start_norecurse`, `ide_atapi_cmd_reply_end` and
`ide_buffered_readv_cb`. In the debugger, `AHCIDevice.cur_cmd` is null. No Caelum
serial output appears before this failure.

This matches [QEMU issue 3216](https://gitlab.com/qemu-project/qemu/-/issues/3216).
An unfinished ATAPI read can complete after the firmware stops and restarts the
AHCI command engine, which clears the command pointer. Upstream commit
[`d9f78431d8eb`](https://github.com/qemu/qemu/commit/d9f78431d8ebdc2d03ad74461138c1c9eb076aa5)
cancels those reads before the command list is unmapped. Use a QEMU package or
local build carrying that fix; version strings alone do not identify downstream
backports. The existing launcher accepts a separate executable:

```sh
make run CPUS=4 ACCEL=kvm QEMU=/path/to/fixed/qemu-system-x86_64
```

The September 29 investigation reproduced the same crash with both `cfb97fa`
(before the block driver) and `77d1f16` (before the configurable-queue milestone).
A retained September 27 host core also has the same stack. Successful boots
interleave with failures, including under a host debugger; a single successful
retry does not establish a fix. This is independent of attaching a virtio-blk
development disk, and the kernel has not yet entered when it occurs.

A separate minimal x86_64 QEMU build from `v10.2.2` with only the upstream fix
applied booted the unchanged block-driver PR image to userspace twice using KVM,
four CPUs, 256 MiB RAM, fresh OVMF variables, entropy enabled and no block disk.
The build used `QEMU_DISPLAY=none`; it does not include a GTK display backend.
No kernel workaround, launcher device change or system QEMU replacement was
needed. The installed emulator still needs the upstream fix for ordinary use
without a `QEMU` override.

To inspect a host crash, use `coredumpctl info` or attach host GDB to the QEMU
process and capture `bt` at SIGSEGV. This differs from connecting GDB to QEMU's
guest debug port to inspect Caelum. TCG can help comparison, but switching
accelerators or adding a disk is not a correction for this AHCI lifetime bug.
