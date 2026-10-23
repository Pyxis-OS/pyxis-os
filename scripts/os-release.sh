#!/bin/sh
set -eu

# The release is the tag at HEAD, only for a build of this repository's own
# checkout, as for the build revision. Several tags, or one that is not an ABI
# name, name no release; the packer refuses to pack such a build.
revision=$(./scripts/kernel-build-revision.sh | sed -n 's/^#define KERNEL_BUILD_REVISION "\(.*\)"$/\1/p')
release=
floor=
if [ -n "$revision" ]; then
  # The build commit's time: no index signed before it is accepted on first
  # contact (docs/wip/pkg-upgrade.md, decision 71).
  floor=$(TZ=UTC0 git log -1 --format=%cd --date=format-local:%Y-%m-%dT%H:%M:%SZ HEAD)
  tags=$(git tag --points-at HEAD)
  if [ "$(printf '%s\n' "$tags" | grep -c .)" -eq 1 ] &&
     printf '%s\n' "$tags" | grep -qxE '[0-9a-z][0-9a-z.-]{0,63}'; then
    release=$tags
  fi
fi
"${LUA:-lua}" scripts/os-release.lua boot/os-release.lua "$revision" "$release" "$floor"
