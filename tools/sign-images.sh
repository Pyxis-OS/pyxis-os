#!/bin/sh
# Usage: tools/sign-images.sh DIR
#
# Signs DIR/SHA256SUMS, the checksums of every published install ISO
# (docs/wip/pkg-upgrade.md, decision 75), as DIR/SHA256SUMS.sig. Only chronium
# runs this, on his machine: the signer key comes from 1Password through a pipe
# and is never written to disk. Nothing is uploaded; the upload order is printed
# at the end.
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
  echo "sign-images: $1" >&2
  exit 1
}

[ "$#" -eq 1 ] || fail "usage: tools/sign-images.sh DIR"
directory=$1
sums=$directory/SHA256SUMS
[ -f "$sums" ] || fail "$directory has no SHA256SUMS; run make release-image first"
case "$package_host" in
  */) ;;
  *) fail "PACKAGE_HOST must end in /" ;;
esac
for certificate in "$signer_cert" "$signing_ca"; do
  [ -f "$certificate" ] || fail "missing $certificate"
done
if grep -vxE '[0-9a-f]{64}  pyxis-[0-9a-z][0-9a-z.-]{0,63}\.iso' "$sums" >/dev/null; then
  fail "$sums has a line that is not a pyxis-VERSION.iso checksum"
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM

# Every published ISO stays, so DIR must keep every line clients can see.
published=$(curl -sS -o "$work/published" -w '%{http_code}' "${package_host}images/SHA256SUMS") ||
  fail "cannot read ${package_host}images/SHA256SUMS"
case "$published" in
  200)
    while IFS= read -r line || [ -n "$line" ]; do
      grep -qxF -- "$line" "$sums" || fail "$sums drops the published line: $line"
    done < "$work/published"
    ;;
  404) ;;
  *) fail "${package_host}images/SHA256SUMS answered HTTP $published" ;;
esac

# Each ISO in DIR must be listed and match; ISOs published earlier may be absent.
for iso in "$directory"/pyxis-*.iso; do
  [ -e "$iso" ] || continue
  awk -v name="$(basename "$iso")" '$2 == name { found = 1 } END { exit !found }' "$sums" ||
    fail "$iso is not listed in SHA256SUMS"
done
(cd "$directory" && sha256sum --check --strict --ignore-missing SHA256SUMS) ||
  fail "an ISO in $directory does not match SHA256SUMS"

op document get "$key_item" --vault "$key_vault" |
  openssl dgst -sha256 -sign /dev/stdin -out "$work/SHA256SUMS.sig" "$sums"

# The checks a user makes, before anything is uploaded.
openssl verify -x509_strict -partial_chain -purpose codesign -CAfile "$signing_ca" "$signer_cert" ||
  fail "the signer does not chain to the signing CA now"
openssl x509 -in "$signer_cert" -noout -ext basicConstraints | grep -q 'CA:FALSE' ||
  fail "the signer certificate is not CA:FALSE"
openssl x509 -in "$signer_cert" -noout -pubkey > "$work/signer.pub"
openssl dgst -sha256 -verify "$work/signer.pub" -signature "$work/SHA256SUMS.sig" "$sums" ||
  fail "the new signature does not verify"

# Published certificates never change; a new leaf gets a new name.
for certificate in "$signer_cert" "$signing_ca"; do
  copy=$directory/$(basename "$certificate")
  if [ -e "$copy" ]; then
    cmp -s "$certificate" "$copy" || fail "$copy differs from $certificate"
  else
    cp "$certificate" "$copy"
  fi
done
mv "$work/SHA256SUMS.sig" "$sums.sig"

cat <<EOF
Signed $sums.
Upload, in this order, after chronium's OK for this publish:
  1. ISOs in $directory that the bucket does not hold yet
  2. $(basename "$signer_cert") and $(basename "$signing_ca"), if the bucket does not hold them yet
  3. SHA256SUMS
  4. SHA256SUMS.sig, last
EOF
