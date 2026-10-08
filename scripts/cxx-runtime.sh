#!/bin/sh
# Build libunwind, libc++abi and libc++ for the SDK from the pyxis-llvm commit
# the installed toolchain records, then install them into build/sdk. Runs
# after the libc headers are exported; the archives need no libc to build.
set -eu
sdk=$(pwd)/build/sdk
compiler=${CROSS_COMPILE:-x86_64-unknown-pyxis-}clang
target=x86_64-unknown-pyxis

toolchain=$(dirname -- "$(command -v "$compiler")")
revision=$toolchain/../share/pyxis-toolchain/llvm-revision
repository=$(sed -n 's/^repository=//p' "$revision")
commit=$(sed -n 's/^commit=//p' "$revision")
if [ -z "$repository" ] || [ -z "$commit" ]; then
  echo "Missing LLVM revision in $revision: use the installed Pyxis toolchain." >&2
  exit 1
fi

# The runtime sources must match the compiler, so the checkout and build are
# keyed by its commit. Repeated SDK builds reuse them; a new toolchain commit
# or make clean fetches again.
work=$(pwd)/build/cxx-runtime
source=$work/source-$commit
build=$work/build-$commit
install=$work/install-$commit
mkdir -p "$work"
find "$work" -mindepth 1 -maxdepth 1 ! -name "*-$commit" -exec rm -rf {} +

if [ ! -f "$source/.pyxis-complete" ]; then
  rm -rf "$source"
  git init -q "$source"
  # Only the runtimes and the build files they use; tests and docs stay out.
  git -C "$source" sparse-checkout set --no-cone \
    /runtimes/ /cmake/ /llvm/cmake/ /libunwind/ /libcxxabi/ /libcxx/ /libc/ \
    '!/libcxx/test/' '!/libcxx/docs/' '!/libcxxabi/test/' '!/libunwind/test/' \
    '!/libc/test/' '!/libc/docs/'
  git -C "$source" fetch -q --depth 1 --filter=blob:none "$repository" "$commit"
  git -C "$source" checkout -q --detach FETCH_HEAD
  test "$(git -C "$source" rev-parse HEAD)" = "$commit"
  touch "$source/.pyxis-complete"
fi

# CMake keeps cached settings that a later configure no longer passes, so a
# changed script or SDK path starts from an empty build directory.
stamp="$(sha256sum "$0" | cut -d ' ' -f 1) $sdk"
if [ "$(cat "$build/.pyxis-configuration" 2>/dev/null)" != "$stamp" ]; then
  rm -rf "$build" "$install"
  mkdir -p "$build"
  printf '%s\n' "$stamp" > "$build/.pyxis-configuration"
fi

# libunwind's bare-metal mode finds exception tables through the SDK linker
# script's bounds instead of dl_iterate_phdr. libc++abi stays hosted: on x86-64
# its bare-metal mode only silences the uncaught-exception message.
# Configure checks only compile test programs, so the library checks for
# pthread, rt, dl and atomic would pass spuriously; *_HAS_*_LIB says they do
# not exist.
flags="--target=$target --sysroot=$sdk/sysroot -march=x86-64"
cmake -G 'Unix Makefiles' -S "$source/runtimes" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SYSTEM_NAME=Generic \
  -DCMAKE_INSTALL_PREFIX="$install" \
  -DCMAKE_C_COMPILER="$toolchain/$target-clang" \
  -DCMAKE_CXX_COMPILER="$toolchain/$target-clang++" \
  -DCMAKE_ASM_COMPILER="$toolchain/$target-clang" \
  -DCMAKE_C_COMPILER_TARGET=$target \
  -DCMAKE_CXX_COMPILER_TARGET=$target \
  -DCMAKE_ASM_COMPILER_TARGET=$target \
  -DCMAKE_AR="$toolchain/llvm-ar" \
  -DCMAKE_RANLIB="$toolchain/llvm-ranlib" \
  -DCMAKE_NM="$toolchain/llvm-nm" \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" -DCMAKE_ASM_FLAGS="$flags" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DLLVM_ENABLE_RUNTIMES='libunwind;libcxxabi;libcxx' \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_DOCS=OFF \
  -DLIBUNWIND_ENABLE_SHARED=OFF \
  -DLIBUNWIND_ENABLE_THREADS=OFF \
  -DLIBUNWIND_IS_BAREMETAL=ON \
  -DLIBUNWIND_USE_COMPILER_RT=ON \
  -DLIBUNWIND_ENABLE_ASSERTIONS=OFF \
  -DLIBUNWIND_HAS_PTHREAD_LIB=OFF -DLIBUNWIND_HAS_DL_LIB=OFF \
  -DLIBCXXABI_ENABLE_SHARED=OFF \
  -DLIBCXXABI_ENABLE_THREADS=OFF \
  -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
  -DLIBCXXABI_USE_COMPILER_RT=ON \
  -DLIBCXXABI_ENABLE_ASSERTIONS=OFF \
  -DLIBCXXABI_NON_DEMANGLING_TERMINATE=ON \
  -DLIBCXXABI_HAS_PTHREAD_LIB=OFF \
  -DLIBCXX_ENABLE_SHARED=OFF \
  -DLIBCXX_CXX_ABI=libcxxabi \
  -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=OFF \
  -DLIBCXX_USE_COMPILER_RT=ON \
  -DLIBCXX_ENABLE_THREADS=OFF \
  -DLIBCXX_ENABLE_LOCALIZATION=OFF \
  -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF \
  -DLIBCXX_ENABLE_FILESYSTEM=OFF \
  -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF \
  -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF \
  -DLIBCXX_ENABLE_EXPERIMENTAL_LIBRARY=OFF \
  -DLIBCXX_INSTALL_MODULES=OFF \
  -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
  -DLIBCXX_HAS_PTHREAD_LIB=OFF -DLIBCXX_HAS_RT_LIB=OFF -DLIBCXX_HAS_ATOMIC_LIB=OFF \
  >/dev/null
# Make runs this script with its job server; the generated Makefiles share it.
cmake --build "$build"
cmake --install "$build" >/dev/null

# Replace the SDK's C++ headers only when they changed, keeping timestamps so
# unchanged builds do not recompile consumers.
headers=$sdk/sysroot/usr/include/c++
if ! diff -qr "$install/include/c++" "$headers" >/dev/null 2>&1; then
  rm -rf "$headers"
  cp -pR "$install/include/c++" "$headers"
fi
mkdir -p "$sdk/sysroot/usr/lib"
for library in libc++ libc++abi libunwind; do
  install -C -m 644 "$install/lib/$library.a" "$sdk/sysroot/usr/lib/$library.a"
done
