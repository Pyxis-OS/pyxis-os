#!/bin/sh
set -eu

# The release is the tag at HEAD, only for a build of this repository's own
# checkout, as for the build revision.
revision=$(./scripts/kernel-build-revision.sh | sed -n 's/^#define KERNEL_BUILD_REVISION "\(.*\)"$/\1/p')
release=
if [ -n "$revision" ]; then
  tags=$(git tag --points-at HEAD)
  if [ "$(printf '%s\n' "$tags" | grep -c .)" -gt 1 ]; then
    echo "os-release: HEAD has several tags: $(echo $tags)" >&2
    exit 1
  fi
  release=$tags
fi
"${LUA:-lua}" scripts/os-release.lua boot/os-release.lua "$revision" "$release"
