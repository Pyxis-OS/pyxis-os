#!/bin/sh
set -eu

# An archive nested in some other checkout must not inherit that repository's ID.
revision=
root=$(git rev-parse --show-toplevel 2>/dev/null || :)
if [ "$root" = "$(pwd -P)" ]; then
  revision=$(git rev-parse --verify --short=12 HEAD 2>/dev/null || :)
fi
case "$revision" in
  ''|*[!0-9a-f]*) revision= ;;
esac
if [ "${#revision}" -gt 40 ]; then
  revision=
fi
printf '#define KERNEL_BUILD_REVISION "%s"\n' "$revision"
