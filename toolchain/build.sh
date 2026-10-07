#!/bin/sh
# Build the pinned cross compiler separately from normal OS/SDK builds.
set -eu

if [ "$#" -ne 2 ]; then
  echo 'Usage: toolchain/build.sh PREFIX WORKDIR (absolute paths)' >&2
  exit 1
fi
prefix=$1
work=$2
case "$prefix:$work" in
  /*:/*) ;;
  *) echo 'PREFIX and WORKDIR must be absolute paths' >&2; exit 1 ;;
esac
patches=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
jobs=${JOBS:-4}
target=x86_64-unknown-pyxis
export PATH="$prefix/bin:$PATH"
mkdir -p "$prefix" "$work"
cd "$work"

# Existing archives may be supplied for offline builds, but always verify them.
# Downloads come only from the internal cache of ftp.gnu.org.
gnu_cache=https://repo.internal/repository/raw-gnu/gnu
if [ ! -f binutils-2.47.tar.xz ]; then
  curl -fL --retry 3 "$gnu_cache/binutils/binutils-2.47.tar.xz" \
    -o binutils-2.47.tar.xz
fi
if [ ! -f gcc-16.2.0.tar.gz ]; then
  curl -fL --retry 3 "$gnu_cache/gcc/gcc-16.2.0/gcc-16.2.0.tar.gz" \
    -o gcc-16.2.0.tar.gz
fi
sha256sum -c "$patches/SHA256SUMS"

# Refuse to reuse configured/patched source trees from an earlier build.
mkdir sources build-binutils build-gcc
tar -xf binutils-2.47.tar.xz -C sources
tar -xf gcc-16.2.0.tar.gz -C sources
patch --batch --fuzz=0 -d sources/binutils-2.47 -p1 < "$patches/binutils-2.47-pyxis.patch"
patch --batch --fuzz=0 -d sources/gcc-16.2.0 -p1 < "$patches/gcc-16.2.0-pyxis.patch"

cd build-binutils
../sources/binutils-2.47/configure \
  --target="$target" --prefix="$prefix" --with-sysroot \
  --disable-nls --disable-werror --disable-multilib
make -j"$jobs"
make install

cd ../build-gcc
../sources/gcc-16.2.0/configure \
  --target="$target" --prefix="$prefix" \
  --with-sysroot="$prefix/$target/sysroot" \
  --with-native-system-header-dir=/usr/include --without-headers \
  --disable-nls --disable-multilib --disable-shared --disable-threads \
  --disable-decimal-float --disable-fixincludes --enable-languages=c
make -j"$jobs" all-gcc
make -j"$jobs" all-target-libgcc
make install-gcc
make install-target-libgcc

mkdir -p "$prefix/share/pyxis-toolchain"
cp "$patches/SHA256SUMS" "$patches/"*.patch "$patches/README.md" \
  "$prefix/share/pyxis-toolchain/"
cp ../sources/gcc-16.2.0/COPYING3 ../sources/gcc-16.2.0/COPYING.RUNTIME \
  "$prefix/share/pyxis-toolchain/"
