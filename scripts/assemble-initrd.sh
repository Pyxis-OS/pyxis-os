#!/bin/sh
set -eu
command -v cpio >/dev/null 2>&1 || {
  echo 'Missing GNU cpio: install it, then run make initrd.' >&2
  exit 1
}
cat build/sdk/manifest.txt build/bundle-info/ports.txt > build/guest-sdk-manifest.txt
"${LUA:-lua}" scripts/stage-tree.lua boot/initrd.lua build/initrd-root \
  userspace=build/userspace-root ports=build/ports-root sdk=build/sdk \
  provenance=build/guest-sdk-manifest.txt "init=${INIT:-}" "wad=${DOOM_WAD:-}" "demos=${DOOM_DEMOS:-}"
(cd build/initrd-root && find . -mindepth 1 -printf '%P\0' | LC_ALL=C sort -z) > build/initrd-files.list
(cd build/initrd-root && cpio --null --create --format=newc --reproducible \
  --owner=0:0 --quiet < ../initrd-files.list > ../initrd.cpio.tmp)
cmp -s build/initrd.cpio.tmp build/initrd.cpio || mv build/initrd.cpio.tmp build/initrd.cpio
rm -f build/initrd.cpio.tmp
