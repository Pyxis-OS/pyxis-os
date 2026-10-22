#!/bin/sh
set -eu

# include/abi/ at the last release tag is a contract while boot/os-release.lua
# still lists that release: lines may be added, not removed or changed
# (docs/interfaces/abi.md). The comparison is textual and includes comments.
tag=$(git describe --tags --abbrev=0 HEAD 2>/dev/null) || {
  echo "check-abi: no release tag is reachable from HEAD" >&2
  exit 1
}
listed=$("${LUA:-lua}" -e 'for _, name in ipairs(dofile("boot/os-release.lua").abi) do print(name) end')
if ! printf '%s\n' "$listed" | grep -qxF "$tag"; then
  echo "check-abi: boot/os-release.lua does not list $tag, so include/abi/ is not compared with it"
  exit 0
fi
# A changed line counts as a removal and an addition.
removed=$(git diff --numstat "$tag" -- include/abi | awk '$2 != 0 { print $3 }')
if [ -n "$removed" ]; then
  git diff --no-color "$tag" -- $removed >&2
  echo "check-abi: the files above remove or change lines of $tag's include/abi/. Restore them, or, for a deliberate break, remove $tag from boot/os-release.lua in this change." >&2
  exit 1
fi
echo "check-abi: include/abi/ keeps every line of $tag"
