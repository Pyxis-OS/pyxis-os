#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$script_dir/abi-contract.sh"
revision=$("$script_dir/kernel-build-revision.sh" | sed -n 's/^#define KERNEL_BUILD_REVISION "\(.*\)"$/\1/p')
if [ -z "$revision" ]; then
  echo "os-release: no source checkout identity; build from Git with full release history and tags" >&2
  exit 1
fi
abi_contract

# First contact accepts no signed index predating the build commit.
floor=$(TZ=UTC0 git log -1 --format=%cd --date=format-local:%Y-%m-%dT%H:%M:%SZ HEAD)
dirty=$(git status --porcelain --ignore-submodules=none)
release=
version=
upgrade_from=
provided=
if [ "$abi_distance" -eq 1 ] && [ -z "$dirty" ]; then
  release=$abi_release
  provided=${release%[a-z]}
else
  version=$abi_base+$revision
  upgrade_from=$abi_release
  if [ "$abi_compatible" = true ]; then
    provided=$abi_base
  fi
  if [ -n "$dirty" ]; then
    version=$version-dirty
  else
    provided="${provided:+$provided }$version"
  fi
fi
"${LUA:-lua}" "$script_dir/os-release.lua" "$revision" "$release" "$version" "$upgrade_from" "$floor" $provided
