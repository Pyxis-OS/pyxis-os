#!/bin/sh
# Build the pinned native Go cross-toolchain separately from ordinary images.
set -eu
if [ "$#" -ne 2 ]; then
  echo 'Usage: toolchain/build-go.sh PREFIX WORKDIR (absolute, fresh paths)' >&2
  exit 1
fi
prefix=$1
work=$2
case "$prefix:$work" in
  /*:/*) ;;
  *) echo 'PREFIX and WORKDIR must be absolute paths' >&2; exit 1 ;;
esac
[ ! -e "$prefix" ] || { echo 'PREFIX must not already exist' >&2; exit 1; }
mkdir -p "$(dirname -- "$prefix")"
go_repository=https://git.internal/PyxisOS/pyxis-go.git
# The full object name is deliberate: builds never follow the release branch.
go_commit=7d9387971271c0b6481b14cfcabd4dae84af2e48
mkdir -p "$work"
source=$work/source
mkdir "$source"
git -C "$source" init -q
git -C "$source" fetch -q --depth 1 "$go_repository" "$go_commit"
git -C "$source" checkout -q --detach FETCH_HEAD
test "$(git -C "$source" rev-parse HEAD)" = "$go_commit"
(
  cd "$source/src"
  GOTOOLCHAIN=local GOPROXY=off GOSUMDB=off CGO_ENABLED=0 \
    GOOS=linux GOARCH=amd64 GOAMD64=v1 ./make.bash
)
# Keep a complete GOROOT: cross-package builds use its sources and host tools.
# mkdir refuses an existing prefix rather than mixing compiler revisions.
mkdir "$prefix"
for entry in bin pkg src lib api VERSION LICENSE PATENTS; do
  cp -R "$source/$entry" "$prefix/"
done
mkdir -p "$prefix/share/pyxis-go"
cp "$source/LICENSE" "$source/PYXIS.md" "$prefix/share/pyxis-go/"
printf 'repository=%s\ncommit=%s\n' "$go_repository" "$go_commit" \
  > "$prefix/share/pyxis-go/revision"
