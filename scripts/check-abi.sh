#!/bin/sh
set -eu

# include/abi/ at each release that boot/os-release.lua lists is a contract:
# lines may be added, not removed or changed (docs/interfaces/abi.md). The
# comparison is textual and includes comments; it does not see renumbering.
listed=$("${LUA:-lua}" -e 'for _, name in ipairs(dofile("boot/os-release.lua").abi) do print(name) end')
if [ -z "$listed" ]; then
  echo "check-abi: boot/os-release.lua lists no release; include/abi/ is not compared"
  exit 0
fi
status=0
for release in $listed; do
  if ! git rev-parse --verify --quiet "refs/tags/$release^{commit}" >/dev/null; then
    echo "check-abi: $release is listed but not tagged yet; nothing to compare"
    continue
  fi
  # A changed line counts as a removal and an addition; a rename as a removal.
  removed=$(git diff --no-renames --numstat "$release" -- include/abi | awk '$2 != 0 { print $3 }')
  if [ -n "$removed" ]; then
    git diff --no-renames --no-color "$release" -- $removed >&2
    echo "check-abi: the files above remove or change lines of $release's include/abi/. Restore them, or, for a deliberate break, remove $release from boot/os-release.lua in this change." >&2
    status=1
  else
    echo "check-abi: include/abi/ keeps every line of $release"
  fi
done
exit $status
