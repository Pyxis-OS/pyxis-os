#!/bin/sh
set -eu

# Assets stay local. Recreate this disposable directory so removing an override
# cannot leave a previous WAD or demo in a later image/CI artifact.
rm -rf build/userspace/share/doom
if [ -z "${DOOM_WAD:-}" ]; then
  if [ -n "${DOOM_DEMOS:-}" ]; then
    echo 'DOOM_DEMOS requires DOOM_WAD.' >&2
    exit 1
  fi
  exit 0
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
