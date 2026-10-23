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
