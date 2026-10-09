#!/bin/sh
# Fail when a pull request pins a dependency commit that does not contain that
# repository's current main, or that cannot be fetched (unpublished).
#
# usage: check-submodule-pins.sh BASE_COMMIT HEAD_COMMIT
#
# Only gitlinks the pull request changes since it left the base branch are
# checked. An unchanged pin cannot regress, and checking it would fail
# unrelated pull requests whenever a dependency's main moved ahead.
# Dependency repositories resolve beside this repository's origin, as the
# relative URLs in .gitmodules do, and are read anonymously.
set -eu

[ "$#" -eq 2 ] || { echo "usage: $0 BASE_COMMIT HEAD_COMMIT" >&2; exit 2; }
base=$1
head=$2
paths='userspace ports fs third_party/lwip'

fork=$(git merge-base "$base" "$head")
origin=$(git remote get-url origin)
origin=${origin%/}
origin=${origin%.git}
parent=${origin%/*}
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
git init -q --bare "$scratch"

# The gitlink commit at a path, or nothing if there is none.
pin_at() {
  git ls-tree "$1" "$2" | awk '$1 == "160000" { print $3 }'
}

failed=0
for path in $paths; do
  old=$(pin_at "$fork" "$path")
  new=$(pin_at "$head" "$path")
  if [ -z "$new" ] || [ "$old" = "$new" ]; then
    continue
  fi
  relative=$(git config --blob "$head:.gitmodules" --get "submodule.$path.url")
  url="$parent/${relative#../}"
  echo "$path: pin $old -> $new, checking against main of $url"

  if ! git -C "$scratch" fetch -q --no-tags --filter=blob:none "$url" \
      "+refs/heads/main:refs/dependency/main/$path" 2>/dev/null &&
     ! git -C "$scratch" fetch -q --no-tags "$url" \
      "+refs/heads/main:refs/dependency/main/$path"; then
    echo "error: $path: cannot fetch main of $url" >&2
    failed=1
    continue
  fi
  if ! git -C "$scratch" fetch -q --no-tags --filter=blob:none "$url" "$new" 2>/dev/null &&
     ! git -C "$scratch" fetch -q --no-tags "$url" "$new"; then
    echo "error: $path pins $new, which cannot be fetched from $url." \
      "Publish the dependency commit before pinning it." >&2
    failed=1
    continue
  fi
  main=$(git -C "$scratch" rev-parse "refs/dependency/main/$path")
  if ! git -C "$scratch" merge-base --is-ancestor "$main" "$new"; then
    echo "error: $path pins $new, which does not contain main ($main) of $url." \
      "Pinning it would roll back merged work. Rebase the dependency PR onto main and repin." >&2
    failed=1
  fi
done
exit "$failed"
