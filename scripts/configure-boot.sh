#!/bin/sh
set -eu
# Boot init reads boot://config/live.lua, or installed.lua when a disk is bound;
# spaces are configured there, not on the kernel command line.
boot_init=boot://boot-init.pxe
case "$BOOT_MENU_TIMEOUT" in
  ''|*[!0-9]*) echo 'BOOT_MENU_TIMEOUT must be a nonnegative decimal seconds count.' >&2; exit 1 ;;
esac
command_line="init=$boot_init"
case "${LOG_UDP:-0}" in
  0) ;;
  1) command_line="$command_line log.udp=1" ;;
  *) echo 'LOG_UDP must be 0 or 1.' >&2; exit 1 ;;
esac
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
display_size=${DISPLAY_SIZE:-}
if [ -n "$display_size" ]; then
  # Pass malformed geometry to the driver, but keep a single literal token.
  if [ "${#display_size}" -gt 4095 ] ||
      ! printf '%s' "$display_size" | LC_ALL=C tr -d '!-~' | cmp -s - /dev/null; then
    echo 'DISPLAY_SIZE must contain 1..4095 printable ASCII bytes without spaces.' >&2
    exit 1
  fi
  command_line="$command_line display.size=\${PYXIS_DISPLAY_SIZE}"
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
PYXIS_BEACON_NAME="$remote_beacon" PYXIS_DISPLAY_SIZE="$display_size" \
awk -v timeout="$BOOT_MENU_TIMEOUT" '
  BEGIN {
    normal = ENVIRON["PYXIS_NORMAL_LINE"]; rescue = ENVIRON["PYXIS_RESCUE_LINE"]
    beacon = ENVIRON["PYXIS_BEACON_NAME"]
    display = ENVIRON["PYXIS_DISPLAY_SIZE"]
    normal_bytes = length(normal)
    if (beacon != "") normal_bytes += length(beacon) - length("${PYXIS_REMOTE_BEACON}")
    if (display != "") normal_bytes += length(display) - length("${PYXIS_DISPLAY_SIZE}")
    install_bytes = length("init=boot://init-install.pxe boot.install=1")
    if (display != "") install_bytes += length(" display.size=") + length(display)
    if (normal_bytes > 4095 || install_bytes > 4095 ||
        (rescue != "" && normal_bytes + length(" boot.default_config=1") > 4095)) {
      print "Kernel command line must fit within 4095 bytes." > "/dev/stderr"
      exit 1
    }
    if (beacon != "") print "${PYXIS_REMOTE_BEACON}=" beacon
    if (display != "") print "${PYXIS_DISPLAY_SIZE}=" display
  }
  /^\// { skip = $0 == "/Pyxis OS (rescue)" && rescue == "" }
  skip { next }
  $0 == "# PYXIS_BOOT_MENU_TIMEOUT" { print "timeout: " timeout; next }
  $0 == "# PYXIS_NORMAL_COMMAND_LINE" { print "  cmdline: " normal; next }
  $0 == "# PYXIS_RESCUE_COMMAND_LINE" { print "  cmdline: " rescue; next }
  $0 == "  cmdline: init=boot://init-install.pxe boot.install=1" && display != "" {
    print $0 " display.size=${PYXIS_DISPLAY_SIZE}"; next
  }
  { print }
' boot/limine/limine.conf > build/limine.conf.tmp
cmp -s build/limine.conf.tmp build/limine.conf || mv build/limine.conf.tmp build/limine.conf
rm -f build/limine.conf.tmp
