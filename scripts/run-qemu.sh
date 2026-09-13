#!/bin/sh
set -eu
: "${QEMU:=qemu-system-x86_64}"
: "${QEMU_DISPLAY:=gtk}"
: "${MEMORY:=256M}"
: "${ACCEL:=tcg}"
: "${OVMF_CODE:=/usr/share/OVMF/OVMF_CODE.fd}"
: "${OVMF_VARS:=/usr/share/OVMF/OVMF_VARS.fd}"
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
exec "$QEMU" -machine q35 -accel "$ACCEL" -cpu max -smp 1 -m "$MEMORY" \
  -drive "if=pflash,format=raw,unit=0,readonly=on,file=$OVMF_CODE" \
  -drive if=pflash,format=raw,unit=1,file=build/OVMF_VARS.fd \
  -cdrom build/pyxis.iso -boot d -display "$QEMU_DISPLAY" -serial mon:stdio \
  -no-reboot -no-shutdown "$@"
