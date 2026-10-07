#!/bin/sh
# Build the pinned LLVM cross toolchain separately from normal OS/SDK builds.
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
toolchain=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
jobs=${JOBS:-4}
target=x86_64-unknown-pyxis

# Pyxis commits on top of llvmorg-23.1.3, from the pyxis-23.1.3 branch.
llvm_repository=https://git.internal/PyxisOS/pyxis-llvm.git
llvm_commit=41ab6043cc4fd63e0a358d1e60bba249a751d8ee

mkdir -p "$prefix" "$work"
cd "$work"

# Refuse to reuse a configured tree from an earlier build. LLVM_SOURCE selects
# an existing fork checkout instead of the pinned commit, for fork development.
mkdir build-llvm build-builtins
if [ -n "${LLVM_SOURCE:-}" ]; then
  source=$LLVM_SOURCE
else
  source=$work/llvm-project
  git init -q "$source"
  git -C "$source" fetch -q --depth 1 "$llvm_repository" "$llvm_commit"
  git -C "$source" checkout -q --detach FETCH_HEAD
  test "$(git -C "$source" rev-parse HEAD)" = "$llvm_commit"
fi

# Only the X86 backend, Clang, LLD and the archive/object tools Pyxis uses.
# Optional host libraries stay off so the installation runs without them.
components='clang;clang-resource-headers;lld;llvm-ar;llvm-ranlib;llvm-nm;llvm-objcopy;llvm-strip;llvm-objdump;llvm-readobj;llvm-size;llvm-symbolizer'
cmake -G Ninja -S "$source/llvm" -B build-llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DLLVM_ENABLE_PROJECTS='clang;lld' \
  -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_DEFAULT_TARGET_TRIPLE="$target" \
  -DLLVM_ENABLE_ASSERTIONS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF \
  -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON \
  -DLLVM_DISTRIBUTION_COMPONENTS="$components" \
  -DLLVM_PARALLEL_LINK_JOBS="${LINK_JOBS:-2}" \
  ${LLVM_USE_LINKER:+-DLLVM_USE_LINKER="$LLVM_USE_LINKER"}
ninja -C build-llvm -j"$jobs" install-distribution

# Compiler helpers replace libgcc. The Pyxis driver builds them without the
# red zone; they land in the resource directory the driver links from.
resource=$("$prefix/bin/clang" -print-resource-dir)
cmake -G Ninja -S "$source/compiler-rt/lib/builtins" -B build-builtins \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Generic \
  -DCMAKE_C_COMPILER="$prefix/bin/clang" \
  -DCMAKE_ASM_COMPILER="$prefix/bin/clang" \
  -DCMAKE_C_COMPILER_TARGET="$target" \
  -DCMAKE_ASM_COMPILER_TARGET="$target" \
  -DCMAKE_AR="$prefix/bin/llvm-ar" \
  -DCMAKE_NM="$prefix/bin/llvm-nm" \
  -DCMAKE_RANLIB="$prefix/bin/llvm-ranlib" \
  -DCMAKE_C_FLAGS=-ffreestanding \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCOMPILER_RT_BAREMETAL_BUILD=ON \
  -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
  -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=ON \
  -DCOMPILER_RT_INSTALL_PATH="$resource"
ninja -C build-builtins -j"$jobs" install

# Builds select tools by the target prefix.
# llvm-readobj and llvm-symbolizer select readelf/addr2line behaviour by name.
for tool in clang ar ranlib nm objcopy strip objdump readelf size addr2line; do
  case "$tool" in
    clang) name=clang ;;
    readelf) name=llvm-readobj ;;
    addr2line) name=llvm-symbolizer ;;
    *) name=llvm-$tool ;;
  esac
  ln -sf "$name" "$prefix/bin/$target-$tool"
done
ln -sf ld.lld "$prefix/bin/$target-ld.lld"

mkdir -p "$prefix/share/pyxis-toolchain"
cp "$toolchain/README.md" "$source/llvm/LICENSE.TXT" \
  "$prefix/share/pyxis-toolchain/"
printf 'repository=%s\ncommit=%s\n' "$llvm_repository" \
  "$(git -C "$source" rev-parse HEAD)" > "$prefix/share/pyxis-toolchain/llvm-revision"
