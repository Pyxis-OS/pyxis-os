#!/bin/sh
set -eu
umask 077

for tool in sgdisk mkfs.fat mmd mcopy mktemp truncate dd; do
  command -v "$tool" >/dev/null 2>&1 || {
    echo "Missing $tool: USB image assembly requires GPT fdisk, dosfstools, mtools and GNU coreutils." >&2
    exit 1
  }
done

valid_mib() {
  case "$2" in
    ''|0*|*[!0-9]*) echo "$1 must be a positive decimal MiB count without leading zeros." >&2; exit 1 ;;
  esac
  # Keep byte-size arithmetic within a signed 64-bit host file offset.
  if [ "${#2}" -gt 13 ] || [ "$2" -gt 8796093022207 ]; then
    echo "$1 exceeds the host image-size range." >&2
    exit 1
  fi
}
valid_mib USB_IMAGE_MIB "$USB_IMAGE_MIB"
valid_mib USB_ESP_MIB "$USB_ESP_MIB"
if [ "$USB_IMAGE_MIB" -le "$((USB_ESP_MIB + 2))" ]; then
  echo 'USB_IMAGE_MIB must leave room for the ESP, pool and two MiB of GPT/alignment space.' >&2
  exit 1
fi
case "$USB_POOL_OWNER" in
  *[!a-fA-F0-9]*) echo 'USB_POOL_OWNER contains invalid hex digits.' >&2; exit 1 ;;
esac
if [ "${#USB_POOL_OWNER}" -ne 32 ] || [ "$USB_POOL_OWNER" = 00000000000000000000000000000000 ]; then
  echo 'USB_POOL_OWNER must be supplied as a nonzero principal ID (32 hex digits).' >&2
  exit 1
fi

image=build/pyxis-usb.img
if [ -L "$image" ] || { [ -e "$image" ] && [ ! -f "$image" ]; }; then
  echo "$image must be absent or a regular file, not a symlink or device." >&2
  exit 1
fi
mkdir -p build
staging=$(mktemp -d build/usb-image.XXXXXX)
trap 'rm -rf -- "$staging"' EXIT
trap 'exit 1' HUP INT TERM

mib_bytes=1048576
sector_bytes=512
esp_start=$((mib_bytes / sector_bytes))
esp_end=$((esp_start + USB_ESP_MIB * mib_bytes / sector_bytes - 1))
pool_start_mib=$((USB_ESP_MIB + 1))
pool_start=$((pool_start_mib * mib_bytes / sector_bytes))
pool_mib=$((USB_IMAGE_MIB - USB_ESP_MIB - 2))
pool_end=$((pool_start + pool_mib * mib_bytes / sector_bytes - 1))
# This type describes pool contents; identity and mount authority remain separate.
pool_type=1a8194a3-8a07-4dff-830e-4cb4ed7aac00

mkdir -p "$staging/source/bin"
cat > "$staging/source/README.txt" <<'EOF'
Pyxis USB read-only sample volume

This usb-test volume contains known text and bin/cat.pxe captured from the matching
boot archive. Firmware loads the EFI kernel/archive pair; native USB reads
require the separate kernel USB milestone. This is not a persistent home volume.
EOF
cp build/initrd-root/cat.pxe "$staging/source/bin/cat.pxe"
build/fs-tools/mkpyxisfs --image "$staging/pool.img" \
  --size "$((pool_mib * mib_bytes))" \
  --volume usb-test --source "$staging/source" --owner "$USB_POOL_OWNER"

mkfs.fat -C -F 32 -S "$sector_bytes" -h "$esp_start" -n PYXIS_BOOT \
  "$staging/esp.img" "$((USB_ESP_MIB * 1024))"
mmd -i "$staging/esp.img" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
mcopy -i "$staging/esp.img" third_party/limine/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$staging/esp.img" build/caelum.elf ::/boot/caelum.elf
mcopy -i "$staging/esp.img" build/initrd.cpio ::/boot/initrd.cpio
mcopy -i "$staging/esp.img" build/limine.conf ::/boot/limine/limine.conf

truncate -s "$((USB_IMAGE_MIB * mib_bytes))" "$staging/disk.img"
sgdisk --clear \
  --new="1:$esp_start:$esp_end" --typecode=1:ef00 --change-name='1:Pyxis EFI' \
  --new="2:$pool_start:$pool_end" --typecode="2:$pool_type" --change-name='2:Pyxis pool' \
  "$staging/disk.img"
dd if="$staging/esp.img" of="$staging/disk.img" bs=1M seek=1 conv=notrunc,sparse status=none
dd if="$staging/pool.img" of="$staging/disk.img" bs=1M seek="$pool_start_mib" conv=notrunc,sparse status=none
mv -T "$staging/disk.img" "$image"
printf 'Built %s: %s MiB, ESP %s MiB, usb-test pool %s MiB; owner %s\n' \
  "$image" "$USB_IMAGE_MIB" "$USB_ESP_MIB" "$pool_mib" "$USB_POOL_OWNER"
