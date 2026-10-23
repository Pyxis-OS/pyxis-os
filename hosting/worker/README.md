# packages.pyxis.nexus

A Cloudflare Worker serves the signed `public` and `releases` package
repositories ([signed indexes](../../docs/interfaces/packages.md#signed-indexes))
from the B2 bucket `PyxisPackageRepo` (eu-central-003), whose object keys equal
the URL paths:

```text
public/current         public/index/N.lua(.sig)   public/signer-2026.pem
public/packages/ID/VERSION.tar
releases/current       releases/index/N.lua(.sig) releases/signer-2026.pem
releases/packages/pyxis/VERSION.tar
```

The bucket is public-read, so the Worker fetches
`https://f003.backblazeb2.com/file/PyxisPackageRepo/PATH` without credentials.
Integrity comes from the signatures, not from B2, Cloudflare or TLS.

## Worker

`worker.js` answers GET and HEAD only, and only for paths matching
`^/(public|releases)/[A-Za-z0-9._+/-]+$` with no empty, `.` or `..` segment and
no query; anything else is 404. It forwards `Range` and the conditional
headers, streams the body and drops B2's `x-bz-*` headers.

| Path | Edge and client cache |
| --- | --- |
| `REPO/current` | 30 s |
| `REPO/signer*.pem` | 1 hour |
| `REPO/index/N.lua`, `REPO/index/N.lua.sig`, `REPO/packages/ID/VERSION.tar` | 1 year, `immutable` |
| any other path | 404, not fetched |
| 404 from B2 | 30 s, so a missed upload does not stick |
| 5xx from B2 | not cached |

`current` is the only object a publish replaces. The immutable paths are never
rewritten, so a year-long cache cannot serve stale bytes.

## Deploy

Only after chronium's OK for that deploy, from this directory with Wrangler
logged in to the account that holds the `pyxis.nexus` zone:

```sh
wrangler deploy
```

`wrangler.toml` routes `packages.pyxis.nexus/*` to the Worker; the hostname
needs a proxied DNS record in that zone.

Before the first deploy, recheck against Cloudflare's and Backblaze's own pages:
- B2 egress to Cloudflare is free under the Bandwidth Alliance.
- The Workers plan's daily request limit and per-request CPU limit.

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
