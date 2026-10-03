# Temporary installer-authority probe

This is a temporary native validation program for task 4.2. The owner requested
keeping it for review until task 4.3 writes the installer; remove it then.
It is not an installer, consent checker or supported application. The ordinary
image and CI do not build or package it. Run it manually through the real
`Install Pyxis` entry only with the disposable QEMU fixtures below.

The program reads the original kernel/archive FILE grants, rejects attempts to
write them and checks that init forwards no launcher, home, namespace, working
directory or environment. It enumerates five devices and exercises unsupported
and read-only handling, retained-pool exclusion, competing claims, mount
exclusion, attenuation, exact raw read/write/flush, disk independence, GPT
rescan, shared-alias mutation revocation, root lifetime after disk closure,
4 KiB alignment and asynchronous last-close cleanup.

It writes a pattern into the free gap at byte 65536 on writable fixtures,
restores the 512-byte device's original gap, and changes both GPT headers'
disk GUID with correct CRCs. The 4 KiB blank fixture keeps its pattern.
Closing a mounted root deliberately leaves that pool retained until reboot.
Fixture GUIDs identify the two prepared disks independently of enumeration
order; they are inputs to this probe, not production defaults.

## Build and prepare

From the repository root, first build the ordinary image and host tools using
the README commands and the existing compiler. Then:

```sh
make -C docs/development/experiments/native-filesystem-task4.2/probe -j16 \
  SDK="$PWD/build/sdk" BUILD="$PWD/build/installer-probe" \
  CROSS_COMPILE=/path/to/x86_64-unknown-pyxis-
mkdir -p build/installer-probe
build/fs-tools/mkfs.npfs --image build/installer-probe/pool-a.raw \
  --size 128MiB --journal 8MiB --volume bench
build/fs-tools/mkfs.npfs --image build/installer-probe/pool-b.raw \
  --size 128MiB --journal 8MiB --volume bench
truncate -s 132MiB build/installer-probe/mounted-disk.raw
sgdisk --clear --set-alignment=1 \
  --disk-guid=12345678-1234-4567-89ab-0123456789ab \
  --new=1:2049:+128MiB --typecode=1:8300 build/installer-probe/mounted-disk.raw
dd if=build/installer-probe/pool-a.raw of=build/installer-probe/mounted-disk.raw \
  bs=512 seek=2049 conv=notrunc status=none
truncate -s 132MiB build/installer-probe/writable-disk.raw
sgdisk --clear --set-alignment=1 \
  --disk-guid=87654321-4321-4567-89ab-0123456789ab \
  --new=1:2049:+128MiB --typecode=1:8300 build/installer-probe/writable-disk.raw
dd if=build/installer-probe/pool-b.raw of=build/installer-probe/writable-disk.raw \
  bs=512 seek=2049 conv=notrunc status=none
truncate -s 64MiB build/installer-probe/readonly-disk.raw
truncate -s 64MiB build/installer-probe/unsupported-disk.raw
truncate -s 64MiB build/installer-probe/4k-disk.raw
```

Use new image paths, or remove only your own previous fixtures before repeating.
The formatter refuses existing outputs. Separate formatting gives the pools
different identities; cloning a pool would exercise the existing duplicate-pool
guard instead of this probe's intended device-independence check.

After the ordinary build, manually stage the probe. A subsequent ordinary
`make image` recreates userspace staging and removes this extra executable.
If your ordinary build uses INIT, pass the same value to assemble-initrd:

```sh
cp build/installer-probe/installer.pxe build/userspace-root/installer.pxe
scripts/assemble-initrd.sh
scripts/make-image.sh
cp build/pyxis.iso build/installer-probe/probe.iso
```

## Manual QEMU run

Use a fresh writable copy of OVMF VARS. Adjust QEMU/firmware paths to the host;
the validation used QEMU 10.2.2, nested KVM, q35, four CPUs and 2 GiB:

```sh
cp /usr/share/edk2/ovmf/OVMF_VARS.fd build/installer-probe/vars.fd
qemu-system-x86_64 -enable-kvm -machine q35 -cpu max \
  -smp 4,sockets=1,cores=4,threads=1 -m 2G \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  -drive if=pflash,format=raw,file=build/installer-probe/vars.fd \
  -cdrom build/installer-probe/probe.iso \
  -drive if=none,id=a,format=raw,cache=writeback,file=build/installer-probe/mounted-disk.raw \
  -device virtio-blk-pci,drive=a,disable-legacy=on \
  -drive if=none,id=b,format=raw,cache=writeback,file=build/installer-probe/writable-disk.raw \
  -device virtio-blk-pci,drive=b,disable-legacy=on \
  -drive if=none,id=c,format=raw,readonly=on,file=build/installer-probe/readonly-disk.raw \
  -device virtio-blk-pci,drive=c,disable-legacy=on \
  -drive if=none,id=d,format=raw,file=build/installer-probe/unsupported-disk.raw \
  -device virtio-blk-pci,drive=d \
  -drive if=none,id=e,format=raw,cache=writeback,file=build/installer-probe/4k-disk.raw \
  -device virtio-blk-pci,drive=e,disable-legacy=on,logical_block_size=4096,physical_block_size=4096 \
  -device virtio-rng-pci,disable-legacy=on \
  -serial file:build/installer-probe/serial.txt -monitor stdio -no-reboot
```

Select `Install Pyxis` manually during the five-second menu. A headless run can
use QEMU monitor `sendkey down`, then `sendkey ret` while the menu is visible;
`stop`/`cont` and `screendump` help inspect it. Native init launches the probe
as `app://installer.pxe`. Each group prints a diagnostic to its console and
the existing debug-log syscall; completion is `PROBE COMPLETE: all checks passed`
with exit zero. A failure reports the operation and actual/expected status, or
the failed byte/lifetime check. Inspect the output manually; there is no
boot/output automation or fault injection.

Stop QEMU before host inspection. Extract the target partition to a regular
image, then use the ordinary checker and comparator:

```sh
dd if=build/installer-probe/writable-disk.raw of=build/installer-probe/check-pool.raw \
  bs=512 skip=2049 count=262144 status=none
build/fs-tools/fsck.npfs --image build/installer-probe/check-pool.raw
cmp build/installer-probe/pool-b.raw build/installer-probe/check-pool.raw
sgdisk --verify build/installer-probe/writable-disk.raw
```

Restore the ordinary image with the same `make image` settings as before.
No binaries, disk images or new boot options belong in the PR. Healthy runtime
checks do not qualify uncertain-I/O, power-loss or physical-media behavior.
