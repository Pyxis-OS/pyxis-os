#!/bin/bash
# Opt-in target libraries and a host configuration tool; not a compiler rebuild.
set -euo pipefail
export LC_ALL=C

fail() { echo "$*" >&2; exit 1; }
[[ $# = 3 ]] || fail 'Usage: toolchain/build-llvm-development.sh SDK OUTPUT WORKDIR (absolute paths)'
for path in "$@"; do [[ "$path" = /* ]] || fail 'Use absolute paths'; done
sdk=$(realpath -e "$1")
output=$(realpath -m "$2")
work=$(realpath -m "$3")
toolchain=$(cd -- "$(dirname -- "$0")" && pwd)
revision=$(cat "$toolchain/llvm-development/revision")
repository=https://git.internal/PyxisOS/pyxis-llvm.git
compiler=$(realpath -e "$(command -v "${CROSS_COMPILE:-x86_64-unknown-pyxis-}clang")")
compiler_bin=$(dirname -- "$compiler")
host_cc=${HOSTCC:-cc}
host_cxx=${HOSTCXX:-c++}
jobs=${JOBS:-8}
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || fail 'JOBS must be positive'
[[ "$work" != / && "$work" != "$sdk" && "$work" != "$output" ]] || fail 'Use separate dedicated directories'
cmp "$sdk/share/toolchain/llvm-revision" "$compiler_bin/../share/pyxis-toolchain/llvm-revision" || fail 'SDK/compiler revisions differ'
[[ "$("$compiler" -dumpmachine)" = x86_64-unknown-pyxis ]] || fail 'Use the Pyxis compiler'
cmp "$("$compiler" -print-libgcc-file-name)" "$sdk/sysroot/usr/lib/libclang_rt.builtins.a" || fail 'SDK/compiler builtins differ'
mkdir -p "$work" "$output"
exec 9> "$work/build.lock"
flock -n 9 || fail 'Another development build owns WORKDIR'
exec 8> "$output/export.lock"
flock -n 8 || fail 'Another development build owns OUTPUT'
python3 "$toolchain/stage-hosted.py" inventory "$sdk" > "$work/sdk-input.json"
if [[ -n ${LLVM_SOURCE:-} ]]; then
  source=$(realpath -e "$LLVM_SOURCE")
else
  source=$work/llvm-project
  [[ -d "$source/.git" ]] || git init -q "$source"
  [[ -z "$(git -C "$source" status --porcelain)" ]] || fail 'LLVM source must be clean'
  git -C "$source" fetch -q --depth 1 "$repository" "$revision"
  git -C "$source" checkout -q --detach FETCH_HEAD
fi
[[ "$(git -C "$source" rev-parse HEAD)" = "$revision" && -z "$(git -C "$source" status --porcelain)" ]] || fail 'LLVM_SOURCE must be the clean development pin'

common=(
  -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_ASSERTIONS=OFF
  -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-unknown-pyxis
  -DLLVM_ENABLE_THREADS=ON -DLLVM_ENABLE_EH=OFF -DLLVM_ENABLE_RTTI=OFF
  -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF
  -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_Z3_SOLVER=OFF
  -DLLVM_ENABLE_CURL=OFF -DLLVM_ENABLE_HTTPLIB=OFF -DLLVM_ENABLE_ICONV=OFF
  -DLLVM_ENABLE_ICU=OFF -DLLVM_ENABLE_FFI=OFF -DLLVM_ENABLE_PLUGINS=OFF
  -DLLVM_ENABLE_BACKTRACES=OFF -DLLVM_ENABLE_CRASH_DUMPS=OFF
  -DLLVM_ENABLE_TELEMETRY=OFF -DLLVM_ENABLE_BINDINGS=OFF
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
  -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_DOCS=OFF
  -DLLVM_BUILD_LLVM_DYLIB=OFF -DLLVM_LINK_LLVM_DYLIB=OFF -DBUILD_SHARED_LIBS=OFF
  -DLLVM_BUILD_TOOLS=OFF -DLLVM_BUILD_UTILS=OFF
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
)
host=("${common[@]}" -DCMAKE_C_COMPILER="$host_cc" -DCMAKE_CXX_COMPILER="$host_cxx"
  -DLLVM_TOOL_LLVM_CONFIG_BUILD=ON)
target=("${common[@]}"
  -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
  -DCMAKE_C_COMPILER="$compiler_bin/clang" -DCMAKE_CXX_COMPILER="$compiler_bin/clang++"
  -DCMAKE_C_COMPILER_TARGET=x86_64-unknown-pyxis -DCMAKE_CXX_COMPILER_TARGET=x86_64-unknown-pyxis
  -DCMAKE_AR="$compiler_bin/llvm-ar" -DCMAKE_RANLIB="$compiler_bin/llvm-ranlib"
  -DCMAKE_SYSROOT="$sdk/sysroot" -DLLVM_HOST_TRIPLE=x86_64-unknown-pyxis
  -DLLVM_TABLEGEN="$work/generators/bin/llvm-tblgen"
  -DLLVM_CONFIG_PATH="$work/generators/bin/llvm-config"
  -DLLVM_TOOL_LLVM_CONFIG_BUILD=ON
  -DLLVM_TOOL_LTO_BUILD=OFF -DLLVM_TOOL_GOLD_BUILD=OFF -DLLVM_TOOL_REMARKS_SHLIB_BUILD=OFF
  -DLLVM_EXTERNAL_PROJECTS=pyxis-development
  -DLLVM_EXTERNAL_PYXIS_DEVELOPMENT_SOURCE_DIR="$toolchain/llvm-development")
{
  printf 'source=%s\nrevision=%s\n' "$source" "$revision"
  printf '%q ' "${host[@]}" "${target[@]}"
  printf '\n'
  for name in CFLAGS CXXFLAGS CPPFLAGS LDFLAGS CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH; do
    printf '%s=%q\n' "$name" "${!name-}"
  done
  for tool in "$host_cc" "$host_cxx" cmake ninja "$compiler_bin/clang" "$compiler_bin/clang++" "$compiler_bin/llvm-ar" "$compiler_bin/ld.lld"; do
    sha256sum "$(realpath -e "$(command -v "$tool")")"
  done
} > "$work/config.pending"
if [[ -f "$work/build-config.txt" ]]; then
  cmp "$work/config.pending" "$work/build-config.txt" || fail 'Build tools/options changed; use a fresh WORKDIR'
elif [[ -d "$work/generators" || -d "$work/target" ]]; then
  fail 'Unrecorded build tree; use a fresh WORKDIR'
fi
mv "$work/config.pending" "$work/build-config.txt"
if ! cmp -s "$work/sdk-input.json" "$work/sdk-previous.json"; then
  python3 "$toolchain/stage-hosted.py" invalidate-sdk "$sdk" "$work"
  rm -f "$work/target/CMakeCache.txt" "$work/target/bin/pyxis-llvm-object-check"
fi
cp "$work/sdk-input.json" "$work/sdk-previous.json"

cmake -G Ninja -S "$source/llvm" -B "$work/generators" "${host[@]}"
cmake --build "$work/generators" --parallel "$jobs" --target llvm-tblgen llvm-config
cmake -G Ninja -S "$source/llvm" -B "$work/target" "${target[@]}"
cmake --build "$work/target" --parallel "$jobs" --target pyxis-llvm-object-check

# Build the unmodified host tool against host Support, but report the target's
# actual flags and component graph. The host default triple above matches Pyxis.
python3 "$toolchain/llvm-development/configure-tool.py" "$work/target" "$work/config-tool"
read -r -a host_flags <<< "$("$work/generators/bin/llvm-config" --cxxflags)"
read -r -a host_libs <<< "$("$work/generators/bin/llvm-config" --link-static --ldflags --libs support targetparser --system-libs | tr '\n' ' ')"
"$host_cxx" -O2 -DNDEBUG "${host_flags[@]}" \
  -I "$work/config-tool" "$source/llvm/tools/llvm-config/llvm-config.cpp" \
  "${host_libs[@]}" -o "$work/llvm-config"

python3 "$toolchain/stage-hosted.py" inventory "$sdk" > "$work/sdk-after.json"
cmp "$work/sdk-input.json" "$work/sdk-after.json" || fail 'SDK changed during build'
[[ "$(git -C "$source" rev-parse HEAD)" = "$revision" && -z "$(git -C "$source" status --porcelain)" ]] || fail 'LLVM source changed during build'
python3 "$toolchain/stage-llvm-development.py" "$sdk" "$source" "$work" "$output"
