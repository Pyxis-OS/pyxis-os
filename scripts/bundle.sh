#!/bin/bash
# Four explicit build products. No fetching, cache selection or package solver.
set -euo pipefail
export LC_ALL=C

action=${1:?Usage: scripts/bundle.sh record|pack|verify COMPONENT}
component=${2:?Missing component}
case "$component" in
  kernel) payload=(build/caelum.elf); repository=. ;;
  sdk) payload=(build/sdk); repository=. ;;
  userspace) payload=(build/userspace-root); repository=userspace ;;
  ports) payload=(build/ports-root build/ports-dev); repository=ports ;;
  *) echo "Unknown component: $component" >&2; exit 1 ;;
esac
info=build/bundle-info/$component
mkdir -p build/bundle-info

checksums() {
  local path
  for path in "$@"; do test -e "$path" || return 1; done
  if [ -n "$(find "$@" ! -type f ! -type d -print -quit)" ]; then
    echo "Bundle payload must contain only regular files and directories: $1" >&2
    return 1
  fi
  printf 'directories_sha256=%s\n' "$(find "$@" -type d -print0 | sort -z | sha256sum | cut -d ' ' -f 1)"
  find "$@" -type f -print0 | sort -z | xargs -0 -r sha256sum
}

sdk_id() {
  checksums build/sdk | sha256sum | cut -d ' ' -f 1
}

interface_id() {
  (cd "$1" && find abi pxe -type f -print0 | sort -z | xargs -0 sha256sum) |
    sha256sum | cut -d ' ' -f 1
}

source_info() {
  local prefix=$1 directory=$2
  printf '%s_revision=%s\n' "$prefix" "$(git -C "$directory" rev-parse HEAD)"
  if [ -n "$(git -C "$directory" status --porcelain)" ]; then
    printf '%s_state=modified\n' "$prefix"
  else
    printf '%s_state=clean\n' "$prefix"
  fi
  # Record local patches and untracked content as well as the committed revision.
  local changes
  changes=$({
    git -C "$directory" diff --binary HEAD
    (cd "$directory" && git ls-files --others --exclude-standard -z |
      xargs -0 -r sha256sum)
  } | sha256sum | cut -d ' ' -f 1)
  printf '%s_changes_sha256=%s\n' "$prefix" "$changes"
}

case "$action" in
  record)
    checksums "${payload[@]}" > "$info.sha256.tmp"
    {
      printf 'component=%s\n' "$component"
      source_info "$component" "$repository"
      printf 'builder_image=%s\n' "${BUILDER_IMAGE:-local}"
      case "$component" in
        kernel)
          source_info lwip third_party/lwip
          printf 'abi_sha256=%s\n' "$(interface_id include)"
          printf 'log_level=%s\n' "${LOG_LEVEL:-info}"
          printf 'cppflags=%s\ncflags=%s\nldflags=%s\n' "${CPPFLAGS:-}" "${CFLAGS:-}" "${LDFLAGS:-}"
          "${CC:-${CROSS_COMPILE:-x86_64-unknown-pyxis-}gcc}" --version | head -n 1
          ;;
        sdk)
          source_info userland userspace
          printf 'abi_sha256=%s\n' "$(interface_id build/sdk/sysroot/usr/include)"
          ;;
        userspace|ports)
          printf 'sdk_sha256=%s\n' "$(sdk_id)"
          "${CC:-${CROSS_COMPILE:-x86_64-unknown-pyxis-}gcc}" --version | head -n 1
          ;;
      esac
      if [ "$component" = userspace ]; then
        printf 'lua_sha256=%s\n' "$(checksums build/ports-dev/lua | sha256sum | cut -d ' ' -f 1)"
      fi
    } > "$info.txt.tmp"
    for extension in txt sha256; do
      cmp -s "$info.$extension.tmp" "$info.$extension" || mv "$info.$extension.tmp" "$info.$extension"
      rm -f "$info.$extension.tmp"
    done
    ;;
  pack)
    "$0" verify "$component"
    mkdir -p build/bundles
    tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
      -cf "build/bundles/$component.tar.tmp" "${payload[@]}" "$info.txt" "$info.sha256"
    cmp -s "build/bundles/$component.tar.tmp" "build/bundles/$component.tar" ||
      mv "build/bundles/$component.tar.tmp" "build/bundles/$component.tar"
    rm -f "build/bundles/$component.tar.tmp"
    ;;
  verify)
    test -f "$info.txt" && test -f "$info.sha256" || {
      echo "Missing $component bundle metadata; extract its bundle at the repository root." >&2
      exit 1
    }
    checksums "${payload[@]}" > "$info.check"
    if ! cmp -s "$info.check" "$info.sha256"; then
      rm -f "$info.check"
      echo "$component payload differs from its recorded bundle (including added/removed files)." >&2
      exit 1
    fi
    rm -f "$info.check"
    if [ "$component" = userspace ] || [ "$component" = ports ]; then
      expected=$(sed -n 's/^sdk_sha256=//p' "$info.txt")
      test "$expected" = "$(sdk_id)" || {
        echo "$component bundle requires a different SDK; select matching bundles or rebuild it." >&2
        exit 1
      }
    fi
    if [ "$component" = userspace ]; then
      expected=$(sed -n 's/^lua_sha256=//p' "$info.txt")
      test "$expected" = "$(checksums build/ports-dev/lua | sha256sum | cut -d ' ' -f 1)" || {
        echo 'Userland bundle requires different Lua development files; select matching bundles or rebuild it.' >&2
        exit 1
      }
    fi
    ;;
  *) echo "Unknown bundle action: $action" >&2; exit 1 ;;
esac
