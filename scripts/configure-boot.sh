#!/bin/sh
set -eu
# Boot init reads boot://config/live.lua, or installed.lua when a disk is bound;
# spaces are configured there, not on the kernel command line.
boot_init=boot://boot-init.pxe
case "$BOOT_MENU_TIMEOUT" in
  ''|*[!0-9]*) echo 'BOOT_MENU_TIMEOUT must be a nonnegative decimal seconds count.' >&2; exit 1 ;;
esac
command_line="init=$boot_init"
remote_beacon=${REMOTE_BEACON:-}
if [ -n "$remote_beacon" ]; then
  # Match the public beacon name's unquoted ASCII command-line representation.
  if [ "${#remote_beacon}" -gt 63 ] ||
      ! printf '%s' "$remote_beacon" | LC_ALL=C tr -d '!-~' | cmp -s - /dev/null; then
    echo 'REMOTE_BEACON must name 1..63 printable ASCII bytes without spaces.' >&2
    exit 1
  fi
  # Limine expands macros once. A definition preserves literal ${...} in names.
  command_line="$command_line remote.beacon=\${PYXIS_REMOTE_BEACON}"
fi
rescue_line=
mount_disk=${MOUNT_DISK:-}
if [ -n "$mount_disk" ]; then
  case "$mount_disk" in
    ????????-????-????-????-????????????) ;;
    *) echo 'MOUNT_DISK must be a canonical GPT GUID.' >&2; exit 1 ;;
  esac
  disk_hex=$(printf '%s' "$mount_disk" | tr -d '-')
  case "$disk_hex" in
    *[!a-fA-F0-9]*) echo 'MOUNT_DISK contains invalid hex digits.' >&2; exit 1 ;;
  esac
  if [ "${#disk_hex}" -ne 32 ] || [ "$disk_hex" = 00000000000000000000000000000000 ]; then
    echo 'MOUNT_DISK must be a nonzero canonical GPT GUID.' >&2
    exit 1
  fi
  command_line="$command_line mount.disk=$mount_disk"
  # Only a bound disk has a pool override for the rescue entry to ignore.
  rescue_line="$command_line boot.default_config=1"
fi
mkdir -p build
PYXIS_NORMAL_LINE="$command_line" PYXIS_RESCUE_LINE="$rescue_line" \
PYXIS_BEACON_NAME="$remote_beacon" \
awk -v timeout="$BOOT_MENU_TIMEOUT" '
  BEGIN {
    normal = ENVIRON["PYXIS_NORMAL_LINE"]; rescue = ENVIRON["PYXIS_RESCUE_LINE"]
    beacon = ENVIRON["PYXIS_BEACON_NAME"]
    if (beacon != "") print "${PYXIS_REMOTE_BEACON}=" beacon
  }
  /^\// { skip = $0 == "/Pyxis OS (rescue)" && rescue == "" }
  skip { next }
  $0 == "# PYXIS_BOOT_MENU_TIMEOUT" { print "timeout: " timeout; next }
  $0 == "# PYXIS_NORMAL_COMMAND_LINE" { print "  cmdline: " normal; next }
  $0 == "# PYXIS_RESCUE_COMMAND_LINE" { print "  cmdline: " rescue; next }
  { print }
' boot/limine/limine.conf > build/limine.conf.tmp
cmp -s build/limine.conf.tmp build/limine.conf || mv build/limine.conf.tmp build/limine.conf
rm -f build/limine.conf.tmp
