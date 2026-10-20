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

## Tone

The site must not read like generated filler. It follows the README rules: plain
and specific, real screenshots, no marketing language and no emojis.

## Open questions

- The page set beyond releases, the manual and the gallery.
- Where the generator and the screenshots live in the repository.
