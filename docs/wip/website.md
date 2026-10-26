# Project website

Status: **website implementation delivered (2026-10-23).** The accepted direction
(chronium, 2026-10-20) produced the Hugo site in [`site/`](../../site/README.md).
Builds, previews and publication are separate operations; repository contents do
not establish which build is currently deployed. The remaining self-hosting
direction below is future work.

## Implemented site

- **Generation.** `site/build.py` uses Python and pinned Hugo 0.167.0 with the
  repository's custom theme. It imports the versioned manual, release history
  and screenshot gallery from selected Git revisions, recording their exact
  source commits in `build-source.json`. The earlier framework-free/Go generator
  suggestion is superseded.
- **Hosting.** `site/wrangler.toml` configures the existing `pyxis-nexus`
  Cloudflare Worker to serve generated static assets on `pyxis.nexus`; the
  earlier Cloudflare Pages proposal is superseded. Build and deployment commands
  are in the [site reference](../../site/README.md#publication).
- **Review before publishing.** chronium reviews the look, the generator and the
  wording before anything is published.
- **Deploy credentials** stay chronium's.
- **Downloads and verification (chronium, 2026-10-21).** The site links the
  configured install ISO at `packages.pyxis.nexus` and imports verification
  instructions from the [signing reference](../../packages/signing/README.md#checking-an-install-iso).
  The build retains an unpublished label unless the explicit publication input
  is supplied; a release tag alone is not evidence of an uploaded ISO.

## Certificates

Two certificates stay apart (chronium, 2026-10-21):

- **TLS for `pyxis.nexus` and `packages.pyxis.nexus`.** The Cloudflare-facing
  deployment uses its public edge certificate. If Pyxis later serves the files
  from the netcup node, the origin would use a
  Cloudflare Origin CA certificate if Cloudflare proxies it, or ACME (Let's
  Encrypt) if it is served directly. Not decided.
- **Package and ISO signing.** The separate Pyxis Software Signing CA and
  signing workflow are implemented; the client trusts the shipped software
  signing anchor rather than the public TLS chain. See the
  [signing reference](../../packages/signing/README.md). Signing and deployment
  credentials remain chronium's.

The homelab CA is never used for the public TLS name.

## Tone

The site must not read like generated filler. It follows the README rules: plain
and specific, real screenshots, no marketing language and no emojis.

## Remaining direction

Serving these static files from Pyxis on the netcup node remains a separate
future target. Origin TLS, host deployment and its qualification still need
their own decisions. The implemented generator and repository location are
settled by `site/`; this file does not schedule a replacement generator.
