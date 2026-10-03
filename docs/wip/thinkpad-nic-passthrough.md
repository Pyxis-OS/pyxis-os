# ThinkPad NIC passthrough to QEMU

Status: **owner direction, 2026-10-03.** Tasks 1–3 below are accepted and
assigned in order. Task 1 is a code change; tasks 2 and 3 are run by the owner on
the ThinkPad. The RTL8111 driver is a [follow-up milestone](#follow-up-milestone-rtl8111-driver),
not part of this work. Context: [ThinkPad next steps](thinkpad-next-steps.md#2-ethernet-passthrough-to-qemu-then-a-driver).

- [x] Task 1: launcher option and preflight checks.
- [ ] Task 2: owner host setup.
- [ ] Task 3: owner first passthrough boot.

## Goal

Run Pyxis in QEMU/KVM on the ThinkPad's own Fedora with the **built-in RJ45
controller passed through**, so a native RTL8111 driver can be developed and
debugged in a VM against the real hardware. The passed function is `05:00.0`
(RTL8111, `10ec:8168`, revision 0x15). It is alone in IOMMU group 15, so only that
function moves to the VM. The dock's Ethernet (`02:00.0`) and Wi-Fi (`03:00.0`)
stay with Fedora.

## Task 1: launcher option `VFIO_PCI` (code, agent)

Add one explicit option to `scripts/run-qemu.sh`, passed through by the Makefile
like the other device options:

```sh
make run VFIO_PCI=0000:05:00.0 MEMORY=2G
```

**Accepted behaviour:**

- **Off by default.** With `VFIO_PCI` empty, the command line is unchanged.
- **Strict address validation.** Accept only a full PCI address `DDDD:BB:DD.F` in
  lowercase hexadecimal (function 0–7). Reject anything else, including commas,
  because QEMU uses commas as option separators. This follows the existing
  `VIRTIO_FS_SOCKET` check.
- **Preflight checks, each failing with a clear message before QEMU starts:**
  1. `/sys/bus/pci/devices/$VFIO_PCI` exists;
  2. its bound driver is `vfio-pci` (`/sys/bus/pci/devices/$VFIO_PCI/driver`);
  3. its IOMMU group resolves (`…/iommu_group`), and `/dev/vfio/<group>` is
     readable and writable by the invoking user;
  4. the memory-lock limit (`ulimit -l`) is `unlimited` or at least the guest's
     `MEMORY`, because VFIO pins all guest RAM. The message should suggest
     `MEMORY=2G` or the limit setup in task 2.
- **QEMU argument:** append `-device vfio-pci,host=$VFIO_PCI`. Nothing else
  changes; the VirtIO devices stay as configured.
- **Optional ROM sub-option, *proposed*:** if QEMU or OVMF has trouble with the
  card's PXE option ROM, a `VFIO_ROMBAR=0` option appends `,rombar=0`. Pyxis
  doesn't use the ROM inside the VM. Add it only if task 3 shows it is needed.
- **Docs:** document the option beside the other launcher options
  (`docs/development/qemu.md` and the README's option list), linking to this plan
  for host setup.

No other launcher, kernel or image change is part of this task.

### Task 1 validation (2026-10-03)

Implemented on `bringup/vfio-launcher` from main `a433a19`; usage and the finite
memory-limit parser's supported formats are in
[QEMU launcher documentation](../development/qemu.md#pci-passthrough).
`sh -n` and `git diff --check` passed. Manual argument comparisons against the
main launcher confirmed an unchanged command line with passthrough off and one
VFIO device argument with it on, including debug and USB launch modes. Those
argument checks used `QEMU=/bin/echo`, not a hardware boot.

Malformed addresses, a missing function and an unbound function were rejected.
For `MEMORY=2G`, a finite 2097152 KiB limit passed and 2097151 KiB failed;
an 8 KiB limit rejected `MEMORY=8193B`, which needs 16 KiB after alignment.
The real NIC's driver, group and access checks passed after the owner's reboot;
this session reported unlimited memlock and a user-owned `/dev/vfio/15`.

The ordinary image build passed with GCC 16.2 and the pinned submodules. An
interactive boot without passthrough reached userspace using installed QEMU
10.2.2, KVM on the ThinkPad host (not nested virtualization), four CPUs, 2 GiB RAM,
no display window and the matching raw edk2 OVMF pair. GDB confirmed four online
CPUs after initialization. QEMU and GDB were stopped afterward. Task 3's hardware
boot remains unrun by the agent.

## Task 2: host setup on the ThinkPad's Fedora (owner)

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

To limit exposure, a numeric value of at least the guest size (in KiB) can
replace `unlimited`/`infinity`. QEMU is run directly, not through libvirt, so no
SELinux or libvirt configuration is involved.

## Task 3: first boot with the card passed through (owner)

```sh
make run VFIO_PCI=0000:05:00.0 MEMORY=2G CPUS=4
```

**Expected:**

- Pyxis boots as usual.
- `lspci` inside Pyxis lists `10ec:8168` (RTL8111, rev 0x15) beside QEMU's
  devices, and the boot log's PCI function count is one higher than without
  passthrough.
- No driver claims it yet; that's the follow-up milestone.

**If it fails:** record the launcher's preflight message or QEMU's error. If
QEMU reports a ROM problem, that is the trigger for the `VFIO_ROMBAR=0`
sub-option above.

**Addressing for later.** The card keeps its real MAC, so the router's existing
reservation for the built-in port applies inside the VM. A static Pyxis
configuration with that address and the router as gateway puts the VM on the LAN
exactly where the native ThinkPad will be. The address is kept out of this doc.

## Follow-up milestone: RTL8111 driver

Not assigned here. It is planned as its own milestone once task 3 succeeds:

- **Scope:** a Caelum driver for the RTL8111 at `05:00.0` (revision 0x15),
  exposed as `net0` beside the existing VirtIO NIC path in
  [networking](../devices/networking.md). Reuse the [PCI](../devices/pci.md)
  claim, BAR mapping and MSI-X handling from the xHCI work.
- **Configuration:** static IPv4 with the router-reserved address. DHCP stays
  separate, with broadcast reception, as recorded in
  [ThinkPad next steps](thinkpad-next-steps.md#3-later-idea-reverse-remote-terminal-with-broadcast-discovery).
- **Completion goal:** the remote terminal reaches Pyxis over the passed-through
  card in QEMU, then over the same port natively on the ThinkPad.
- **Later:** the dock's `02:00.0` is the same chip family at a different revision,
  inside a multi-function management chip, so it is a separate step.
