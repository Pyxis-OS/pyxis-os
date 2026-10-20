#!/bin/sh
set -eu
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$source_dir/../.." && pwd)
packet=${1:?usage: build.sh ABSOLUTE-PACKET-DIRECTORY}
case "$packet" in
  /*) ;;
  *) echo "require an absolute packet directory" >&2; exit 1 ;;
esac
case "$packet" in
  /dev|/dev/*) echo "packet directory must be outside /dev" >&2; exit 1 ;;
esac
cd "$repo"
test -f build/ports-dev/mbedtls/include/psa/crypto.h || {
  echo "build the image/port development headers first" >&2
  exit 1
}
make -j8 fs-tools
output=build/tools/installer-native-gate
mkdir -p "$output" "$packet"
cc=${HOST_CC:-cc}
"$cc" -std=gnu23 -O2 -ffunction-sections -fdata-sections -I include -I fs/include \
  -I build/ports-dev/mbedtls/include -idirafter userspace/include \
  -c userspace/installer/consent.c -o "$output/consent.o"
"$cc" -std=gnu23 -O2 -ffunction-sections -fdata-sections \
  -Dinstall_read=unused_native_install_read -I include -I fs/include \
  -I build/ports-dev/mbedtls/include -idirafter userspace/include \
  -c userspace/installer/io.c -o "$output/io.o"
"$cc" -std=gnu23 -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
  -I include -I fs/include -I userspace/installer -idirafter userspace/include \
  "$source_dir/media-consent.c" "$output/consent.o" "$output/io.o" \
  build/fs-tools/libnpfs-format.a -Wl,--gc-sections -o "$output/media-consent"
install -m 755 "$output/media-consent" build/fs-tools/fat32-inspect "$packet/"
install -m 644 "$source_dir/horse-check.py" "$source_dir/check-media.py" \
  "$source_dir/check-publication.py" "$source_dir/media-consent.c" "$packet/"
install -m 644 LICENSE "$packet/HOST-TOOLS-LICENSE"
printf '%s\n' "Host helpers copied. Runtime images, expected hashes and staging are untouched."
