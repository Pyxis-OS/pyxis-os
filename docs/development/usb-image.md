# Raw USB boot image

The opt-in `usb-image` target assembles a GPT disk image at
`build/pyxis-usb.img`. Firmware loads the pinned Limine EFI loader, Caelum and
the matching boot archive from FAT32. Programs then use the archive in RAM.
Caelum USB reads and qualified writes use the separate
[native storage backend](../devices/usb-storage.md).

## Build and layout

Initialize the pinned dependencies and use the existing Pyxis compiler. Host
assembly additionally requires GPT fdisk (`sgdisk`), dosfstools (`mkfs.fat`),
mtools (`mmd`, `mcopy`) and GNU coreutils. No root, loop devices or mounted host
partitions are needed. The compiler container does not need rebuilding.

```sh
git submodule update --init fs userspace ports third_party/lwip
make -j16 usb-image
```

The default menu timeout is zero for immediate normal boot. Add
`BOOT_MENU_TIMEOUT=5` when building install media to make the separate
`Install Pyxis` entry selectable; see [configuration](configuration.md#boot-menu-timeout).

The sample pool uses the [npfs format](../../fs/docs/npfs-format.md), with
fresh pool, volume, disk and partition IDs on each build. It has no on-disk
principal. Knowing an ID grants no authority. The formatter chooses its journal
from the image builder's `USB_POOL_JOURNAL` configuration; the default is 8 MiB
for this small development pool. Choose at least 128 MiB for a 256 GB pool.

| Setting | Default | Meaning |
| --- | --- | --- |
| `USB_IMAGE_MIB` | `1024` | Total raw disk size in MiB |
| `USB_ESP_MIB` | `256` | FAT32 EFI System Partition size in MiB |
| `USB_POOL_JOURNAL` | `8MiB` | Journal size passed to mkfs.npfs |
| `USB_BOOT_IMAGE` | `build/pyxis-usb.img` | Existing raw file used by the USB launcher |

Image sizes are positive decimal MiB counts without leading zeros. They are
build configuration, not filesystem limits. The chosen sizes must leave space
for the pool, satisfy the formatter's capacity/reserve requirements and fit the
kernel/archive pair in a valid FAT32 ESP. For example, a different configuration
is selected directly:

```sh
make -j16 usb-image USB_IMAGE_MIB=512 USB_ESP_MIB=128 USB_POOL_JOURNAL=8MiB
```

The disk uses 512-byte logical sectors, a protective MBR, 128 GPT entries and
primary/backup GPT copies. Both partitions begin on MiB boundaries. The first
MiB and final MiB reserve GPT/alignment space; the pool occupies the remaining
space after the ESP. Thus its size is `USB_IMAGE_MIB - USB_ESP_MIB - 2` MiB.

| GPT entry | Type | Contents |
| --- | --- | --- |
| 1, `Pyxis EFI` | Standard EFI System Partition | `EFI/BOOT/BOOTX64.EFI`, `boot/caelum.elf`, `boot/initrd.cpio`, `boot/limine/limine.conf` |
| 2, `Pyxis pool` | Pyxis pool, `1a8194a3-8a07-4dff-830e-4cb4ed7aac00` | One `usb-test` npfs volume containing `README.txt`, `SAFE_TO_WIPE` and `bin/cat.pxe` captured from the matching archive |

The pool type is an image metadata convention introduced by this builder.
[GPT discovery](../devices/gpt.md) preserves type GUIDs without inferring
authority; [native mounts](../devices/native-readonly-filesystem.md) still select
an explicit disk identity, partition entry and volume. The sample seeds storage
validation; it is not mounted as a persistent home volume by default.
`SAFE_TO_WIPE` is a regular empty file in the volume root, marking every volume this builder creates
as disposable for the installer. Removing the marker makes the volume final;
see the [installer consent rules](../wip/native-filesystem.md#target-consent).

`usb-image` reuses ordinary kernel, SDK, ports, archive and Limine configuration
assembly, including its interface checks and `INIT` selections. Explicit
[prebuilt bundles](build-bundles.md) are supported; host filesystem tools still
build from the pinned `fs` repository. The ordinary `image` target remains the
ISO path and does not require the additional FAT/GPT assembly tools.

Every `usb-image` invocation constructs fresh temporary pool, ESP and disk files,
then replaces the prior regular output after successful assembly. Failure keeps
the previous image. Symlink and device output paths are rejected. The image is
sparse where possible; it is not inherently RAM-backed. Rebuilding replaces
the pool and its identities. There is no data-preserving update, rollback or
atomic physical-media update protocol.

## Boot through emulated USB

Build first, then launch the existing image:

```sh
make run-usb CPUS=4
make debug-usb CPUS=4
make run-usb USB_BOOT_IMAGE=/path/to/selected.raw ACCEL=tcg
```

The launcher uses Q35, `qemu-xhci` and a directly attached `usb-storage` device
with `bootindex=1`, backed by the raw image read-only. It attaches no boot ISO.
`VIRTIO_BLK_IMAGE` must be empty on this path, keeping native USB validation from
being hidden by another storage backend. See QEMU's
[USB emulation](https://www.qemu.org/docs/master/system/devices/usb.html) and
[boot ordering](https://www.qemu.org/docs/master/system/bootindex.html) references.

Launching does not rebuild the image and does not require its formatting
input again. Existing CPU, memory, accelerator, display, networking, host
filesystem and firmware overrides apply. The launcher copies fresh OVMF
variables for every run; use a matching raw code/variables pair with USB boot
support. `debug-usb` pauses with GDB on `127.0.0.1:1234`, as described in the
[debugger guide](gdb.md). Ordinary `make run` and `make debug` retain ISO boot.

After firmware boot, exercise `app://` programs and RAM-backed `home://` in the
shell. The checked-in `CONFIG_XHCI=n` leaves kernel USB access disabled. With
xHCI enabled, supported boot-present BOT disks provide native block reads and
GPT discovery. Qualified disks also support explicitly authorized writes and
ordered cache flushes. The default init does not mount this sample volume;
configure its actual `MOUNT_DISK` GUID and an explicit trusted read-only mount
as described in [init configuration](../userland/init.md#native-disk-configuration-and-mounting).
Image assembly chooses a fresh GUID, so an ISO validation image can configure
the existing USB image without replacing it. Firmware image loading alone does
not demonstrate native USB I/O. The supplied `run-usb`/`debug-usb` launchers
keep their raw attachment read-only. Writable QEMU validation uses an explicitly
selected private image copy and writable attachment with `--read-write` init;
see the [manual storage record](usb-storage-bringup.md) and
[persistent USB development walkthrough](edit-build-run.md#persistent-usb-development).
Keep that private disk copy across QEMU runs and build the separately booted
ISO with its actual GUID. Rebuilding the ISO can change the kernel, archive or
trusted init without replacing the persistent disk. Do not rerun `usb-image`
against the private copy: successful assembly replaces its pool and saved files.
The firmware-boot launchers remain read-only; the walkthrough uses a separate
manual writable attachment. Physical writable use remains deferred.

## Prepare a selected physical target later

Phase A builds and boots regular emulated image files. Physical writing requires
a separately assigned installation step and explicit selection of the expendable
whole USB drive. Keep the internal NVMe outside that selection. The following
is a manual preparation procedure for that later step, not an automatic target
finder or installer.

Select a stable whole-drive `/dev/disk/by-id/` path, then inspect its resolved
device, model, serial, capacity, logical-sector size and current mount points:

```sh
usb_target=/dev/disk/by-id/usb-REPLACE_WITH_THE_SELECTED_WHOLE_DRIVE
readlink -f "$usb_target"
lsblk -o NAME,PATH,MODEL,SERIAL,SIZE,LOG-SEC,MOUNTPOINTS "$usb_target"
sudo blockdev --getss "$usb_target"
sudo blockdev --getsize64 "$usb_target"
stat -c %s build/pyxis-usb.img
```

The selected drive must use 512-byte logical sectors and be at least as large
as the image. Unmount each currently mounted partition on that exact drive and
relinquish other host access before writing. Check the selected whole-drive path
again before copying; the copy replaces its partition table and contents.

```sh
sudo dd if=build/pyxis-usb.img of="$usb_target" bs=4M conv=fsync status=progress
sudo sgdisk --move-second-header "$usb_target"
sudo sgdisk --verify "$usb_target"
```

Relocating the backup GPT is required when the physical target is larger than
the image: [Caelum's GPT scanner](../devices/gpt.md#supported-layout-and-checks)
requires that copy at the disk's actual final logical block. This changes GPT
geometry without expanding the ESP or pool. The remaining capacity stays unused;
pool resizing is separate work. Rewriting an installation replaces its pool,
and these commands provide no rollback on interruption.

Select USB UEFI boot in firmware, with Secure Boot disabled for the unsigned
vendored loader. Limine reads the same EFI files and the shell runs from RAM.
Physical controller, laptop port topology, USB keyboard support, persistent
reads/writes and power-loss durability require the later assigned hardware and
USB stages. A successful QEMU boot does not establish those properties.

## Validation

The source kernel, SDK, ports, userspace, host filesystem tools, ISO and USB image
builds passed with `make -j16`. The existing local CMake 4.4.3 was added to PATH
for port builds; no compiler/container rebuild or dependency pin change was
needed. GPT fdisk 1.0.10, dosfstools 4.2 and mtools 4.0.49 assembled the image.

Manual validation used both the default 1024/256 MiB image/ESP and a 512/128 MiB
configuration. Both had healthy primary/backup GPT copies and complete,
consistent retained pool states according to `pyxisfs-inspect check`. EFI
kernel/archive/configuration contents matched the assembly inputs; the default
image's EFI loader and extracted pool executable also matched their inputs.
An insufficient image/ESP configuration was refused without replacing the
existing image. Extending a disposable image copy from 512 to 768 MiB and
relocating its backup GPT produced a healthy map with unchanged pool identity
and extent; this was a regular file, not physical media.

USB boots used Q35, installed QEMU 10.2.2, nested KVM, 256 MiB guest RAM,
entropy enabled, fresh raw OVMF variables, no display window and no virtio-blk
disk or boot ISO. Firmware was edk2-ovmf 20260508-8.fc44, using
`/usr/share/edk2/ovmf/OVMF_CODE.fd` and its matching `OVMF_VARS.fd`.

| Image/ESP configuration | CPUs | Observed result |
| --- | --- | --- |
| 1024/256 MiB | 4 | USB firmware loading, GDB stop at `kernel_init`, correct command line/archive extent, interactive `ls`, `date` and `cat app://share/hello.txt` |
| 512/128 MiB | 4 | USB boot to the interactive shell; `date` completed |
| Same 512/128 MiB image | 1 | One pre-kernel file-open failure; later unchanged-image boot and `date` succeeded |

QEMU's monitor reported the read-only USB MSD at 5000 Mb/s and no inserted CD
image on the USB path. Caelum reported no block backend, as expected. Whole-image
hashes were unchanged after the boots. The isolated
[Limine file-open failure](qemu.md#usb-firmware-file-open-failure-before-kernel-entry)
remains unexplained; these observations do not establish repeatable firmware
reliability.

Ordinary ISO launch also reached the shell and ran `date` with four CPUs, using
the existing QEMU 10.2.2 build carrying the
[AHCI fix](qemu.md#ahci-cd-rom-crash-before-kernel-entry). Supplying
`USB_BOOT_IMAGE` did not change that explicit ISO mode; an empty image on
`run-usb` was refused. No new tests or CI were added. Native USB I/O, passthrough
and physical writes were not exercised. QEMU and GDB sessions were closed.
