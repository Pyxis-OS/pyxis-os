#!/bin/sh
set -eu
# These select archive entries, not host files. INIT still stages a host file
# as boot://init. Keep this grammar free of shell quoting and Limine expansion.
valid_image() {
  case "$1" in
    boot://?*) ;;
    *) echo "Init must name a boot:// archive entry: $1" >&2; exit 1 ;;
  esac
  case "$1" in
    *[!a-zA-Z0-9_./:+-]*) echo "Unsupported character in init path: $1" >&2; exit 1 ;;
  esac
}
case "$BOOT_MENU_TIMEOUT" in
  ''|*[!0-9]*) echo 'BOOT_MENU_TIMEOUT must be a nonnegative decimal seconds count.' >&2; exit 1 ;;
esac
# Space names: 1-31 of a-z, 0-9 and '-', unique; the kernel checks them again.
valid_space_name() {
  case "$1" in
    ''|*[!a-z0-9-]*) echo "Invalid space name: $1" >&2; exit 1 ;;
  esac
  [ "${#1}" -le 31 ] || { echo "Space name exceeds 31 characters: $1" >&2; exit 1; }
}
command_line=
names=' '
# Deliberate whitespace splitting; glob expansion is disabled for selections.
set -f
for selection in $SPACES; do
  case "$selection" in
    *=*) ;;
    *) echo "Space selection must be NAME=IMAGE: $selection" >&2; exit 1 ;;
  esac
  name=${selection%%=*}
  image=${selection#*=}
  valid_space_name "$name"
  case "$names" in
    *" $name "*) echo "Duplicate space name: $name" >&2; exit 1 ;;
  esac
  names="$names$name "
  valid_image "$image"
  command_line="${command_line:+$command_line }space.$name=$image"
done
[ -n "$command_line" ] || { echo 'SPACES must configure at least one space.' >&2; exit 1; }
# CPU lists: comma-separated decimal indices and inclusive A-B ranges, without
# leading zeros, at most nine digits. Indices beyond the booted CPU count are
# reported at boot.
valid_cpu_number() {
  case "$1" in
    0) ;;
    ''|0*|*[!0-9]*) return 1 ;;
  esac
  [ "${#1}" -le 9 ]
}
valid_cpu_list() {
  rest=$1,
  while [ -n "$rest" ]; do
    entry=${rest%%,*}
    rest=${rest#*,}
    case "$entry" in
      *-*)
        first=${entry%%-*}
        last=${entry#*-}
        valid_cpu_number "$first" && valid_cpu_number "$last" &&
          [ "$first" -le "$last" ] || return 1 ;;
      *) valid_cpu_number "$entry" || return 1 ;;
    esac
  done
}
cpu_names=' '
for selection in $SPACE_CPUS; do
  case "$selection" in
    *=?*) ;;
    *) echo "Space CPU set must be NAME=LIST: $selection" >&2; exit 1 ;;
  esac
  name=${selection%%=*}
  list=${selection#*=}
  case "$names" in
    *" $name "*) ;;
    *) echo "CPU set names a space not in SPACES: $name" >&2; exit 1 ;;
  esac
  case "$cpu_names" in
    *" $name "*) echo "Duplicate CPU set for space: $name" >&2; exit 1 ;;
  esac
  cpu_names="$cpu_names$name "
  valid_cpu_list "$list" || { echo "Invalid CPU list for space $name: $list" >&2; exit 1; }
  command_line="$command_line space.$name.cpus=$list"
done
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
fi
if [ "${#command_line}" -gt 4095 ]; then
  echo 'Kernel command line exceeds 4095 bytes.' >&2
  exit 1
fi
mkdir -p build
awk -v normal="$command_line" -v timeout="$BOOT_MENU_TIMEOUT" '
  $0 == "# PYXIS_BOOT_MENU_TIMEOUT" { print "timeout: " timeout; next }
  $0 == "# PYXIS_NORMAL_COMMAND_LINE" { print "  cmdline: " normal; next }
  { print }
' boot/limine/limine.conf > build/limine.conf.tmp
cmp -s build/limine.conf.tmp build/limine.conf || mv build/limine.conf.tmp build/limine.conf
rm -f build/limine.conf.tmp
