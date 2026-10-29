# Package signing certificates

Public certificates only; the private key never leaves chronium's 1Password
vault ([decision 71](../../docs/wip/pkg-upgrade.md#decided)).

| File | Certificate | SHA-256 fingerprint |
| --- | --- | --- |
| `pyxis-signing-ca.pem` | Pyxis Software Signing CA, to 2029-10-22 | `D5:18:25:8B:27:E6:A2:B0:D7:81:A0:35:BE:E5:D3:2C:7F:C2:38:97:84:26:98:25:30:39:7E:64:3A:91:69:73` |
| `signer-2026.pem` | Pyxis Release and Package Signer 2026, to 2027-10-23 | `BF:46:A3:52:C2:2F:DF:3F:8E:4B:F2:EF:55:9F:74:4D:AF:28:63:14:E0:01:78:6C:B6:DA:6E:6F:B4:3B:55:59` |

The core archive ships the CA as
`boot://share/pyxis/trust/software-signing-ca.pem`, the only certificate pkg
trusts for signed repositories; the Homelab Root that issued it is not trusted.
A signed publish copies its leaf beside the index under this name. A new yearly
leaf gets a new file here and a new name; published leaves never change.

## Signing a publish

`tools/sign-repository.sh NAME DIR` publishes and signs the next index of a
repository tree. Only chronium runs it, with the 1Password CLI signed in; the
key reaches `openssl` through a pipe. It refuses unless `DIR/current` and its
index match what `https://packages.pyxis.nexus/NAME/` serves, so pack into a
signing tree with `REPOSITORY_NAME=` to leave `current` alone. It then runs
`pxpkg publish --signer signer-2026.pem`, signs `index/N.lua`, and checks the
chain at the signing time, CA:FALSE, the signature and the tree before
printing the upload order. On any failure it removes the new index and
restores `current`. It uploads nothing.

## Signing the install ISOs and images

`tools/sign-images.sh DIR` signs `DIR/SHA256SUMS` as `DIR/SHA256SUMS.sig`
([decision 75](../../docs/wip/pkg-upgrade.md#decided)). The signature format is
the same as an index's: DER ECDSA P-256 over the SHA-256 of the file. Only
chronium runs the script, as for a repository. Before signing, it checks:
- `DIR/SHA256SUMS` keeps every line that
  `https://packages.pyxis.nexus/images/SHA256SUMS` serves, because every ISO
  and QEMU image stays published;
- every `pyxis-*.iso` and `pyxis-*.qcow2` in DIR is listed, and `sha256sum
  --check` passes.

After signing, it checks the chain, CA:FALSE and the signature, copies both
certificates into DIR, and prints the upload order. It uploads nothing.

Each release's publish signs `SHA256SUMS` again with the current leaf, so a
new leaf takes over at the next release.

## Checking an install ISO

A ready-to-run QEMU image, `pyxis-VERSION.qcow2`, is listed in the same
`SHA256SUMS` and checked the same way; the last command then names it.

From `https://packages.pyxis.nexus/images/`, download:
- `pyxis-VERSION.iso`;
- `SHA256SUMS` and `SHA256SUMS.sig`;
- `signer-2026.pem` and `pyxis-signing-ca.pem`.

Then run:

```sh
openssl x509 -in pyxis-signing-ca.pem -noout -fingerprint -sha256
openssl verify -x509_strict -partial_chain -purpose codesign \
  -CAfile pyxis-signing-ca.pem signer-2026.pem
openssl x509 -in signer-2026.pem -noout -pubkey > signer.pub
openssl dgst -sha256 -verify signer.pub -signature SHA256SUMS.sig SHA256SUMS
sha256sum --check --ignore-missing SHA256SUMS
```

The first command's fingerprint must equal the CA's in the table above, which
pyxis.nexus also shows. The others must print `OK`, `Verified OK` and
`pyxis-VERSION.iso: OK`.
