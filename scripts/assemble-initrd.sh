#!/bin/sh
set -eu
command -v cpio >/dev/null 2>&1 || {
  echo 'Missing GNU cpio: install it, then run make initrd.' >&2
  exit 1
}
# QUAKE_DATA is a private id1 directory; accept either case for its pak names.
quake_pak0= quake_pak1=
if [ -n "${QUAKE_DATA:-}" ]; then
  for name in pak0.pak PAK0.PAK; do
    [ -f "$QUAKE_DATA/$name" ] && quake_pak0=$QUAKE_DATA/$name && break
  done
  for name in pak1.pak PAK1.PAK; do
    [ -f "$QUAKE_DATA/$name" ] && quake_pak1=$QUAKE_DATA/$name && break
  done
  [ -n "$quake_pak0" ] || {
    echo "QUAKE_DATA=$QUAKE_DATA has no pak0.pak" >&2
    exit 1
  }
fi
# The guest SDK carries the compiler runtime its archives were built with.
case "$(sed -n 's/^PYXIS_TOOLCHAIN := //p' build/sdk/share/toolchain.mk)" in
  gcc) sdk_runtime=libgcc.a ;;
  llvm) sdk_runtime=libclang_rt.builtins.a ;;
  *) echo 'build/sdk/share/toolchain.mk names no known toolchain' >&2; exit 1 ;;
esac
cat build/sdk/manifest.txt build/bundle-info/ports.txt > build/guest-sdk-manifest.txt
"${LUA:-lua}" scripts/stage-tree.lua boot/initrd.lua build/initrd-root \
  userspace=build/userspace-root ports=build/ports-root sdk=build/sdk sdk_runtime=$sdk_runtime \
  provenance=build/guest-sdk-manifest.txt "init=${INIT:-}" "network_config=${NETWORK_CONFIG:-}" \
  "wad=${DOOM_WAD:-}" "demos=${DOOM_DEMOS:-}" \
  "quake_pak0=$quake_pak0" "quake_pak1=$quake_pak1"
# Installed systems keep only these executables in boot://; the rest go to bin://.
while IFS= read -r program; do
  [ -f "build/initrd-root/$program" ] || {
    echo "boot/rescue.list names $program, which the archive lacks" >&2
    exit 1
  }
done < boot/rescue.list
(cd build/initrd-root && find . -mindepth 1 -printf '%P\0' | LC_ALL=C sort -z) > build/initrd-files.list
(cd build/initrd-root && cpio --null --create --format=newc --reproducible \
  --owner=0:0 --quiet < ../initrd-files.list > ../initrd.cpio.tmp)
cmp -s build/initrd.cpio.tmp build/initrd.cpio || mv build/initrd.cpio.tmp build/initrd.cpio
rm -f build/initrd.cpio.tmp
