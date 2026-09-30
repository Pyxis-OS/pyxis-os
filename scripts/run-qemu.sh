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
: "${VIRTIO_RNG:=1}"
: "${VIRTIO_BLK_IMAGE:=}"
: "${VIRTIO_BLK_READONLY:=0}"
: "${UDP_FORWARD:=}"
: "${TCP_FORWARD:=}"
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
net_backend=user,id=pyxis_net
if [ -n "$UDP_FORWARD" ]; then
  [ "$VIRTIO_NET" = 1 ] || {
    echo 'UDP_FORWARD requires VIRTIO_NET=1.' >&2
    exit 1
  }
  case "$UDP_FORWARD" in
    *:*) ;;
    *) echo 'UDP_FORWARD must be HOST_PORT:GUEST_PORT.' >&2; exit 1 ;;
  esac
  host_port=${UDP_FORWARD%%:*}
  guest_port=${UDP_FORWARD#*:}
  for port in "$host_port" "$guest_port"; do
    case "$port" in
      ''|0*|*[!0-9]*) echo 'UDP_FORWARD ports must be decimal 1..65535 without leading zeros.' >&2; exit 1 ;;
    esac
    [ "${#port}" -le 5 ] && [ "$port" -le 65535 ] || {
      echo 'UDP_FORWARD ports must be in 1..65535.' >&2
      exit 1
    }
  done
  # Match the stock network configuration. Expose only a host loopback port.
  net_backend="$net_backend,hostfwd=udp:127.0.0.1:$host_port-10.0.2.15:$guest_port"
fi
if [ -n "$TCP_FORWARD" ]; then
  [ "$VIRTIO_NET" = 1 ] || {
    echo 'TCP_FORWARD requires VIRTIO_NET=1.' >&2
    exit 1
  }
  case "$TCP_FORWARD" in
    *:*) ;;
    *) echo 'TCP_FORWARD must be HOST_PORT:GUEST_PORT.' >&2; exit 1 ;;
  esac
  host_port=${TCP_FORWARD%%:*}
  guest_port=${TCP_FORWARD#*:}
  for port in "$host_port" "$guest_port"; do
    case "$port" in
      ''|0*|*[!0-9]*) echo 'TCP_FORWARD ports must be decimal 1..65535 without leading zeros.' >&2; exit 1 ;;
    esac
    [ "${#port}" -le 5 ] && [ "$port" -le 65535 ] || {
      echo 'TCP_FORWARD ports must be in 1..65535.' >&2
      exit 1
    }
  done
  # Match the stock network configuration. Expose only a host loopback port.
  net_backend="$net_backend,hostfwd=tcp:127.0.0.1:$host_port-10.0.2.15:$guest_port"
fi
case "$VIRTIO_NET" in
  0) set -- "$@" -nic none ;;
  1) set -- "$@" -netdev "$net_backend" \
       -device virtio-net-pci,netdev=pyxis_net,disable-legacy=on ;;
  *) echo 'VIRTIO_NET must be 0 or 1.' >&2; exit 1 ;;
esac
case "$VIRTIO_RNG" in
  0) ;;
  1) set -- "$@" -object rng-random,id=pyxis_rng,filename=/dev/urandom \
       -device virtio-rng-pci,rng=pyxis_rng,disable-legacy=on ;;
  *) echo 'VIRTIO_RNG must be 0 or 1.' >&2; exit 1 ;;
esac
case "$VIRTIO_BLK_READONLY" in
  0) blk_readonly=off ;;
  1) blk_readonly=on ;;
  *) echo 'VIRTIO_BLK_READONLY must be 0 or 1.' >&2; exit 1 ;;
esac
if [ -n "$VIRTIO_BLK_IMAGE" ]; then
  [ -f "$VIRTIO_BLK_IMAGE" ] || {
    echo 'VIRTIO_BLK_IMAGE must name an existing regular raw image file.' >&2
    exit 1
  }
  # An absolute filename keeps QEMU from interpreting a relative path prefix
  # as a block protocol. Commas would introduce additional drive options.
  case "$VIRTIO_BLK_IMAGE" in
    /*) blk_image=$VIRTIO_BLK_IMAGE ;;
    *) blk_image="$(pwd -P)/$VIRTIO_BLK_IMAGE" ;;
  esac
  case "$blk_image" in
    *,*) echo 'VIRTIO_BLK_IMAGE absolute path must not contain commas.' >&2; exit 1 ;;
  esac
  set -- "$@" \
    -drive "if=none,id=pyxis_blk,format=raw,cache=writeback,readonly=$blk_readonly,file=$blk_image" \
    -device virtio-blk-pci,drive=pyxis_blk,disable-legacy=on,num-queues=1
fi
machine=q35
if [ -n "$VIRTIO_FS_SOCKET" ]; then
  [ -S "$VIRTIO_FS_SOCKET" ] || {
    echo "Missing virtio-fs socket $VIRTIO_FS_SOCKET: start virtiofsd first; see docs/devices/virtio-fs.md." >&2
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
