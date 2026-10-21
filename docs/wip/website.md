# Project website

Status: **accepted direction (chronium, 2026-10-20); not implemented.** Target:
0.0.8, with the public package mirror, the first tester ISO and a short technical
manual. The domain `pyxis.nexus` was registered on 2026-10-20.

## Direction

- **Static files from the repository docs.** A small host script, with no
  framework, generates the site from the docs: the [release history](../releases.md),
  the manual and a gallery of screenshots.
- **Hosting.** Cloudflare Pages first. Later Pyxis serves the same static files
  itself on the netcup node, the public server target.
- **Review before publishing.** chronium reviews the look, the generator and the
  wording before anything is published.
- **Deploy credentials** stay chronium's.
- **Download section (chronium, 2026-10-21).** A small section on `pyxis.nexus`
  links the current tester ISO with its size, checksum and signature, and the
  manual. The ISO itself is hosted with the package mirror (`pkg.pyxis.nexus`), not
  on the website; see [tester ISOs](packages.md#later-ideas).

## Certificates

Two certificates stay apart (chronium, 2026-10-21):

- **TLS for `pyxis.nexus` and `pkg.pyxis.nexus`.** Today it is Cloudflare's edge
  certificate. When Pyxis serves the files from the netcup node, the origin uses a
  Cloudflare Origin CA certificate if Cloudflare proxies it, or ACME (Let's
  Encrypt) if it is served directly. Not decided.
- **Package and ISO signing.** chronium's homelab root CA issues a new
  intermediate, trusted only through Pyxis's own shipped anchors. See
  [tester ISOs](packages.md#later-ideas).

The homelab CA is never used for the public TLS name.

## Tone

The site must not read like generated filler. It follows the README rules: plain
and specific, real screenshots, no marketing language and no emojis.

## Open questions

- Download bandwidth and caching for the ISO on the mirror host.
- The page set beyond releases, the manual and the gallery.
- Where the generator and the screenshots live in the repository.
- The generator's language. chronium suggests Go with the standard library only
  (for example `html/template`), so the generator could later run on Pyxis itself
  once the [Go port](go-runtime.md) exists; Go is planned after hosted Clang.
  This is a suggestion (chronium, 2026-10-20), not decided.
