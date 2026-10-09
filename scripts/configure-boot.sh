#!/bin/sh
set -eu
# Boot init reads boot://config/live.lua, or installed.lua when a disk is bound;
# spaces are configured there, not on the kernel command line.
boot_init=boot://boot-init.pxe
case "$BOOT_MENU_TIMEOUT" in
  ''|*[!0-9]*) echo 'BOOT_MENU_TIMEOUT must be a nonnegative decimal seconds count.' >&2; exit 1 ;;
esac
command_line="init=$boot_init"
case "${DEBUG_CHECKPOINT:-0}" in
  0) ;;
  1) command_line="$command_line debug.checkpoint=1" ;;
  *) echo 'DEBUG_CHECKPOINT must be 0 or 1.' >&2; exit 1 ;;
esac
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
display_timing=${DISPLAY_TIMING:-}
case "$display_timing" in
  '') ;;
  off|observe|blank) command_line="$command_line display.timing=$display_timing" ;;
  *) echo 'DISPLAY_TIMING must be off, observe or blank.' >&2; exit 1 ;;
esac
display_timing_metrics=${DISPLAY_TIMING_METRICS:-0}
case "$display_timing_metrics" in
  0) ;;
  1) command_line="$command_line display.timing.metrics=1" ;;
  *) echo 'DISPLAY_TIMING_METRICS must be 0 or 1.' >&2; exit 1 ;;
esac
display_inventory=${DISPLAY_INVENTORY:-0}
case "$display_inventory" in
  0) ;;
  1) command_line="$command_line display.inventory=1" ;;
  *) echo 'DISPLAY_INVENTORY must be 0 or 1.' >&2; exit 1 ;;
esac
display_flip=${DISPLAY_FLIP:-0}
case "$display_flip" in
  0) ;;
  1) command_line="$command_line display.flip=1" ;;
  *) echo 'DISPLAY_FLIP must be 0 or 1.' >&2; exit 1 ;;
esac
display_flip_metrics=${DISPLAY_FLIP_METRICS:-0}
case "$display_flip_metrics" in
  0) ;;
  1) command_line="$command_line display.flip.metrics=1" ;;
  *) echo 'DISPLAY_FLIP_METRICS must be 0 or 1.' >&2; exit 1 ;;
esac
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
PYXIS_BEACON_NAME="$remote_beacon" PYXIS_DISPLAY_SIZE="$display_size" PYXIS_DISPLAY_TIMING="$display_timing" \
PYXIS_DISPLAY_TIMING_METRICS="$display_timing_metrics" PYXIS_DISPLAY_INVENTORY="$display_inventory" \
PYXIS_DISPLAY_FLIP="$display_flip" PYXIS_DISPLAY_FLIP_METRICS="$display_flip_metrics" \
awk -v timeout="$BOOT_MENU_TIMEOUT" '
  BEGIN {
    normal = ENVIRON["PYXIS_NORMAL_LINE"]; rescue = ENVIRON["PYXIS_RESCUE_LINE"]
    beacon = ENVIRON["PYXIS_BEACON_NAME"]
    display = ENVIRON["PYXIS_DISPLAY_SIZE"]
    timing = ENVIRON["PYXIS_DISPLAY_TIMING"]
    timing_metrics = ENVIRON["PYXIS_DISPLAY_TIMING_METRICS"] == "1"
    inventory = ENVIRON["PYXIS_DISPLAY_INVENTORY"] == "1"
    flip = ENVIRON["PYXIS_DISPLAY_FLIP"] == "1"
    flip_metrics = ENVIRON["PYXIS_DISPLAY_FLIP_METRICS"] == "1"
    normal_bytes = length(normal)
    if (beacon != "") normal_bytes += length(beacon) - length("${PYXIS_REMOTE_BEACON}")
    if (display != "") normal_bytes += length(display) - length("${PYXIS_DISPLAY_SIZE}")
    install_bytes = length("init=boot://init-install.pxe boot.install=1")
    if (display != "") install_bytes += length(" display.size=") + length(display)
    if (timing != "") install_bytes += length(" display.timing=") + length(timing)
    if (timing_metrics) install_bytes += length(" display.timing.metrics=1")
    if (flip) install_bytes += length(" display.flip=1")
    if (flip_metrics) install_bytes += length(" display.flip.metrics=1")
    if (inventory) install_bytes += length(" display.inventory=1")
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
  $0 == "  cmdline: init=boot://init-install.pxe boot.install=1" {
    line = $0
    if (display != "") line = line " display.size=${PYXIS_DISPLAY_SIZE}"
    if (timing != "") line = line " display.timing=" timing
    if (timing_metrics) line = line " display.timing.metrics=1"
    if (flip) line = line " display.flip=1"
    if (flip_metrics) line = line " display.flip.metrics=1"
    if (inventory) line = line " display.inventory=1"
    print line; next
  }
  { print }
' boot/limine/limine.conf > build/limine.conf.tmp
cmp -s build/limine.conf.tmp build/limine.conf || mv build/limine.conf.tmp build/limine.conf
rm -f build/limine.conf.tmp
