#!/bin/sh
set -eu
command -v xorriso >/dev/null 2>&1 || {
  echo 'Missing xorriso: install it, then run make image.' >&2
  exit 1
}
mkdir -p build/image/EFI/BOOT build/image/boot/limine
cp build/caelum.elf.gz build/image/boot/caelum.elf.gz
cp build/initrd.cpio.gz build/image/boot/initrd.cpio.gz
rm -f build/image/boot/hello.pxe build/image/boot/caelum.elf build/image/boot/initrd.cpio
cp build/limine.conf build/image/boot/limine/limine.conf
cp third_party/limine/BOOTX64.EFI build/image/EFI/BOOT/BOOTX64.EFI
cp third_party/limine/limine-uefi-cd.bin build/image/boot/limine/
xorriso -as mkisofs -R -J -V PYXIS_OS \
  -e boot/limine/limine-uefi-cd.bin -no-emul-boot \
  -efi-boot-part --efi-boot-image --protective-msdos-label \
  build/image -o build/pyxis.iso
