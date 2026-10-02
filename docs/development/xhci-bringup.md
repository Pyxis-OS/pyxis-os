# xHCI bring-up record

Phase B.2 was developed on October 2, 2026, from main `f0b97ed` after merged
contract PR #319. PCI resources are commit `a582f9e`; controller code is
`76afe7c`. Dependency pins are unchanged. Behavior and interfaces are in the
[xHCI reference](../devices/usb-xhci.md).

## Configuration and manual checks

Ordinary `make -j16 image` and the configurable USB image build completed with
the existing compiler. Changed kernel code produced no compiler warnings;
existing vendor warnings remain. No new tests, self-tests, fault-injection
framework, CI workflow or boot/output automation was added.

```sh
make -j16 usb-image USB_IMAGE_MIB=512 USB_ESP_MIB=128 \
  USB_POOL_OWNER=00112233445566778899aabbccddeeff
```

The image/RAM sizes, owner ID and observed capacities below are validation
parameters, separate from driver contracts. Runs used nested KVM, Q35, `-cpu max`,
256 MiB RAM and fresh variables from raw OVMF code/variables files supplied by
edk2-ovmf `20260508-8.fc44`. The paths were
`/usr/share/edk2/ovmf/OVMF_CODE.fd` and `OVMF_VARS.fd`.

Controller inspection used QEMU 10.2.2 with the existing local upstream AHCI
regression fix described in [QEMU notes](qemu.md). It booted the ISO and attached
the raw image only behind xHCI, without VirtIO block. A directly attached USB
mouse exercised independent controller port state. The manual command was:

```sh
qemu-system-x86_64 -machine q35 -accel kvm -cpu max \
  -smp cpus=4,sockets=1,cores=4,threads=1 -m 256M -rtc base=utc \
  -drive if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -display none -serial mon:stdio -no-reboot -no-shutdown \
  -S -gdb tcp:127.0.0.1:1234 -cdrom build/pyxis.iso -boot d -nic none \
  -object rng-random,id=pyxis_rng,filename=/dev/urandom \
  -device virtio-rng-pci,rng=pyxis_rng,disable-legacy=on \
  -device qemu-xhci,id=pyxis_xhci \
  -drive if=none,id=pyxis_usb,format=raw,readonly=on,file=build/pyxis-usb.img \
  -device usb-storage,id=usb_disk,bus=pyxis_xhci.0,port=1,drive=pyxis_usb \
  -device usb-mouse,id=usb_mouse,bus=pyxis_xhci.0,port=2
```

Copy the matching variables template before each launch. The local fixed QEMU
executable was `/tmp/pyxis-qemu-ahci-fix/build/qemu-system-x86_64`.
Repeat with one CPU by changing both CPU/core counts. Both configurations reached
a running controller with two separate Enabled-slot reservations. The observed
controller advertised xHCI 1.0, eight root ports, 64 slots, 32-byte contexts and
zero scratchpads. Protocol capabilities mapped the disk to a USB 3 port and the
mouse to a USB 2 port; those numbers are observations, not matching rules.

Interactive GDB hardware breakpoints at `xhci_interrupt`, `consume_command` and
`wait_activity` showed:

- Static vector 39 delivered through the IDT. MSI-X entry zero contained the BSP
  route `fee00000:00000000`, data `27` and an unmasked entry.
- Enable Slot submissions at physical TRBs `141000` and `141010` produced
  cycle-owned command events with those exact pointers, type 33, success 1 and
  distinct slot IDs. The USB 2 reset also produced a port-change event.
- After worker consumption, two commands were complete, no command was pending,
  event dequeue was three TRBs beyond the ring start, EHB was clear and USBSTS
  reported no error. DCBAA device entries remained zero, as permitted before
  Address Device.
- Manual monitor removal of the mouse produced a third completed command,
  Disable Slot. Its port became retired with slot zero, while the disk's slot
  remained reserved and the controller stayed running.

The archive-backed shell executed `date` successfully. This validates controller
commands and interrupts, not USB descriptors, class matching, block reads or
mouse input. No physical drive, passthrough, ThinkPad or writable media was used.
Nonzero scratchpads, 64-byte device contexts and BIOS-owned handoff remain
unmeasured paths.

## Existing VirtIO regression

The ordinary ISO path, without xHCI, also booted with four CPUs, a private
virtiofsd 1.14.0 export, user networking, entropy and the unchanged baseline raw
image attached read-only through VirtIO block. The existing launch interface was:

```sh
make debug QEMU=/tmp/pyxis-qemu-ahci-fix/build/qemu-system-x86_64 \
  CPUS=4 MEMORY=256M ACCEL=kvm QEMU_DISPLAY=none \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  VIRTIO_FS_SOCKET=/tmp/pyxis-usb-xhci/build/virtio-socket/fs.sock \
  VIRTIO_NET=1 VIRTIO_BLK_IMAGE=build/baseline-usb.img VIRTIO_BLK_READONLY=1
```

The shell read the private export's `host://hello.txt` and received ICMP replies
from QEMU's gateway. GDB found all four transports active, FUSE session ready,
five completed block reads, healthy GPT copies, and three network IRQs/TX/RX
packets at inspection. This checks shared MSI-X mechanics with existing device
sources and queue-vector indices. It does not expand the USB storage scope.
All QEMU, GDB and private daemon processes were closed afterward.

## Review follow-up

The follow-up to reviewed revision `5df2bd5` makes interrupt enable/disable write
zero to IMAN.IP, preserving that W1C pending bit. The IRQ acknowledgement path is
unchanged. The [qualification debt](../technical-debt.md#xhci-hardware-profile-and-runtime-retention)
records missing USB 2 debounce/startup settling, idle polling cost, the asserted
OS-owned request after handoff timeout and the bootstrap-base consistency check.
Timing, firmware rollback and BAR mapping policies are unchanged.

`make -j16 image` passed with the existing compiler. One manual four-CPU ISO boot
used the QEMU/controller/device configuration above with fresh OVMF variables.
GDB stopped at the normal disable and enable calls: the generated writes masked
both IP/IE and set only IE when enabling; IMAN read back `0`, then `2`. This startup
began with IP clear. Preservation of an already-pending IP follows source/register
semantics (xHCI 1.2b §5.5.2.1), rather than an observed pending-IP transition.

The ordinary IRQ path delivered three interrupts. Both Enable Slot commands
completed with matching physical TRB identities and separate disk/mouse slots.
The worker consumed three events, left no command pending, advanced ERDP with EHB
clear and observed USBSTS zero; the controller remained running without failure.
QEMU and GDB were closed afterward. This recheck covers the controller startup
change; physical firmware, debounce/settling and power cost remain unqualified.

## Startup cost and firmware limit

Before controller implementation, an unchanged baseline image from `f0b97ed`
was booted through installed QEMU 10.2.2 USB with four CPUs and the same RAM,
firmware and 512/128 MiB image configuration. Its SHA-256 was
`60c15aca8c8c5826319d16f4f8c631a4e1e916074f3825b76dc6a7c4b19e21f0`.

```sh
make debug-usb CPUS=4 MEMORY=256M ACCEL=kvm QEMU_DISPLAY=none \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  USB_BOOT_IMAGE=build/baseline-usb.img
```

GDB used a temporary hardware breakpoint at `user_launch_initial`, then read
`period_fs` and the HPET main counter at `0xfffffe80402020f0`:

```text
p/d *(unsigned long long *)0xfffffe80402020f0 * period_fs / 1000000
```

Successful debugger-observed kernel-clock intervals were 262.36693 and
182.75115 ms, a range of 79.61578 ms. Two further unchanged-image attempts failed
in Limine before Caelum entry. Two matched post-implementation USB attempts also
failed at that stage, so no comparable post-change timing or performance
conclusion is available. These intervals exclude firmware loading and work
before clock initialization; `user_launch_initial` also precedes the worker's
actual controller activation. They are not end-to-end boot or storage throughput
measurements. The [pre-kernel file-open failure](qemu.md#usb-firmware-file-open-failure-before-kernel-entry)
remains unqualified, independent of native driver operation.

In the four-CPU ISO inspection profile, prepared controller DMA consumed four
pages (DCBAA, command ring, event ring and ERST; no scratchpads). PMM reported
4,168 allocated frames, VM 8,758 reserved / 4,000 backed pages and 35 range
records; heap had 48 live allocations. An earlier same-profile boot that rejected
the controller and unwound its resources reported 4,160 allocated frames,
8,744 reserved / 3,996 backed pages, 26 range records and 46 live allocations.
That is an observed 32 KiB PMM difference including worker startup storage,
not a matched-revision benchmark or a fixed controller memory requirement.
Storage transfer performance belongs to a later stage with an actual read path.
