#!/bin/sh
set -eu
: "${QEMU:=qemu-system-x86_64}"
: "${QEMU_DISPLAY:=gtk}"
: "${MEMORY:=256M}"
: "${CPUS:=1}"
: "${ACCEL:=tcg}"
: "${OVMF_CODE:=/usr/share/OVMF/OVMF_CODE.fd}"
: "${OVMF_VARS:=/usr/share/OVMF/OVMF_VARS.fd}"
: "${VIRTIO_FS_SOCKET:=}"
: "${VIRTIO_NET:=0}"
command -v "$QEMU" >/dev/null 2>&1 || {
  echo "Missing $QEMU: install QEMU or set QEMU, then run make run." >&2
  exit 1
}
for firmware in "$OVMF_CODE" "$OVMF_VARS"; do
  [ -r "$firmware" ] || {
    echo "Missing firmware $firmware: set OVMF_CODE and OVMF_VARS to a matching raw pair." >&2
    exit 1
  }
done
# Fresh variables per invocation; never pass the host variables file writable.
cp "$OVMF_VARS" build/OVMF_VARS.fd
set -- "${1:-run}"
if [ "$1" = debug ]; then
  set -- -S -gdb tcp:127.0.0.1:1234
else
  set --
fi
case "$VIRTIO_NET" in
  0) set -- "$@" -nic none ;;
  1) set -- "$@" -netdev user,id=pyxis_net \
       -device virtio-net-pci,netdev=pyxis_net,disable-legacy=on ;;
  *) echo 'VIRTIO_NET must be 0 or 1.' >&2; exit 1 ;;
esac
machine=q35
if [ -n "$VIRTIO_FS_SOCKET" ]; then
  [ -S "$VIRTIO_FS_SOCKET" ] || {
    echo "Missing virtio-fs socket $VIRTIO_FS_SOCKET: start virtiofsd first; see docs/virtio-fs.md." >&2
    exit 1
  }
  # QEMU key/value arguments use commas as separators. Keep this interface
  # literal rather than accepting additional chardev options through a path.
  case "$VIRTIO_FS_SOCKET" in
    *,*) echo 'VIRTIO_FS_SOCKET must not contain commas.' >&2; exit 1 ;;
  esac
  machine=q35,memory-backend=pyxis_mem
  set -- "$@" \
    -object "memory-backend-memfd,id=pyxis_mem,size=$MEMORY,share=on" \
    -chardev "socket,id=pyxis_fs,path=$VIRTIO_FS_SOCKET" \
    -device vhost-user-fs-pci,chardev=pyxis_fs,tag=pyxis-host
fi
exec "$QEMU" -machine "$machine" -accel "$ACCEL" -cpu max \
  -rtc base=utc \
  -smp "cpus=$CPUS,sockets=1,cores=$CPUS,threads=1" -m "$MEMORY" \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE" \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/pyxis.iso -boot d -display "$QEMU_DISPLAY" -serial mon:stdio \
  -no-reboot -no-shutdown "$@"
