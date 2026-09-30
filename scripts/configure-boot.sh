#!/bin/sh
set -eu
# These select archive entries, not host files. INIT still stages a host file
# as app://init. Keep this grammar free of shell quoting and Limine expansion.
valid_image() {
  case "$1" in
    app://?*) ;;
    *) echo "Init must name an app:// archive entry: $1" >&2; exit 1 ;;
  esac
  case "$1" in
    *[!a-zA-Z0-9_./:+-]*) echo "Unsupported character in init path: $1" >&2; exit 1 ;;
  esac
}
valid_image "$INIT_DEFAULT"
valid_image "$INIT_PRIMARY"
command_line="init=$INIT_DEFAULT init.primary=$INIT_PRIMARY"
# Deliberate whitespace splitting; glob expansion is disabled for selections.
set -f
for selection in $INIT_CPUS; do
  cpu=${selection%%=*}
  image=${selection#*=}
  case "$cpu" in
    ''|*[!0-9]*) echo "Invalid init CPU selection: $selection" >&2; exit 1 ;;
  esac
  valid_image "$image"
  command_line="$command_line init.$cpu=$image"
done
mount_disk=${MOUNT_DISK:-}
mount_principal=${MOUNT_PRINCIPAL:-}
if [ -n "$mount_disk" ] || [ -n "$mount_principal" ]; then
  if [ -z "$mount_disk" ] || [ -z "$mount_principal" ]; then
    echo 'MOUNT_DISK and MOUNT_PRINCIPAL must be supplied together.' >&2
    exit 1
  fi
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
  case "$mount_principal" in
    *[!a-fA-F0-9]*) echo 'MOUNT_PRINCIPAL contains invalid hex digits.' >&2; exit 1 ;;
  esac
  if [ "${#mount_principal}" -ne 32 ] || [ "$mount_principal" = 00000000000000000000000000000000 ]; then
    echo 'MOUNT_PRINCIPAL must be a nonzero 128-bit ID (32 hex digits).' >&2
    exit 1
  fi
  command_line="$command_line mount.disk=$mount_disk mount.principal=$mount_principal"
fi
if [ "${#command_line}" -gt 4095 ]; then
  echo 'Kernel command line exceeds 4095 bytes.' >&2
  exit 1
fi
mkdir -p build
cat boot/limine/limine.conf > build/limine.conf.tmp
printf '  cmdline: %s\n' "$command_line" >> build/limine.conf.tmp
cmp -s build/limine.conf.tmp build/limine.conf || mv build/limine.conf.tmp build/limine.conf
rm -f build/limine.conf.tmp
