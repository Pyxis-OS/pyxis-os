#!/bin/sh
# Usage: tools/sign-repository.sh NAME DIR
#
# Publishes DIR's working index as the next signed index of repository NAME
# (docs/interfaces/packages.md#signed-indexes). Only chronium runs this, on his
# machine: the signer key comes from 1Password through a pipe and is never
# written to disk. Nothing is uploaded; the upload order is printed at the end.
#
# PACKAGE_HOST, SIGNER_CERT and SIGNING_CA default to the published host and
# the certificates in packages/signing; a test run points them elsewhere.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
package_host=${PACKAGE_HOST:-https://packages.pyxis.nexus/}
signer_cert=${SIGNER_CERT:-$root/packages/signing/signer-2026.pem}
signing_ca=${SIGNING_CA:-$root/packages/signing/pyxis-signing-ca.pem}
# The key belongs to signer-2026.pem; a new leaf changes both together.
key_item="Pyxis Release and Package Signer 2026 Private Key"
key_vault="Homelab PKI"

fail() {
  echo "sign-repository: $1" >&2
  exit 1
}

[ "$#" -eq 2 ] || fail "usage: tools/sign-repository.sh NAME DIR"
name=$1
directory=$2
printf '%s\n' "$name" | grep -qxE '[a-z0-9][a-z0-9.-]{0,63}' || fail "invalid repository name: $name"
[ -f "$directory/index.lua" ] || fail "$directory has no index.lua; pack into it first"
case "$package_host" in
  */) ;;
  *) fail "PACKAGE_HOST must end in /" ;;
esac
for certificate in "$signer_cert" "$signing_ca"; do
  [ -f "$certificate" ] || fail "missing $certificate"
done

work=$(mktemp -d)
previous=
published=
cleanup() {
  status=$?
  if [ "$status" -ne 0 ] && [ -n "$published" ]; then
    # Nothing left this machine, so serial N is reused by the next attempt.
    rm -f "$directory/index/$published.lua" "$directory/index/$published.lua.sig"
    if [ -n "$previous" ]; then
      printf '%s\n' "$previous" > "$directory/current.restore"
      mv "$directory/current.restore" "$directory/current"
    else
      rm -f "$directory/current"
    fi
    echo "sign-repository: removed index $published and restored current" >&2
  fi
  rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

# The serial follows what clients have seen, so DIR must hold exactly the
# published index. Pack with REPOSITORY_NAME= so packing leaves current alone.
if [ -f "$directory/current" ]; then
  previous=$(cat "$directory/current")
fi
live=$(curl -sS -o "$work/current" -w '%{http_code}' "${package_host}$name/current") ||
  fail "cannot read ${package_host}$name/current"
case "$live" in
  200)
    live=$(cat "$work/current")
    [ "$live" = "$previous" ] ||
      fail "published current is $live but $directory/current is ${previous:-absent}"
    curl -sSf -o "$work/index.lua" "${package_host}$name/index/$live.lua" ||
      fail "cannot read published index $live"
    cmp -s "$work/index.lua" "$directory/index/$live.lua" ||
      fail "$directory/index/$live.lua differs from the published one"
    ;;
  404)
    [ -z "$previous" ] || fail "nothing is published, but $directory/current is $previous"
    ;;
  *)
    fail "${package_host}$name/current answered HTTP $live"
    ;;
esac

signed_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)
python3 "$root/tools/pxpkg.py" publish --repository "$name" --signed-at "$signed_at" \
  --signer "$signer_cert" "$directory"
published=$(cat "$directory/current")
index=$directory/index/$published.lua
certificate=$directory/$(basename "$signer_cert")

op document get "$key_item" --vault "$key_vault" |
  openssl dgst -sha256 -sign /dev/stdin -out "$index.sig" "$index"

# The checks pkg makes, before anything is uploaded.
openssl verify -x509_strict -partial_chain -purpose codesign \
  -attime "$(date -u -d "$signed_at" +%s)" -CAfile "$signing_ca" "$certificate" ||
  fail "the signer does not chain to the signing CA at $signed_at"
openssl x509 -in "$certificate" -noout -ext basicConstraints | grep -q 'CA:FALSE' ||
  fail "the signer certificate is not CA:FALSE"
openssl x509 -in "$certificate" -noout -pubkey > "$work/signer.pub"
openssl dgst -sha256 -verify "$work/signer.pub" -signature "$index.sig" "$index" ||
  fail "the new signature does not verify"
python3 "$root/tools/pxpkg.py" check "$directory"
published_index=$published
published=

cat <<EOF
Signed $name serial $published_index at $signed_at.
Upload, in this order, after chronium's OK for this publish:
  1. archives under $directory/packages/ that the bucket does not hold yet
  2. $(basename "$certificate"), if the bucket does not hold it yet
  3. index/$published_index.lua and index/$published_index.lua.sig
  4. current, last
EOF
