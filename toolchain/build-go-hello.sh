#!/bin/sh
set -eu
if [ "$#" -ne 2 ]; then
  echo 'Usage: toolchain/build-go-hello.sh GO_PREFIX OUTPUT_BUNDLE (absolute paths)' >&2
  exit 1
fi
prefix=$1
output=$2
case "$prefix:$output" in
  /*:/*) ;;
  *) echo 'GO_PREFIX and OUTPUT_BUNDLE must be absolute paths' >&2; exit 1 ;;
esac
toolchain=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
test -f "$prefix/share/pyxis-go/revision" || {
  echo 'Missing pinned Go toolchain provenance: use toolchain/build-go.sh.' >&2
  exit 1
}
# This is an independent bundle build, never a libc/LLVM link.
mkdir -p "$output/app/bin" "$output/app/share/licenses/go"
GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off \
  GOROOT="$prefix" GOOS=pyxis GOARCH=amd64 GOAMD64=v1 CGO_ENABLED=0 \
  "$prefix/bin/go" build -trimpath -o "$output/app/bin/gohello.pxe" "$toolchain/go-hello/main.go"
cp "$toolchain/go-hello/manifest.json" "$output/manifest.json"
cp "$prefix/share/pyxis-go/LICENSE" "$prefix/share/pyxis-go/revision" \
  "$output/app/share/licenses/go/"

mkdir -p "$output/app/share/licenses/pyxis-go-hello"
cp "$toolchain/../LICENSE" "$output/app/share/licenses/pyxis-go-hello/LICENSE"
