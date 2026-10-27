#!/bin/sh
# Build native LLVM once in the compiler container, using a disposable SDK.
set -eu
if [ "$#" -ne 3 ]; then
  echo 'Usage: toolchain/build-container-libraries.sh PREFIX WORKDIR LLVM_SOURCE (absolute paths)' >&2
  exit 1
fi
prefix=$1
work=$2
llvm_source=$3
case "$prefix:$work:$llvm_source" in
  /*:/*:/*) ;;
  *) echo 'Use absolute paths' >&2; exit 1 ;;
esac
toolchain=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
revision=$(cat "$toolchain/llvm-development/sdk-revision")
jobs=${JOBS:-8}
mkdir "$work"
source=$work/pyxis
repository=https://git.internal/PyxisOS/pyxis-os.git
git init -q "$source"
git -C "$source" remote add origin "$repository"
git -C "$source" fetch -q --depth 1 origin "$revision"
git -C "$source" checkout -q --detach FETCH_HEAD
test "$(git -C "$source" rev-parse HEAD)" = "$revision"
git -C "$source" submodule update --init --depth 1 -- userspace fs
export PATH="$prefix/bin:$PATH"
make -C "$source" -j"$jobs" sdk
LLVM_SOURCE="$llvm_source" JOBS="$jobs" "$toolchain/build-llvm-development.sh" \
  "$source/build/sdk" "$work/export" "$work/llvm"
set -- "$work"/export/llvm-development-*/
test "$#" -eq 1 && test -d "$1"
mkdir "$prefix/llvm-development"
cp -a "$1". "$prefix/llvm-development/"
ln -s ../llvm-development/bin/llvm-config "$prefix/bin/x86_64-unknown-pyxis-llvm-config"
cp "$toolchain/llvm-development/sdk-revision" \
  "$prefix/share/pyxis-toolchain/llvm-development-sdk-revision"
"$prefix/bin/x86_64-unknown-pyxis-llvm-config" --host-target
