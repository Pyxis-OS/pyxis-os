#!/bin/bash
# Opt-in native Clang/LLD payload build; independent of ordinary SDK/image builds.
set -euo pipefail
export LC_ALL=C

fail() {
  echo "$*" >&2
  exit 1
}

clean=0
if [ "${1:-}" = --clean ]; then
  clean=1
  shift
fi
if [ "$#" -ne 3 ]; then
  fail 'Usage: toolchain/build-hosted.sh [--clean] SDK OUTPUT WORKDIR (absolute paths)'
fi
for path in "$@"; do
  [[ "$path" = /* ]] || fail 'SDK, OUTPUT and WORKDIR must be absolute paths'
done
sdk=$(realpath -e "$1")
output=$(realpath -m "$2")
work=$(realpath -m "$3")
toolchain=$(cd -- "$(dirname -- "$0")" && pwd)
compiler=$(command -v "${CROSS_COMPILE:-x86_64-unknown-pyxis-}clang")
compiler_bin=$(dirname -- "$compiler")
provenance=$compiler_bin/../share/pyxis-toolchain
# Hosted-only changes need not change the installed cross compiler or runtimes.
repository=https://git.internal/PyxisOS/pyxis-llvm.git
revision=8bdf1360b8c47640c92eeeb515ee4a08814987e6
host_cc=${HOSTCC:-cc}
host_cxx=${HOSTCXX:-c++}
jobs=${JOBS:-16}
command -v ninja >/dev/null || fail 'Hosted builds require Ninja (ninja-build)'
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail 'JOBS must be a positive integer'

compiler_revision=$(sed -n 's/^commit=//p' "$sdk/share/toolchain/llvm-revision")
[[ "$compiler_revision" =~ ^[0-9a-f]{40}$ ]] || fail 'SDK does not record one exact LLVM commit'
[[ "$(sed -n 's/^repository=//p' "$sdk/share/toolchain/llvm-revision")" = "$repository" ]] || fail 'SDK LLVM source is not the Pyxis mirror'
cmp "$sdk/share/toolchain/llvm-revision" "$provenance/llvm-revision" || fail 'Installed compiler and SDK LLVM revisions differ'
[[ "$("$compiler" -dumpmachine)" = x86_64-unknown-pyxis ]] || fail 'Use the installed Pyxis compiler'
resource=$("$compiler" -print-resource-dir)
runtime=$("$compiler" -print-libgcc-file-name)
cmp "$runtime" "$sdk/sysroot/usr/lib/libclang_rt.builtins.a" || fail 'SDK compiler-rt differs from the installed compiler'

[[ "$work" != / && "$work" != "$sdk" ]] || fail 'WORKDIR must be separate from the SDK and filesystem root'
mkdir -p -- "$work" "$output"
exec 9> "$work/build.lock"
flock -n 9 || fail 'Another hosted build owns WORKDIR'
python3 "$toolchain/stage-hosted.py" inventory "$sdk" > "$work/sdk-input.json"
if [ -n "${LLVM_SOURCE:-}" ]; then
  source=$(realpath -e "$LLVM_SOURCE")
else
  source=$work/llvm-project
  if [ ! -d "$source/.git" ]; then
    git init -q "$source"
  fi
  [[ -z "$(git -C "$source" status --porcelain)" ]] || fail 'Persistent LLVM source must be clean'
  git -C "$source" fetch -q --depth 1 "$repository" "$revision"
  git -C "$source" checkout -q --detach FETCH_HEAD
fi
[[ "$(git -C "$source" rev-parse HEAD)" = "$revision" ]] || fail 'LLVM_SOURCE must match the hosted LLVM commit'
[[ -z "$(git -C "$source" status --porcelain)" ]] || fail 'LLVM_SOURCE must be clean'

# Both generators run on Linux and come from the selected fork, never the target.
generator_flags=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_C_COMPILER="$host_cc" -DCMAKE_CXX_COMPILER="$host_cxx"
  -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra' -DLLVM_TARGETS_TO_BUILD=X86
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF
)

# Everything both native trees share. Only the project list, the thread setting
# and the clangd/LLD options below differ.
target_flags=(
  -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
  -DCMAKE_C_COMPILER="$compiler_bin/clang" -DCMAKE_CXX_COMPILER="$compiler_bin/clang++"
  -DCMAKE_C_COMPILER_TARGET=x86_64-unknown-pyxis -DCMAKE_CXX_COMPILER_TARGET=x86_64-unknown-pyxis
  -DCMAKE_AR="$compiler_bin/llvm-ar" -DCMAKE_RANLIB="$compiler_bin/llvm-ranlib"
  -DCMAKE_SYSROOT="$sdk/sysroot" -DLLVM_HOST_TRIPLE=x86_64-unknown-pyxis
  -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-unknown-pyxis
  -DLLVM_TARGETS_TO_BUILD=X86
  -DLLVM_TABLEGEN="$work/generators/bin/llvm-tblgen"
  -DCLANG_TABLEGEN="$work/generators/bin/clang-tblgen"
  -DLLVM_ENABLE_EH=OFF -DLLVM_ENABLE_RTTI=OFF
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF
  -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_Z3_SOLVER=OFF
  -DLLVM_ENABLE_CURL=OFF -DLLVM_ENABLE_HTTPLIB=OFF -DLLVM_ENABLE_ICONV=OFF
  -DLLVM_ENABLE_ICU=OFF -DLLVM_ENABLE_FFI=OFF -DLLVM_ENABLE_PLUGINS=OFF
  -DLLVM_ENABLE_BACKTRACES=OFF -DLLVM_ENABLE_CRASH_DUMPS=OFF
  -DLLVM_ENABLE_TELEMETRY=OFF -DLLVM_ENABLE_BINDINGS=OFF
  -DLLVM_BUILD_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF
  -DLLVM_BUILD_EXAMPLES=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
  -DLLVM_BUILD_BENCHMARKS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF
  -DLLVM_BUILD_DOCS=OFF -DLLVM_INCLUDE_DOCS=OFF
  -DLLVM_BUILD_TOOLS=OFF -DLLVM_BUILD_UTILS=OFF
  -DCLANG_BUILD_TOOLS=ON
  -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON -DLLVM_TOOL_LTO_BUILD=OFF
  -DLLVM_TOOL_GOLD_BUILD=OFF -DLLVM_TOOL_REMARKS_SHLIB_BUILD=OFF
  -DCLANG_TOOL_LIBCLANG_BUILD=OFF -DLLVM_ENABLE_MODULES=OFF
  -DCLANG_RESOURCE_DIR=clang_resources:// -DDEFAULT_SYSROOT=sdk://
)

clang_flags=(
  -DLLVM_ENABLE_PROJECTS='clang;lld' -DLLVM_ENABLE_THREADS=OFF
  -DLLD_INCLUDE_TESTS=OFF -DLLD_BUILD_TOOLS=ON
  -DCLANG_ENABLE_STATIC_ANALYZER=ON -DCLANG_ENABLE_ARCMT=ON
)
clangd_flags=(
  -DCLANG_TIDY_CONFUSABLE_CHARS_GEN="$work/generators/bin/clang-tidy-confusable-chars-gen"
  -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra' -DLLVM_ENABLE_THREADS=ON
  -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_TIDY_ENABLE_STATIC_ANALYZER=OFF
  -DCLANG_ENABLE_ARCMT=OFF -DCLANGD_TIDY_CHECKS=OFF -DCLANGD_ENABLE_REMOTE=OFF
  -DCLANGD_BUILD_XPC=OFF -DCLANGD_MALLOC_TRIM=OFF
)

# Source revisions and SDK contents are ordinary dependency inputs, not reasons
# to discard objects. Compiler identities, build options and paths are different.
configuration=$work/build-config.txt
candidate=$work/build-config.pending
{
  printf 'generator=Ninja\nsource=%s\n' "$source"
  cat "$provenance/llvm-revision"
  printf '%q ' "${generator_flags[@]}" "${target_flags[@]}" "${clang_flags[@]}" "${clangd_flags[@]}"
  printf '\n'
  for name in CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CPATH C_INCLUDE_PATH \
      CPLUS_INCLUDE_PATH LIBRARY_PATH COMPILER_PATH GCC_EXEC_PREFIX; do
    printf '%s=%q\n' "$name" "${!name-}"
  done
  for tool in "$host_cc" "$host_cxx" cmake ninja "$compiler_bin/clang" \
      "$compiler_bin/clang++" "$compiler_bin/ld.lld" "$compiler_bin/llvm-ar" \
      "$compiler_bin/llvm-ranlib"; do
    executable=$(realpath -e "$(command -v "$tool")")
    sha256sum "$executable"
  done
} > "$candidate"
if [ "$clean" = 1 ]; then
  echo 'Hosted build: clean rebuild explicitly requested.'
elif [ -f "$configuration" ]; then
  if ! cmp -s "$configuration" "$candidate"; then
    echo 'Hosted build: compiler/tools, paths, flags or environment changed; clean rebuild.'
    diff -u "$configuration" "$candidate" >&2 || true
    clean=1
  fi
elif [ -d "$work/generators" ] || [ -d "$work/target" ] || [ -d "$work/clangd-target" ]; then
  fail 'Unrecorded build configuration: use --clean once or a new WORKDIR'
else
  echo 'Hosted build: initializing persistent Ninja trees.'
fi
if [ "$clean" = 0 ] && [ -f "$configuration" ]; then
  echo 'Hosted build: reusing persistent Ninja trees.'
fi
if [ "$clean" = 1 ]; then
  rm -rf -- "$work/generators" "$work/target" "$work/clangd-target"
fi
mv -- "$candidate" "$configuration"
if ! cmp -s "$work/sdk-input.json" "$work/sdk-previous.json"; then
  echo 'Hosted build: SDK changed; refresh target checks and affected objects, relink tools.'
  python3 "$toolchain/stage-hosted.py" invalidate-sdk "$sdk" "$work"
  rm -f -- "$work/target/CMakeCache.txt" "$work/clangd-target/CMakeCache.txt"
  # Link scripts and other SDK inputs may not be represented in Ninja's deps.
  for image in target/bin/clang target/bin/lld clangd-target/bin/clangd; do
    if [ -e "$work/$image" ]; then
      binary=$(realpath -e "$work/$image")
      [[ "$binary" = "$work/"* ]] || fail 'Build image escapes WORKDIR'
      rm -f -- "$binary"
    fi
  done
fi
# Record the attempted SDK before compiling, so an interrupted B build followed
# by a return to SDK A still invalidates B's objects and configuration caches.
cp -- "$work/sdk-input.json" "$work/sdk-previous.pending"
mv -- "$work/sdk-previous.pending" "$work/sdk-previous.json"

cmake -G Ninja -S "$source/llvm" -B "$work/generators" "${generator_flags[@]}"
cmake --build "$work/generators" --parallel "$jobs" --target llvm-tblgen clang-tblgen \
  clang-tidy-confusable-chars-gen

# Clang and LLD: workers off.
cmake -G Ninja -S "$source/llvm" -B "$work/target" "${target_flags[@]}" "${clang_flags[@]}"
cmake --build "$work/target" --parallel "$jobs" --target clang lld clang-resource-headers

# clangd needs LLVM threads, so it gets its own tree and Clang/LLD stay as above.
cmake -G Ninja -S "$source/llvm" -B "$work/clangd-target" "${target_flags[@]}" "${clangd_flags[@]}"
cmake --build "$work/clangd-target" --parallel "$jobs" --target clangd

python3 "$toolchain/stage-hosted.py" inventory "$sdk" > "$work/sdk-after.json"
cmp "$work/sdk-input.json" "$work/sdk-after.json" || fail 'SDK changed during the build; rerun with stable inputs'
[[ "$(git -C "$source" rev-parse HEAD)" = "$revision" && -z "$(git -C "$source" status --porcelain)" ]] || fail 'LLVM source changed during the build'
python3 "$toolchain/stage-hosted.py" stage "$sdk" "$resource" "$source" "$work" "$output" "$provenance" "$toolchain"
