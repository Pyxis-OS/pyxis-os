#!/bin/sh
set -eu
sdk=build/sdk
compiler=${CROSS_COMPILE:-x86_64-unknown-pyxis-}clang

case "${1:-}" in
  headers)
    # Build the public include tree afresh so removed headers cannot survive an
    # incremental export. Preserve the existing tree if its contents match.
    staging=build/sdk-headers
    rm -rf "$staging"
    trap 'rm -rf "$staging"' EXIT
    trap 'exit 1' HUP INT TERM
    mkdir -p "$staging/abi" "$staging/pxe" "$staging/remote" "$staging/terminal" "$sdk/sysroot/usr" "$sdk/share/pyxis"
    cp include/abi/*.h "$staging/abi/"
    cp include/pxe/*.h "$staging/pxe/"
    cp include/remote/*.h "$staging/remote/"
    cp include/terminal/*.h "$staging/terminal/"
    cp userspace/include/*.h "$staging/"
    cp -R userspace/libc/include/. "$staging/"
    cp -R fs/include/pyxis_fs "$staging/"
    # scripts/cxx-runtime.sh installs libc++'s headers later; keep them as they
    # are so that unchanged C headers do not replace the whole tree.
    if [ -d "$sdk/sysroot/usr/include/c++" ]; then
      cp -pR "$sdk/sysroot/usr/include/c++" "$staging/c++"
    fi
    if ! diff -qr "$staging" "$sdk/sysroot/usr/include" >/dev/null 2>&1; then
      rm -rf "$sdk/sysroot/usr/include"
      mv "$staging" "$sdk/sysroot/usr/include"
    fi
    install -C -m 644 lib/shebang.c "$sdk/share/pyxis/shebang.c"
    install -C -m 644 lib/key_layout.c "$sdk/share/pyxis/key_layout.c"
    install -C -m 644 lib/terminal_style.c "$sdk/share/pyxis/terminal_style.c"
    mkdir -p "$sdk/share/pyxis/machine_settings"
    install -C -m 644 userspace/include/machine_settings.h userspace/lib/machine_settings.c "$sdk/share/pyxis/machine_settings/"
    install -C -m 644 cmake/pyxis.cmake "$sdk/share/pyxis.cmake"
    install -C -D -m 644 cmake/Platform/Pyxis.cmake "$sdk/share/cmake/Platform/Pyxis.cmake"
    install -C -m 644 userspace/target.mk "$sdk/share/pyxis.mk"
    ;;
  complete)
    mkdir -p "$sdk/sysroot/usr/lib" "$sdk/share/licenses"
    # Compiler helpers are compiler-rt builtins from Clang's resource directory.
    # The installation records its provenance and license under
    # share/pyxis-toolchain.
    runtime=$("$compiler" -print-libgcc-file-name)
    provenance=$(dirname -- "$(command -v "$compiler")")/../share/pyxis-toolchain
    staging=build/sdk-toolchain
    rm -rf "$staging"
    trap 'rm -rf "$staging"' EXIT
    trap 'exit 1' HUP INT TERM
    mkdir -p "$staging"
    if [ ! -f "$runtime" ] || [ ! -f "$provenance/LICENSE.TXT" ]; then
      echo 'Missing compiler runtime or toolchain provenance: use the installed Pyxis toolchain.' >&2
      exit 1
    fi
    install -C -m 644 "$runtime" "$sdk/sysroot/usr/lib/libclang_rt.builtins.a"
    for file in LICENSE.TXT llvm-revision README.md; do
      cp "$provenance/$file" "$staging/"
    done
    if ! diff -qr "$staging" "$sdk/share/toolchain" >/dev/null 2>&1; then
      rm -rf "$sdk/share/toolchain"
      mv "$staging" "$sdk/share/toolchain"
    fi
    for library in libc libpyxis libterm; do
      install -C -m 644 "build/runtime/$library.a" "$sdk/sysroot/usr/lib/$library.a"
    done
    install -C -m 644 build/runtime/libc/start.o "$sdk/sysroot/usr/lib/crt0.o"
    install -C -m 644 build/npfs-sdk/libnpfs-format.a "$sdk/sysroot/usr/lib/libnpfs-format.a"
    install -C -m 644 userspace/linker.ld "$sdk/sysroot/usr/lib/pyxis.ld"
    # tlsf.h carries the complete upstream license; do not export its API.
    install -C -m 644 userspace/third_party/tlsf/tlsf.h "$sdk/share/licenses/tlsf.h"
    install -C -m 644 userspace/third_party/tlsf/UPSTREAM.md "$sdk/share/licenses/tlsf-upstream.md"
    install -C -m 644 userspace/third_party/musl/COPYRIGHT "$sdk/share/licenses/musl-COPYRIGHT"
    install -C -m 644 userspace/third_party/musl/TRE-COPYRIGHT "$sdk/share/licenses/musl-TRE-COPYRIGHT"
    install -C -m 644 userspace/third_party/musl/UPSTREAM.md "$sdk/share/licenses/musl-upstream.md"
    mkdir -p "$sdk/share/licenses/npfs"
    install -C -m 644 fs/LICENSE fs/LICENSING.md "$sdk/share/licenses/npfs/"
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
      printf 'fs_revision=%s\n' "$(git -C fs rev-parse HEAD)"
      if [ -n "$(git -C fs status --porcelain)" ]; then
        printf 'fs_state=modified\n'
      else
        printf 'fs_state=clean\n'
      fi
      printf 'compiler_target=%s\n' "$("$compiler" -dumpmachine)"
      printf 'compiler_version=%s\n' "$("$compiler" -dumpversion)"
      printf 'compiler_rt_sha256=%s\n' "$(sha256sum "$runtime" | cut -d ' ' -f 1)"
      "$("$compiler" -print-prog-name=ld.lld)" --version | sed -n '1p'
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
