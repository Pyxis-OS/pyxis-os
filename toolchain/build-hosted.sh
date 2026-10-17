#!/bin/bash
# Opt-in native Clang/LLD payload build; independent of ordinary SDK/image builds.
set -euo pipefail
export LC_ALL=C

fail() {
  echo "$*" >&2
  exit 1
}

if [ "$#" -ne 3 ]; then
  fail 'Usage: toolchain/build-hosted.sh SDK OUTPUT WORKDIR (absolute paths)'
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
repository=https://git.internal/PyxisOS/pyxis-llvm.git
jobs=${JOBS:-16}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail 'JOBS must be a positive integer'

revision=$(sed -n 's/^commit=//p' "$sdk/share/toolchain/llvm-revision")
[[ "$revision" =~ ^[0-9a-f]{40}$ ]] || fail 'SDK does not record one exact LLVM commit'
[[ "$(sed -n 's/^repository=//p' "$sdk/share/toolchain/llvm-revision")" = "$repository" ]] || fail 'SDK LLVM source is not the Pyxis mirror'
cmp "$sdk/share/toolchain/llvm-revision" "$provenance/llvm-revision" || fail 'Installed compiler and SDK LLVM revisions differ'
[[ "$("$compiler" -dumpmachine)" = x86_64-unknown-pyxis ]] || fail 'Use the installed Pyxis compiler'
resource=$("$compiler" -print-resource-dir)
runtime=$("$compiler" -print-libgcc-file-name)
cmp "$runtime" "$sdk/sysroot/usr/lib/libclang_rt.builtins.a" || fail 'SDK compiler-rt differs from the installed compiler'

# Never inherit CMake state or target objects compiled against a previous ABI.
mkdir -p -- "$(dirname -- "$work")" "$output"
mkdir -- "$work" || fail 'WORKDIR must not exist; use a fresh directory'
python3 "$toolchain/stage-hosted.py" inventory "$sdk" > "$work/sdk-input.json"
if [ -n "${LLVM_SOURCE:-}" ]; then
  source=$(realpath -e "$LLVM_SOURCE")
else
  source=$work/llvm-project
  git init -q "$source"
  git -C "$source" fetch -q --depth 1 "$repository" "$revision"
  git -C "$source" checkout -q --detach FETCH_HEAD
fi
[[ "$(git -C "$source" rev-parse HEAD)" = "$revision" ]] || fail 'LLVM_SOURCE must match the SDK LLVM commit'
[[ -z "$(git -C "$source" status --porcelain)" ]] || fail 'LLVM_SOURCE must be clean'

# Both generators run on Linux and come from the selected fork, never the target.
cmake -G 'Unix Makefiles' -S "$source/llvm" -B "$work/generators" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="${HOSTCC:-cc}" -DCMAKE_CXX_COMPILER="${HOSTCXX:-c++}" \
  -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra' -DLLVM_TARGETS_TO_BUILD=X86 \
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF
cmake --build "$work/generators" --parallel "$jobs" --target llvm-tblgen clang-tblgen \
  clang-tidy-confusable-chars-gen

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
  -DCLANG_RESOURCE_DIR=clang_resources:// -DDEFAULT_SYSROOT=sysroot://
)

# Clang and LLD: workers off.
cmake -G 'Unix Makefiles' -S "$source/llvm" -B "$work/target" "${target_flags[@]}" \
  -DLLVM_ENABLE_PROJECTS='clang;lld' -DLLVM_ENABLE_THREADS=OFF \
  -DLLD_INCLUDE_TESTS=OFF -DLLD_BUILD_TOOLS=ON \
  -DCLANG_ENABLE_STATIC_ANALYZER=ON -DCLANG_ENABLE_ARCMT=ON
cmake --build "$work/target" --parallel "$jobs" --target clang lld clang-resource-headers

# clangd needs LLVM threads, so it gets its own tree and Clang/LLD stay as above.
# Clang-tidy checks, the static analyzer, ARCMT, remote index and XPC stay out.
cmake -G 'Unix Makefiles' -S "$source/llvm" -B "$work/clangd-target" "${target_flags[@]}" \
  -DCLANG_TIDY_CONFUSABLE_CHARS_GEN="$work/generators/bin/clang-tidy-confusable-chars-gen" \
  -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra' -DLLVM_ENABLE_THREADS=ON \
  -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_TIDY_ENABLE_STATIC_ANALYZER=OFF \
  -DCLANG_ENABLE_ARCMT=OFF -DCLANGD_TIDY_CHECKS=OFF -DCLANGD_ENABLE_REMOTE=OFF \
  -DCLANGD_BUILD_XPC=OFF -DCLANGD_MALLOC_TRIM=OFF
cmake --build "$work/clangd-target" --parallel "$jobs" --target clangd

python3 "$toolchain/stage-hosted.py" inventory "$sdk" > "$work/sdk-after.json"
cmp "$work/sdk-input.json" "$work/sdk-after.json" || fail 'SDK changed during the build; discard this WORKDIR'
[[ "$(git -C "$source" rev-parse HEAD)" = "$revision" && -z "$(git -C "$source" status --porcelain)" ]] || fail 'LLVM source changed during the build'
python3 "$toolchain/stage-hosted.py" stage "$sdk" "$resource" "$source" "$work" "$output" "$provenance" "$toolchain"
