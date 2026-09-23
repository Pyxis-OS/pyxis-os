#!/bin/sh
set -eu
sdk=build/sdk

case "${1:-}" in
  headers)
    # Build the public include tree afresh so removed headers cannot survive an
    # incremental export. Preserve the existing tree if its contents match.
    staging=build/sdk-headers
    rm -rf "$staging"
    trap 'rm -rf "$staging"' EXIT
    trap 'exit 1' HUP INT TERM
    mkdir -p "$staging/abi" "$staging/pxe" "$sdk/sysroot/usr" "$sdk/share/pyxis"
    cp include/abi/*.h "$staging/abi/"
    cp include/pxe/*.h "$staging/pxe/"
    cp userspace/include/*.h userspace/libc/include/*.h "$staging/"
    if ! diff -qr "$staging" "$sdk/sysroot/usr/include" >/dev/null 2>&1; then
      rm -rf "$sdk/sysroot/usr/include"
      mv "$staging" "$sdk/sysroot/usr/include"
    fi
    install -C -m 644 lib/shebang.c "$sdk/share/pyxis/shebang.c"
    install -C -m 644 userspace/target.mk "$sdk/share/pyxis.mk"
    ;;
  complete)
    mkdir -p "$sdk/sysroot/usr/lib" "$sdk/bin" "$sdk/share/licenses"
    compiler=${CROSS_COMPILE:-x86_64-unknown-pyxis-}gcc
    libgcc=$("$compiler" -print-libgcc-file-name)
    # The project toolchain installs provenance beside its lib/gcc hierarchy.
    toolchain=$(dirname -- "$libgcc")/../../../../share/pyxis-toolchain
    if [ ! -f "$libgcc" ] || [ ! -f "$toolchain/COPYING.RUNTIME" ]; then
      echo 'Missing target libgcc or toolchain provenance: use the installed Pyxis toolchain.' >&2
      exit 1
    fi
    install -C -m 644 "$libgcc" "$sdk/sysroot/usr/lib/libgcc.a"
    staging=build/sdk-toolchain
    rm -rf "$staging"
    trap 'rm -rf "$staging"' EXIT
    trap 'exit 1' HUP INT TERM
    mkdir -p "$staging"
    cp "$toolchain/COPYING3" "$toolchain/COPYING.RUNTIME" \
      "$toolchain/SHA256SUMS" "$toolchain/README.md" "$toolchain/"*.patch "$staging/"
    if ! diff -qr "$staging" "$sdk/share/toolchain" >/dev/null 2>&1; then
      rm -rf "$sdk/share/toolchain"
      mv "$staging" "$sdk/share/toolchain"
    fi
    for library in libc libpyxis libterm; do
      install -C -m 644 "build/runtime/$library.a" "$sdk/sysroot/usr/lib/$library.a"
    done
    install -C -m 644 build/runtime/libc/start.o "$sdk/sysroot/usr/lib/crt0.o"
    install -C -m 644 userspace/linker.ld "$sdk/sysroot/usr/lib/pyxis.ld"
    install -C -m 755 build/tools/elf2pxe "$sdk/bin/elf2pxe"
    # tlsf.h carries the complete upstream license; do not export its API.
    install -C -m 644 userspace/third_party/tlsf/tlsf.h "$sdk/share/licenses/tlsf.h"
    install -C -m 644 userspace/third_party/tlsf/UPSTREAM.md "$sdk/share/licenses/tlsf-upstream.md"
    install -C -m 644 userspace/third_party/musl/COPYRIGHT "$sdk/share/licenses/musl-COPYRIGHT"
    install -C -m 644 userspace/third_party/musl/UPSTREAM.md "$sdk/share/licenses/musl-upstream.md"
    {
      printf 'pyxis_revision=%s\n' "$(git rev-parse HEAD)"
      if [ -n "$(git status --porcelain)" ]; then
        printf 'source_state=modified\n'
      else
        printf 'source_state=clean\n'
      fi
      printf 'userland_revision=%s\n' "$(git -C userspace rev-parse HEAD)"
      if [ -n "$(git -C userspace status --porcelain)" ]; then
        printf 'userland_state=modified\n'
      else
        printf 'userland_state=clean\n'
      fi
      printf 'compiler_target=%s\n' "$("${CROSS_COMPILE:-x86_64-unknown-pyxis-}gcc" -dumpmachine)"
      printf 'compiler_version=%s\n' "$("${CROSS_COMPILE:-x86_64-unknown-pyxis-}gcc" -dumpfullversion)"
      printf 'libgcc_sha256=%s\n' "$(sha256sum "$libgcc" | cut -d ' ' -f 1)"
      "${CROSS_COMPILE:-x86_64-unknown-pyxis-}ld" --version | sed -n '1p'
      printf 'host=%s %s\n' "$(uname -s)" "$(uname -m)"
      "${HOSTCC:-cc}" --version | sed -n '1p'
    } > "$sdk/manifest.txt.tmp"
    cmp -s "$sdk/manifest.txt.tmp" "$sdk/manifest.txt" || mv "$sdk/manifest.txt.tmp" "$sdk/manifest.txt"
    rm -f "$sdk/manifest.txt.tmp"
    ;;
  *)
    echo 'Usage: scripts/export-sdk.sh headers|complete (from the Pyxis root)' >&2
    exit 1
    ;;
esac
