#!/bin/sh
# Assemble only the target SDK and compiler provenance for app://sdk.
set -eu
staging=build/guest-sdk
sdk=build/sdk
port=build/ports/tcc/stage
rm -rf "$staging"
trap 'rm -rf "$staging"' EXIT
trap 'exit 1' HUP INT TERM
mkdir -p "$staging/usr/lib" "$staging/lib" "$staging/share/licenses"
cp -R "$sdk/sysroot/usr/include" "$staging/usr/"
for library in crt0.o libc.a libterm.a libpyxis.a libgcc.a; do
  cp "$sdk/sysroot/usr/lib/$library" "$staging/usr/lib/"
done
cp -R "$port/lib/tcc" "$staging/lib/"
cp -R "$sdk/share/licenses/." "$staging/share/licenses/"
cp -R "$port/share/licenses/tcc" "$staging/share/licenses/"
cp -R "$sdk/share/toolchain" "$port/share/tcc" "$staging/share/"
cat "$sdk/manifest.txt" > "$staging/manifest.txt"
{
  printf 'ports_revision=%s\n' "$(git -C ports rev-parse HEAD)"
  if [ -n "$(git -C ports status --porcelain)" ]; then
    printf 'ports_state=modified\n'
  else
    printf 'ports_state=clean\n'
  fi
} >> "$staging/manifest.txt"

# Retain timestamps on identical builds; replace the tree to remove stale files.
if ! diff -qr "$staging" build/userspace/sdk >/dev/null 2>&1; then
  rm -rf build/userspace/sdk
  mv "$staging" build/userspace/sdk
fi
