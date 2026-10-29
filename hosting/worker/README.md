# packages.pyxis.nexus

A Cloudflare Worker serves the signed `public` and `releases` package
repositories ([signed indexes](../../docs/interfaces/packages.md#signed-indexes))
and the install ISOs and QEMU images
([checking an ISO](../../packages/signing/README.md#checking-an-install-iso))
from the B2 bucket `PyxisPackageRepo` (eu-central-003), whose object keys equal
the URL paths:

```text
public/current         public/index/N.lua(.sig)   public/signer-2026.pem
public/packages/ID/VERSION.tar
releases/current       releases/index/N.lua(.sig) releases/signer-2026.pem
releases/packages/pyxis/VERSION.tar
images/pyxis-VERSION.iso  images/pyxis-VERSION.qcow2
images/SHA256SUMS(.sig)
images/signer-2026.pem    images/pyxis-signing-ca.pem
```

The bucket is public-read, so the Worker fetches
`https://f003.backblazeb2.com/file/PyxisPackageRepo/PATH` without credentials.
Integrity comes from the signatures, not from B2, Cloudflare or TLS.

## Worker

`worker.js` answers GET and HEAD only, and only for paths matching
`^/(public|releases|images)/[A-Za-z0-9._+/-]+$` with no empty, `.` or `..` segment and
no query; anything else is 404. It forwards `Range` and the conditional
headers, streams the body and drops B2's `x-bz-*` headers. B2 decodes `+` in a
download-by-name path as a space, so the Worker sends a path's `+` (as in
`ID/VARIANT+ABI.tar`) upstream as `%2B`; a client's own `%2B` is still refused.

| Path | Edge and client cache |
| --- | --- |
| `REPO/current`, `images/SHA256SUMS`, `images/SHA256SUMS.sig` | 30 s |
| `REPO/signer*.pem`, `images/signer*.pem`, `images/pyxis-signing-ca.pem` | 1 hour |
| `REPO/index/N.lua`, `REPO/index/N.lua.sig`, `REPO/packages/ID/VERSION.tar`, `images/pyxis-VERSION.iso`, `images/pyxis-VERSION.qcow2` | 1 year, `immutable` |
| any other path | 404, not fetched |
| 404 from B2 | 30 s, so a missed upload does not stick |
| 5xx from B2 | not cached |

`current`, `SHA256SUMS` and `SHA256SUMS.sig` are the only objects a publish
replaces. The signature changes with every new `SHA256SUMS`, so it gets the
same short cache. The immutable paths are never rewritten, so a year-long cache
cannot serve stale bytes.

## Deploy

Only after chronium's OK for that deploy, from this directory with Wrangler
logged in to the account that holds the `pyxis.nexus` zone:

```sh
wrangler deploy
```

`wrangler.toml` attaches the Worker to `packages.pyxis.nexus` as a Workers
Custom Domain. `wrangler deploy` creates the custom domain: Cloudflare adds the
proxied DNS record and the certificate, so no DNS record is made by hand. The
hostname must have no existing DNS record in the zone, and the Wrangler login
needs Workers Routes Write on it. The Worker is the origin and fetches B2
itself.

Before the first deploy, recheck against Cloudflare's and Backblaze's own pages:
- B2 egress to Cloudflare is free under the Bandwidth Alliance.
- The Workers plan's daily request limit and per-request CPU limit.
- A ranged request: a partial `206` must never be cached and later served as
  the whole object. ISO downloads make range requests likely.

## Publishing a signed repository

1. Pack into the signing tree with `REPOSITORY_NAME=`.
2. chronium runs `tools/sign-repository.sh NAME DIR`
   ([signing](../../packages/signing/README.md#signing-a-publish)).
3. After chronium's OK for that publish, upload with the `pyxis-b2` rclone
   remote, in the order the script prints:
   1. new archives;
   2. the signer certificate if new;
   3. `index/N.lua` and `index/N.lua.sig`;
   4. `current` last.

   Each upload is `rclone copyto --checksum DIR/PATH pyxis-b2:PyxisPackageRepo/NAME/PATH`,
   then a `HEAD` through `https://packages.pyxis.nexus/NAME/PATH` that compares
   the length.

A client that reads `current` before the last step still sees the previous,
complete index.

## Publishing an install ISO and QEMU image

1. From the same tagged build as the release package, run
   `make release-image IMAGES=DIR`, where DIR keeps the `SHA256SUMS` published
   so far.
   A release's QEMU image is built and listed between these two steps
   ([release process](../../docs/development/release-process.md#the-qemu-image)).
2. chronium runs `tools/sign-images.sh DIR`
   ([signing](../../packages/signing/README.md#signing-the-install-isos-and-images)).
3. After chronium's OK for that publish, upload to `images/` in the order the
   script prints:
   1. the new ISO, and the QEMU image if there is one;
   2. the certificates if new;
   3. `SHA256SUMS`;
   4. `SHA256SUMS.sig` last.

   Use the same `rclone copyto` and `HEAD` check as for a repository.

Until the last upload, a user can see the new `SHA256SUMS` beside the previous
signature, and verification fails; retrying after the 30-second cache passes.
