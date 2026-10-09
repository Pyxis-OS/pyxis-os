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
# DIABLO_DATA is a private directory holding the Diablo shareware spawn.mpq.
diablo_spawn=
if [ -n "${DIABLO_DATA:-}" ]; then
  for name in spawn.mpq SPAWN.MPQ; do
    [ -f "$DIABLO_DATA/$name" ] && diablo_spawn=$DIABLO_DATA/$name && break
  done
  [ -n "$diablo_spawn" ] || {
    echo "DIABLO_DATA=$DIABLO_DATA has no spawn.mpq" >&2
    exit 1
  }
  [ -f build/ports/devilutionx/stage/bin/devilutionx.pxe ] || {
    echo "DIABLO_DATA is set but DevilutionX is not built: run make ports with it." >&2
    exit 1
  }
fi
# DUKE3D_DATA is a private directory holding the owner's duke3d.grp.
duke3d_grp=
if [ -n "${DUKE3D_DATA:-}" ]; then
  for name in duke3d.grp DUKE3D.GRP; do
    [ -f "$DUKE3D_DATA/$name" ] && duke3d_grp=$DUKE3D_DATA/$name && break
  done
  [ -n "$duke3d_grp" ] || {
    echo "DUKE3D_DATA=$DUKE3D_DATA has no duke3d.grp" >&2
    exit 1
  }
  [ -f build/ports/eduke32/stage/bin/eduke32.pxe ] || {
    echo "DUKE3D_DATA is set but EDuke32 is not built: run make ports with it." >&2
    exit 1
  }
fi
cat build/sdk/manifest.txt build/bundle-info/ports.txt > build/guest-sdk-manifest.txt
"${LUA:-lua}" scripts/stage-tree.lua boot/initrd.lua build/initrd-root \
  userspace=build/userspace-root ports=build/ports-root sdk=build/sdk firmware=build/firmware/ax200 \
  provenance=build/guest-sdk-manifest.txt "init=${INIT:-}" "network_config=${NETWORK_CONFIG:-}" \
  "wad=${DOOM_WAD:-}" "demos=${DOOM_DEMOS:-}" \
  "quake_pak0=$quake_pak0" "quake_pak1=$quake_pak1" \
  "diablo_spawn=$diablo_spawn" devilutionx=build/ports/devilutionx/stage \
  "duke3d_grp=$duke3d_grp" eduke32=build/ports/eduke32/stage
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
