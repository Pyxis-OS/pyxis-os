#!/bin/sh
set -eu
command -v cpio >/dev/null 2>&1 || {
  echo 'Missing GNU cpio: install it, then run make recovery-initrd.' >&2
  exit 1
}
"${LUA:-lua}" scripts/stage-tree.lua scripts/recovery-initrd.lua build/recovery-root \
  userspace=build/userspace-root ports=build/ports-root
(cd build/recovery-root && find . -mindepth 1 -printf '%P\0' | LC_ALL=C sort -z) > build/recovery-files.list
(cd build/recovery-root && cpio --null --create --format=newc --reproducible \
  --owner=0:0 --quiet < ../recovery-files.list > ../recovery.cpio.tmp)
cmp -s build/recovery.cpio.tmp build/recovery.cpio || mv build/recovery.cpio.tmp build/recovery.cpio
rm -f build/recovery.cpio.tmp
