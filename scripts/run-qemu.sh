#!/bin/sh
set -eu
: "${QEMU:=qemu-system-x86_64}"
: "${QEMU_DISPLAY:=gtk}"
: "${QEMU_VIDEO:=std}"
: "${MEMORY:=8G}"
: "${CPUS:=1}"
: "${THREADS:=1}"
: "${ACCEL:=tcg}"
: "${OVMF_CODE:=/usr/share/OVMF/OVMF_CODE.fd}"
: "${OVMF_VARS:=/usr/share/OVMF/OVMF_VARS.fd}"
: "${VIRTIO_FS_SOCKET:=}"
: "${VIRTIO_NET:=0}"
: "${VIRTIO_RNG:=1}"
: "${QEMU_NO_REBOOT:=0}"
: "${VIRTIO_BLK_IMAGE:=}"
: "${VIRTIO_BLK_READONLY:=0}"
: "${VFIO_PCI:=}"
: "${USB_BOOT_IMAGE:=}"
: "${UDP_FORWARD:=}"
: "${TCP_FORWARD:=}"
for count in "$CPUS" "$THREADS"; do
  case "$count" in
    ''|0*|*[!0-9]*) echo 'CPUS and THREADS must be positive decimal integers.' >&2; exit 1 ;;
  esac
done
[ "$((CPUS % THREADS))" = 0 ] || {
  echo 'THREADS must divide CPUS.' >&2
  exit 1
}
cores=$((CPUS / THREADS))
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
mkdir -p build
cp "$OVMF_VARS" build/OVMF_VARS.fd
mode=${1:-run}
case "$mode" in
  debug|debug-usb) set -- -S -gdb tcp:127.0.0.1:1234 ;;
  run|run-usb) set -- ;;
  *) echo 'Launch mode must be run, debug, run-usb or debug-usb.' >&2; exit 1 ;;
esac
case "$QEMU_VIDEO" in
  std) ;;
  virtio) set -- "$@" -vga none -device virtio-gpu-pci,disable-legacy=on ;;
  bochs) set -- "$@" -vga none -device bochs-display ;;
  *) echo 'QEMU_VIDEO must be std, virtio or bochs.' >&2; exit 1 ;;
esac
if [ "$mode" = run-usb ] || [ "$mode" = debug-usb ]; then
  [ -f "$USB_BOOT_IMAGE" ] || {
    echo 'USB_BOOT_IMAGE must name an existing regular raw image file; build it with make usb-image.' >&2
    exit 1
  }
  [ -z "$VIRTIO_BLK_IMAGE" ] || {
    echo 'USB boot requires VIRTIO_BLK_IMAGE to be empty so a second disk cannot mask missing USB access.' >&2
    exit 1
  }
  case "$USB_BOOT_IMAGE" in
    /*) usb_image=$USB_BOOT_IMAGE ;;
    *) usb_image="$(pwd -P)/$USB_BOOT_IMAGE" ;;
  esac
  case "$usb_image" in
    *,*) echo 'USB_BOOT_IMAGE absolute path must not contain commas.' >&2; exit 1 ;;
  esac
  set -- "$@" \
    -drive "if=none,id=pyxis_usb,format=raw,readonly=on,file=$usb_image" \
    -device qemu-xhci,id=pyxis_xhci \
    -device usb-storage,bus=pyxis_xhci.0,port=1,drive=pyxis_usb,bootindex=1
else
  set -- "$@" -cdrom build/pyxis.iso -boot d
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
# QEMU cannot tell a reset register write from a triple fault.
case "$QEMU_NO_REBOOT" in
  0) ;;
  1) set -- "$@" -no-reboot ;;
  *) echo 'QEMU_NO_REBOOT must be 0 or 1.' >&2; exit 1 ;;
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
if [ -n "$VFIO_PCI" ]; then
  case "$VFIO_PCI" in
    [0-9a-f][0-9a-f][0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]:[0-9a-f][0-9a-f].[0-7]) ;;
    *) echo 'VFIO_PCI must be a full lowercase PCI address DDDD:BB:DD.F (function 0..7).' >&2; exit 1 ;;
  esac
  vfio_device="/sys/bus/pci/devices/$VFIO_PCI"
  [ -d "$vfio_device" ] || {
    echo "VFIO_PCI device $VFIO_PCI does not exist on this host." >&2
    exit 1
  }
  vfio_driver=$(readlink -f "$vfio_device/driver") || vfio_driver=
  [ "$vfio_driver" = /sys/bus/pci/drivers/vfio-pci ] || {
    echo "VFIO_PCI device $VFIO_PCI must be bound to vfio-pci; see docs/development/thinkpad-nic-passthrough.md host setup." >&2
    exit 1
  }
  vfio_group_path=$(readlink -f "$vfio_device/iommu_group") || vfio_group_path=
  [ -d "$vfio_group_path" ] || {
    echo "VFIO_PCI device $VFIO_PCI has no resolved IOMMU group; check the host IOMMU setup." >&2
    exit 1
  }
  vfio_group=${vfio_group_path##*/}
  [ -r "/dev/vfio/$vfio_group" ] && [ -w "/dev/vfio/$vfio_group" ] || {
    echo "VFIO_PCI requires read/write access to /dev/vfio/$vfio_group; see docs/development/thinkpad-nic-passthrough.md host setup." >&2
    exit 1
  }
  vfio_memlock=$(ulimit -l)
  if [ "$vfio_memlock" != unlimited ]; then
    case "$MEMORY" in
      *[Kk]) vfio_size=${MEMORY%?}; vfio_unit_kib=1 ;;
      *[Mm]) vfio_size=${MEMORY%?}; vfio_unit_kib=1024 ;;
      *[Gg]) vfio_size=${MEMORY%?}; vfio_unit_kib=1048576 ;;
      *) vfio_size=$MEMORY; vfio_unit_kib=1024 ;;
    esac
    case "$vfio_size" in
      ''|*[!0-9]*)
        echo 'Finite VFIO memlock checking requires integer MEMORY in MiB or with a K/M/G suffix.' >&2
        exit 1 ;;
    esac
    # QEMU rounds RAM up to 8 KiB. Divide the limit to avoid size overflow.
    vfio_max_size=$((vfio_memlock / 8 * 8 / vfio_unit_kib))
    [ "$vfio_size" -gt 0 ] 2>/dev/null && [ "$vfio_size" -le "$vfio_max_size" ] 2>/dev/null || {
      echo "VFIO_PCI needs positive MEMORY=$MEMORY covered by memlock; current limit is $vfio_memlock KiB. Try MEMORY=2G if it fits, or raise the limit as in docs/development/thinkpad-nic-passthrough.md host setup." >&2
      exit 1
    }
  fi
  set -- "$@" -device "vfio-pci,host=$VFIO_PCI"
fi
exec "$QEMU" -machine "$machine" -accel "$ACCEL" -cpu max \
  -rtc base=utc \
  -smp "cpus=$CPUS,sockets=1,cores=$cores,threads=$THREADS" -m "$MEMORY" \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE" \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -display "$QEMU_DISPLAY" -serial mon:stdio \
  "$@"
