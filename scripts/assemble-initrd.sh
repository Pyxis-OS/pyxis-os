#!/bin/sh
set -eu
command -v cpio >/dev/null 2>&1 || {
  echo 'Missing GNU cpio: install it, then run make initrd.' >&2
  exit 1
}
live_config=${LIVE_CONFIG:-}
if [ "${PYXIS_IMAGE_HOSTNAME_SET:-}" = 1 ]; then
  build/tools/machine-hostname "${live_config:-build/userspace-root/config/live.lua}" "$PYXIS_IMAGE_HOSTNAME" > build/live.lua.tmp
  mv build/live.lua.tmp build/live.lua
  live_config=build/live.lua
fi
cat build/sdk/manifest.txt build/bundle-info/ports.txt > build/guest-sdk-manifest.txt
"${LUA:-lua}" scripts/stage-tree.lua boot/initrd.lua build/initrd-root \
  userspace=build/userspace-root ports=build/ports-root sdk=build/sdk firmware=build/firmware/ax200 wifi_firmware=build/firmware/ax200-wifi \
  provenance=build/guest-sdk-manifest.txt "init=${INIT:-}" "network_config=${NETWORK_CONFIG:-}" \
  "live_config=$live_config" "go_hello=${GO_HELLO:-}"
# The independently assembled rescue archive contains no copy of this archive.
install -m 644 build/recovery.cpio build/initrd-root/share/installer/recovery.arc
touch -d @0 build/initrd-root/share/installer/recovery.arc
# Installed systems keep these executable files and complete bundle roots in boot://.
while IFS= read -r program; do
  { [ -f "build/initrd-root/$program" ] || [ -d "build/initrd-root/$program" ]; } || {
    echo "boot/rescue.list names $program, which the archive lacks" >&2
    exit 1
  }
done < boot/rescue.list
(cd build/initrd-root && find . -mindepth 1 -printf '%P\0' | LC_ALL=C sort -z) > build/initrd-files.list
(cd build/initrd-root && cpio --null --create --format=newc --reproducible \
  --owner=0:0 --quiet < ../initrd-files.list > ../initrd.cpio.tmp)
cmp -s build/initrd.cpio.tmp build/initrd.cpio || mv build/initrd.cpio.tmp build/initrd.cpio
rm -f build/initrd.cpio.tmp
