#!/bin/sh
set -eu
# Boot init reads boot://config/live.lua, or system://config/ on the pool when a
# disk is bound; spaces are configured there, not on the kernel command line.
boot_init=boot://boot-init.pxe
case "$BOOT_MENU_TIMEOUT" in
  ''|*[!0-9]*) echo 'BOOT_MENU_TIMEOUT must be a nonnegative decimal seconds count.' >&2; exit 1 ;;
esac
command_line="init=$boot_init"
case "${USB_HID_DESCRIBE:-0}" in
  0) ;;
  1) command_line="$command_line usb.hid.describe=1" ;;
  *) echo 'USB_HID_DESCRIBE must be 0 or 1.' >&2; exit 1 ;;
esac
case "${WIFI_AX200:-0}" in
  0) ;;
  1) command_line="$command_line wifi.ax200=1" ;;
  *) echo 'WIFI_AX200 must be 0 or 1.' >&2; exit 1 ;;
esac
case "${WIFI_RTL8922AE:-0}" in
  0) ;;
  1) command_line="$command_line wifi.rtl8922ae=1" ;;
  *) echo 'WIFI_RTL8922AE must be 0 or 1.' >&2; exit 1 ;;
esac
debug_net=${DEBUG_NET:-}
debug_wait=${DEBUG_WAIT:-0}
case "$debug_wait" in
  0|1) ;;
  *) echo 'DEBUG_WAIT must be 0 or 1.' >&2; exit 1 ;;
esac
if [ -n "$debug_net" ]; then
  if [ "${#debug_net}" -gt 63 ] ||
      ! printf '%s' "$debug_net" | LC_ALL=C tr -d '!-~' | cmp -s - /dev/null; then
    echo 'DEBUG_NET must name 1..63 printable ASCII bytes without spaces.' >&2
    exit 1
  fi
  # This external token identifies the exact staged ELF; no self-hash is linked
  # into that ELF and the target does not claim to measure its running bytes.
  debug_checksum=$(sha256sum build/caelum.elf)
  debug_image=${debug_checksum%% *}
  command_line="$command_line debug.net=\${PYXIS_DEBUG_NET} debug.image=$debug_image"
  if [ "$debug_wait" = 1 ]; then
    command_line="$command_line debug.wait=1"
  fi
elif [ "$debug_wait" = 1 ]; then
  echo 'DEBUG_WAIT=1 requires DEBUG_NET.' >&2
  exit 1
fi
case "${LOG_UDP:-0}" in
  0) ;;
  1) command_line="$command_line log.udp=1" ;;
  *) echo 'LOG_UDP must be 0 or 1.' >&2; exit 1 ;;
esac
# The kernel validates the level and warns before using its default.
log_console_level=${LOG_CONSOLE_LEVEL:-}
case "$log_console_level" in
  '') ;;
  *[!a-z]*) echo 'LOG_CONSOLE_LEVEL must be a lowercase level name.' >&2; exit 1 ;;
  *) command_line="$command_line log.level=$log_console_level" ;;
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
display_idle_blank=${DISPLAY_IDLE_BLANK:-}
case "$display_idle_blank" in
  '') ;;
  off) command_line="$command_line display.idle_blank=off" ;;
  *[!0-9]*) echo 'DISPLAY_IDLE_BLANK must be empty, off or a decimal seconds count.' >&2; exit 1 ;;
  *) command_line="$command_line display.idle_blank=$display_idle_blank" ;;
esac
display_inventory=${DISPLAY_INVENTORY:-0}
case "$display_inventory" in
  0) ;;
  1) command_line="$command_line display.inventory=1" ;;
  *) echo 'DISPLAY_INVENTORY must be 0 or 1.' >&2; exit 1 ;;
esac
display_flip=${DISPLAY_FLIP:-}
case "$display_flip" in
  '') ;;
  0|1) command_line="$command_line display.flip=$display_flip" ;;
  *) echo 'DISPLAY_FLIP must be empty, 0 or 1.' >&2; exit 1 ;;
esac
display_flip_metrics=${DISPLAY_FLIP_METRICS:-0}
case "$display_flip_metrics" in
  0) ;;
  1) command_line="$command_line display.flip.metrics=1" ;;
  *) echo 'DISPLAY_FLIP_METRICS must be 0 or 1.' >&2; exit 1 ;;
esac
display_cursor_probe=${DISPLAY_CURSOR_PROBE:-0}
case "$display_cursor_probe" in
  0) ;;
  1) command_line="$command_line display.cursor.probe=1" ;;
  *) echo 'DISPLAY_CURSOR_PROBE must be 0 or 1.' >&2; exit 1 ;;
esac
pointer_synthetic=${POINTER_SYNTHETIC:-0}
case "$pointer_synthetic" in
  0) ;;
  1) command_line="$command_line pointer.synthetic=schedule" ;;
  *) echo 'POINTER_SYNTHETIC must be 0 or 1.' >&2; exit 1 ;;
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
# A core image boots Install and Rescue only (docs/wip/pkg-upgrade.md, decision
# 64). Install is its first and default entry; the rescue entry, which starts
# boot init's built-in rescue space, is held back and follows it.
case "${DISTRIBUTION:-core}" in
  core|full) ;;
  *) echo 'DISTRIBUTION must be core or full.' >&2; exit 1 ;;
esac
PYXIS_NORMAL_LINE="$command_line" PYXIS_RESCUE_LINE="$rescue_line" \
PYXIS_DISTRIBUTION="${DISTRIBUTION:-core}" \
PYXIS_BEACON_NAME="$remote_beacon" PYXIS_DISPLAY_SIZE="$display_size" PYXIS_DISPLAY_TIMING="$display_timing" \
PYXIS_DEBUG_NAME="$debug_net" \
PYXIS_DISPLAY_TIMING_METRICS="$display_timing_metrics" PYXIS_DISPLAY_IDLE_BLANK="$display_idle_blank" \
PYXIS_DISPLAY_INVENTORY="$display_inventory" \
PYXIS_DISPLAY_FLIP="$display_flip" PYXIS_DISPLAY_FLIP_METRICS="$display_flip_metrics" \
PYXIS_DISPLAY_CURSOR_PROBE="$display_cursor_probe" \
awk -v timeout="$BOOT_MENU_TIMEOUT" '
  BEGIN {
    normal = ENVIRON["PYXIS_NORMAL_LINE"]; rescue = ENVIRON["PYXIS_RESCUE_LINE"]
    core = ENVIRON["PYXIS_DISTRIBUTION"] == "core"
    if (core) normal = normal " boot.default_config=1"
    beacon = ENVIRON["PYXIS_BEACON_NAME"]
    debug_name = ENVIRON["PYXIS_DEBUG_NAME"]
    display = ENVIRON["PYXIS_DISPLAY_SIZE"]
    timing = ENVIRON["PYXIS_DISPLAY_TIMING"]
    timing_metrics = ENVIRON["PYXIS_DISPLAY_TIMING_METRICS"] == "1"
    idle_blank = ENVIRON["PYXIS_DISPLAY_IDLE_BLANK"]
    inventory = ENVIRON["PYXIS_DISPLAY_INVENTORY"] == "1"
    flip = ENVIRON["PYXIS_DISPLAY_FLIP"]
    flip_metrics = ENVIRON["PYXIS_DISPLAY_FLIP_METRICS"] == "1"
    cursor_probe = ENVIRON["PYXIS_DISPLAY_CURSOR_PROBE"] == "1"
    normal_bytes = length(normal)
    if (beacon != "") normal_bytes += length(beacon) - length("${PYXIS_REMOTE_BEACON}")
    if (debug_name != "") normal_bytes += length(debug_name) - length("${PYXIS_DEBUG_NET}")
    if (display != "") normal_bytes += length(display) - length("${PYXIS_DISPLAY_SIZE}")
    install_bytes = length("init=boot://init-fat32-inspect.pxe boot.install=1")
    if (display != "") install_bytes += length(" display.size=") + length(display)
    if (timing != "") install_bytes += length(" display.timing=") + length(timing)
    if (timing_metrics) install_bytes += length(" display.timing.metrics=1")
    if (idle_blank != "") install_bytes += length(" display.idle_blank=") + length(idle_blank)
    if (flip != "") install_bytes += length(" display.flip=") + length(flip)
    if (flip_metrics) install_bytes += length(" display.flip.metrics=1")
    if (inventory) install_bytes += length(" display.inventory=1")
    if (cursor_probe) install_bytes += length(" display.cursor.probe=1")
    if (normal_bytes > 4095 || install_bytes > 4095 ||
        (rescue != "" && normal_bytes + length(" boot.default_config=1") > 4095)) {
      print "Kernel command line must fit within 4095 bytes." > "/dev/stderr"
      exit 1
    }
    if (beacon != "") print "${PYXIS_REMOTE_BEACON}=" beacon
    if (debug_name != "") print "${PYXIS_DEBUG_NET}=" debug_name
    if (display != "") print "${PYXIS_DISPLAY_SIZE}=" display
  }
  function emit(line) {
    if (hold) held = held line "\n"
    else print line
  }
  /^\// {
    skip = $0 == "/Pyxis OS (rescue)" && (rescue == "" || core)
    if (core && $0 != "/Pyxis OS (Caelum)" && $0 != "/Install Pyxis") skip = 1
    hold = core && $0 == "/Pyxis OS (Caelum)"
  }
  skip { next }
  core && $0 == "/Pyxis OS (Caelum)" { emit("/Pyxis OS (rescue)"); next }
  $0 == "# PYXIS_BOOT_MENU_TIMEOUT" { emit("timeout: " timeout); next }
  $0 == "# PYXIS_NORMAL_COMMAND_LINE" { emit("  cmdline: " normal); next }
  $0 == "# PYXIS_RESCUE_COMMAND_LINE" { emit("  cmdline: " rescue); next }
  $0 == "  cmdline: init=boot://boot-init.pxe boot.install=1" ||
  $0 == "  cmdline: init=boot://init-fat32-inspect.pxe boot.install=1" ||
  $0 == "  cmdline: init=boot://init-fat32-write.pxe boot.install=1" ||
  $0 == "  cmdline: init=boot://init-fat32-mount.pxe boot.install=1" ||
  $0 == "  cmdline: init=boot://init-fat32-mount-write.pxe boot.install=1" ||
  $0 == "  cmdline: init=boot://init-fat32-mount.pxe" {
    line = $0
    if (display != "") line = line " display.size=${PYXIS_DISPLAY_SIZE}"
    if (timing != "") line = line " display.timing=" timing
    if (timing_metrics) line = line " display.timing.metrics=1"
    if (idle_blank != "") line = line " display.idle_blank=" idle_blank
    if (flip != "") line = line " display.flip=" flip
    if (flip_metrics) line = line " display.flip.metrics=1"
    if (inventory) line = line " display.inventory=1"
    if (cursor_probe) line = line " display.cursor.probe=1"
    emit(line); next
  }
  { emit($0) }
  END { printf "%s", held }
' boot/limine/limine.conf > build/limine.conf.tmp
cmp -s build/limine.conf.tmp build/limine.conf || mv build/limine.conf.tmp build/limine.conf
rm -f build/limine.conf.tmp
