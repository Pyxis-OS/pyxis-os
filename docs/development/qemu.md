# QEMU launcher and boot troubleshooting

## Display device

`QEMU_VIDEO=std` is the default and retains q35's standard VGA. Select the
display device independently of the window backend:

```sh
make run CPUS=4 QEMU_VIDEO=virtio
make debug CPUS=4 QEMU_VIDEO=virtio QEMU_DISPLAY=none
make run CPUS=4 QEMU_VIDEO=std DISPLAY_SIZE=1024x768
make run CPUS=4 QEMU_VIDEO=bochs DISPLAY_SIZE=800x600
```

The launcher adds `-vga none -device virtio-gpu-pci,disable-legacy=on`; no VGA
can mask a missing GPU driver. The same setting applies to USB run/debug.
Unknown selections fail before launch. With no Limine framebuffer, early output
stays serial and the driver queries the initial GPU pixel size before AP startup.
With a framebuffer it starts at that framebuffer's dimensions. The kernel presents with
one current 2D resource, full-frame transfers and flushes. GTK window changes
resize every local TTY with whole-cell raster cropping and update size queries.
Existing graphics mappings keep their layout and are clipped to the destination;
idle applications do not wake or adapt yet. Failed preparation keeps the previous
geometry. VirtIO uses a hardware system cursor; boot and Bochs compose it in
software. 3D and multiple monitors remain later work.
VirtIO panic output is serial-only.
`QEMU_VIDEO=bochs` adds `-vga none -device bochs-display`. Both it and standard
VGA support the same Bochs register driver. `DISPLAY_SIZE` is baked into the
image as `display.size=WIDTHxHEIGHT`; it is separate from host device selection.
An empty setting quietly keeps the firmware framebuffer (details at trace
level). Exact 32-bit modes require
width divisible by eight, hardware dimensions of 64..16000 by 64..12000, enough
room for the current bar/font, and an extent fitting BAR0 and reported VRAM.
With the current font the effective minimum is 80×64. Unsupported or malformed
sizes keep firmware output with a message; no closest mode is selected. A failed
mode is restored and verified; unverifiable restoration stops boot on serial.

This driver requires QEMU's modern register extension and an enabled firmware
DISPI mode. It preserves the existing WC mapping only when the boot framebuffer
starts at BAR0; another offset refuses mode setting. Panic uses the selected
direct target after successful readback. The [display milestone](../kernel/display.md)
records interfaces, measured cost and remaining limits.

## Hardware pointer frontend

The owner accepted GTK on X11, relative PS/2 input and unscaled 1:1 committed
guest geometry for VirtIO cursor qualification on 2026-10-08:

```sh
GDK_BACKEND=x11 make run CPUS=4 QEMU_VIDEO=virtio QEMU_DISPLAY=gtk
```

Use a native-sized viewport whose actual GTK content dimensions match the
committed guest display dimensions. GTK fit mode can briefly scale output while
the guest processes a resize; qualify pointer placement only after the new
geometry commits and the content is again 1:1. QEMU installs the hardware shape
and position through its host GUI, independently of the cursor-free scanout.
The guest `screenshot` includes that pointer by composing its leased image into
capture storage; a monitor screendump of scanout alone omits the host cursor.

Source inspection found that GDK's native Wayland position warp is a no-op,
scaled or centered GTK cursor placement differs from its input transform, SDL
uses different cursor-channel packing, and VNC has no position callback. Those
frontends and modes are unqualified; GTK on X11 at 1:1 is the checked path.
The kernel adds no frontend-specific switches. QEMU 10.2.2 also returns the
cursor command's used descriptor without a response or fence: checked completion
proves buffer consumption, without an independent acknowledgment of application.
See [hardware qualification](system-pointer-qualification.md#task-4-hardware-qualification)
and [frontend debt](../technical-debt.md#virtio-cursor-frontend-limits).

## PCI passthrough

`VFIO_PCI` is empty by default. Set it to a full lowercase PCI address
`DDDD:BB:DD.F`, with function 0–7, to attach one host function:

```sh
make run VFIO_PCI=0000:05:00.0 MEMORY=2G CPUS=4
```

The same option is available with `make debug`, `make run-usb` and
`make debug-usb`. The launcher adds `-device vfio-pci,host=0000:05:00.0`;
VirtIO devices remain controlled by their existing options.

Before QEMU starts, the launcher checks that the function exists in sysfs,
is bound to `vfio-pci`, has a resolved IOMMU group, and that the invoking user
can read and write its `/dev/vfio/<group>` node. The `ulimit -l` limit must be
unlimited or cover `MEMORY`, since VFIO pins guest RAM. A finite limit is in
KiB; its comparison requires a positive decimal integer size, in MiB when
unsuffixed or with a case-insensitive `K`, `M` or `G` binary suffix. Other QEMU
memory formats are passed through with an unlimited limit, but cannot be checked
with a finite limit. The comparison accounts for QEMU's
[8 KiB machine-RAM alignment](https://github.com/qemu/qemu/blob/v10.2.2/hw/core/machine.c);
QEMU still validates the memory option itself. Empty `VFIO_PCI` leaves the
QEMU command line unchanged.

The supported and qualified ThinkPad host setup uses unlimited memlock. The
finite-limit preflight checks the guest RAM minimum; QEMU/VFIO can lock additional
memory, so passing it does not guarantee enough headroom. QEMU may still reject
a finite limit at startup.

The launcher does not change host drivers, permissions or limits. Follow the
[ThinkPad host setup](thinkpad-nic-passthrough.md#host-setup) before launching.
The [hardware boot results](thinkpad-nic-passthrough.md#validation-2026-10-03)
confirm PCI discovery; the built-in port is now supported by the
[RTL8111 driver](../devices/rtl8111.md).

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

## USB firmware file-open failure before kernel entry

During [USB image validation](usb-image.md#validation) on October 2, one boot of
the 512 MiB image with a 128 MiB ESP failed in pinned Limine v12.9.0 with
`Failed to open executable with path 'boot():/boot/caelum.elf'`. Caelum had not
entered. This used installed QEMU 10.2.2, nested KVM, one CPU, 256 MiB RAM and
the raw OVMF pair from edk2-ovmf. GPT and FAT checks were healthy, extracted EFI
payloads matched their build inputs and the whole-image hash was unchanged.
The same image subsequently booted to an interactive shell with four CPUs,
then with one CPU, without a bootloader or image change.

The cause remains unqualified. Pinned Limine's generic file-open error also
covers failed firmware block reads while opening FAT paths; it does not establish
that the file is absent. Source inspection found no static size cutoff for these
two valid FAT32 configurations. Successful retries are observations, not a fix.
The failure recurred on an unchanged baseline image and during
[xHCI bring-up](xhci-bringup.md). Keep it distinct from native USB access and
from the AHCI CD-ROM crash above. Revisit with repeatable
firmware/USB I/O diagnosis before claiming reliable physical boot.
