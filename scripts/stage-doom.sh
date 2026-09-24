#!/bin/sh
set -eu

# Recreate these disposable directories so removing a local override cannot
# leave retail data or demos in a later image/CI artifact.
rm -rf build/userspace/share/doom
rm -rf build/userspace/share/licenses/doom-shareware
if [ -z "${DOOM_WAD:-}" ]; then
  DOOM_WAD=third_party/doom-shareware/doom1.wad
  install -D -m 644 third_party/doom-shareware/LICENSE \
    build/userspace/share/licenses/doom-shareware/LICENSE
  install -m 644 third_party/doom-shareware/UPSTREAM.md \
    build/userspace/share/licenses/doom-shareware/UPSTREAM.md
  printf '%s\n' share/licenses/doom-shareware \
    share/licenses/doom-shareware/LICENSE share/licenses/doom-shareware/UPSTREAM.md
fi
if [ ! -f "$DOOM_WAD" ]; then
  printf 'Doom WAD is not a regular file: %s\n' "$DOOM_WAD" >&2
  exit 1
fi
install -D -m 644 -- "$DOOM_WAD" build/userspace/share/doom/DOOM.WAD
printf '%s\n' share/doom share/doom/DOOM.WAD

if [ -n "${DOOM_DEMOS:-}" ]; then
  if [ ! -d "$DOOM_DEMOS" ]; then
    printf 'Doom demo directory does not exist: %s\n' "$DOOM_DEMOS" >&2
    exit 1
  fi
  for demo in e1m1sec.lmp e1m2sec.lmp; do
    install -m 644 -- "$DOOM_DEMOS/$demo" "build/userspace/share/doom/$demo"
    printf 'share/doom/%s\n' "$demo"
  done
fi
