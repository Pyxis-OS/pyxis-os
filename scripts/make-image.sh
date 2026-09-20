#!/bin/sh
set -eu
command -v xorriso >/dev/null 2>&1 || {
  echo 'Missing xorriso: install it, then run make image.' >&2
  exit 1
}
mkdir -p build/image/EFI/BOOT build/image/boot/limine
cp build/caelum.elf build/image/boot/caelum.elf
cp build/initrd.cpio build/image/boot/initrd.cpio
rm -f build/image/boot/hello.pxe
cp boot/limine/limine.conf build/image/boot/limine/limine.conf
cp third_party/limine/BOOTX64.EFI build/image/EFI/BOOT/BOOTX64.EFI
cp third_party/limine/limine-uefi-cd.bin build/image/boot/limine/
xorriso -as mkisofs -R -J -V PYXIS_OS \
  -e boot/limine/limine-uefi-cd.bin -no-emul-boot \
  -efi-boot-part --efi-boot-image --protective-msdos-label \
  build/image -o build/pyxis.iso
