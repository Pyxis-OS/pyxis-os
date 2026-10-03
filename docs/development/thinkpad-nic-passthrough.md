# ThinkPad NIC passthrough to QEMU

The built-in RJ45 controller can be passed from the ThinkPad's Fedora host to
Pyxis in QEMU/KVM. Launcher support, owner host setup and a first hardware boot
were completed on 2026-10-03. This provides a development target for the
[planned RTL8111 driver](../wip/thinkpad-rtl8111.md);
Pyxis currently inventories the function without claiming it.

## Hardware and launcher

The host function is `0000:05:00.0`, Realtek `10ec:8168`, revision `0x15`, alone
in IOMMU group 15 on the qualified host. Dock Ethernet (`02:00.0`) and Wi-Fi
(`03:00.0`) remain with Fedora. Re-check addresses and groups after firmware or
host kernel changes; these are machine observations rather than fixed interfaces.

```sh
make run VFIO_PCI=0000:05:00.0 MEMORY=2G CPUS=4
```

Select matching OVMF paths for Fedora: `OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd`
and `OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd` on the qualified host. Set
`CROSS_COMPILE` to the installed Pyxis toolchain prefix if it is outside `PATH`.

`VFIO_PCI` is empty by default. Its address validation, read-only host preflight
checks, memory-limit handling and availability across launch modes are described
in [QEMU launcher documentation](qemu.md#pci-passthrough). It adds one VFIO device
and leaves VirtIO devices configured as before. Host binding and permissions are
configured separately. A ROM override was not needed in the observed boots.

## Host setup

Run these once. Replace `chronium` if the username differs. The examples assume
group 15; re-check after BIOS or kernel updates, because IOMMU group numbers can
change.

**Confirm the IOMMU group holds only the RJ45 function:**

```sh
ls /sys/kernel/iommu_groups/15/devices        # expect only 0000:05:00.0
```

**Bind it to `vfio-pci`:**

```sh
sudo dnf install driverctl
sudo driverctl set-override 0000:05:00.0 vfio-pci
lspci -k -s 05:00.0                            # "Kernel driver in use: vfio-pci"
```

The override persists across reboots. While it is in place, Fedora has no wired
connection on that port; use Wi-Fi or the dock's Ethernet. To undo it:
`sudo driverctl unset-override 0000:05:00.0`.

**Give your user access to the VFIO group device.** `/dev/vfio/vfio` is already
world-accessible; the group node `/dev/vfio/15` is root-only by default. A udev
rule changes its owner:

```sh
echo 'SUBSYSTEM=="vfio", KERNEL=="15", OWNER="chronium", MODE="0600"' \
  | sudo tee /etc/udev/rules.d/90-pyxis-vfio.rules
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=vfio
ls -l /dev/vfio/15                             # owner should be chronium
```

**Raise the memory-lock limit for your user.** VFIO pins all guest RAM, so the
limit must cover `MEMORY`. Both of the following are needed: login shells and SSH
read `limits.d`, while terminals started from a graphical KDE or GNOME session
inherit the limit from the systemd user manager.

```sh
printf '%s\n' 'chronium soft memlock unlimited' 'chronium hard memlock unlimited' \
  | sudo tee /etc/security/limits.d/90-pyxis-vfio.conf
sudo mkdir -p /etc/systemd/system/user@.service.d
printf '%s\n' '[Service]' 'LimitMEMLOCK=infinity' \
  | sudo tee /etc/systemd/system/user@.service.d/90-pyxis-vfio.conf
sudo systemctl daemon-reload
```

Log out and back in, or reboot, then check:

```sh
ulimit -l                                      # expect "unlimited"
```

Unlimited memlock is the supported and qualified configuration here. A finite
value must cover guest RAM plus any additional memory QEMU/VFIO locks; the
launcher's RAM-minimum preflight does not estimate that overhead. QEMU is run
directly, not through libvirt, so no SELinux or libvirt configuration is involved.

## Validation (2026-10-03)

Launcher commit `4e6c64d` is based on main `a433a19`. `sh -n` and
`git diff --check` passed. Manual argument comparisons against the main launcher
confirmed an unchanged command line with passthrough off and one VFIO device
with it on, including debug and USB launch modes. Those argument checks used
`QEMU=/bin/echo`; they did not boot a USB image. Malformed addresses, a missing
function and an unbound function were rejected. For `MEMORY=2G`, a finite
2097152 KiB limit passed and 2097151 KiB failed in argument-only checks; these do
not qualify hardware pinning at that boundary. After review, the finite-limit
parser was simplified to shell integer K/M/G sizes. An 8 KiB limit accepts
`MEMORY=8K` and rejects `MEMORY=9K`, which needs 16 KiB after alignment.

The ordinary image build passed with GCC 16.2 and the pinned submodules. Both
agent boots used installed QEMU 10.2.2, KVM on the ThinkPad host (not nested
virtualization), four CPUs, 2 GiB RAM, no display window and the matching raw
edk2 OVMF pair. The launcher was started interactively in debug mode, with GDB
stopping at `vm_get_stats` after device initialization before resuming userspace:

```sh
QEMU_DISPLAY=none MEMORY=2G CPUS=4 ACCEL=kvm \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  VFIO_PCI=0000:05:00.0 scripts/run-qemu.sh debug
```

The matched baseline omitted `VFIO_PCI` and reported six PCI functions. The
passthrough boot reported seven functions and a complete inventory, then reached
userspace. GDB read the retained NIC record at guest `00:03.0`: vendor `0x10ec`,
device `0x8168`, revision `0x15`, and null owner. Four CPUs were online. QEMU and
GDB were stopped afterward. This session's memlock limit was unlimited and
`/dev/vfio/15` was owned by the invoking user; the group contained only the RJ45
function.

The owner's independent GTK boot reached the shell, where `lspci` showed seven
functions including the same NIC identity at `00:03.0`. The guest PCI address is
assigned by QEMU and need not match the host address. These boots qualify
passthrough and read-only PCI discovery; they do not establish Ethernet I/O.

The card retains its physical MAC. The owner-confirmed built-in port profile and
driver outline are recorded in the [RTL8111 plan](../wip/thinkpad-rtl8111.md).
Later dock work is also distinguished from built-in qualification there.
