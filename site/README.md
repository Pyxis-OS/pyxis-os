# pyxis.nexus

The custom Hugo theme keeps the landing page's palette, font stack, logo and
JVM figure. A compact logo sits beside the shared top navigation, including on
phone widths; Download links directly to the ISO placeholder. `site/` stays with pyxis-os so a site build can read
the manual from the same repository at each release tag; no new repository is
needed. Markdown is copied into a temporary Hugo tree at build time, never
maintained here as another manual. No external theme, JavaScript or font service.

## Local preview

Use Python 3.11+, Git and the standard **Hugo 0.167.0** binary. No Go compiler,
Node, Sass or initialized OS submodules are needed.

```sh
python3 site/build.py --hugo /absolute/path/to/hugo \
  --destination /absolute/path/to/preview
python3 -m http.server --bind 127.0.0.1 --directory /absolute/path/to/preview 8080
```

The destination is generated output and Hugo cleans it on each build. The build
is local and does not deploy. Open the HTTP preview, rather than `file://`, so
root-relative links work. `/manual/` redirects to the selected latest version;
the destination is also an ordinary link for readers without refresh support.

Default input is `docs/manual/` at HEAD under `/manual/0.0.8/`. Today it is only
an outline, explicitly marked **in preparation**, with unwritten topics left
unlinked. Released manuals use the matching tag, for example after 0.0.8 exists:

```sh
python3 site/build.py --hugo /absolute/path/to/hugo \
  --manual 0.0.8=0.0.8 --manual 0.0.9=0.0.9 --latest-manual 0.0.9
```

Only request tags that actually contain the manual; no manual is invented for
older releases. Each page links its source commit. The gallery imports the
source captions and images, including the Caddy nested-QEMU/browser composite.
Relative links within an imported manual remain in that version; references
outside it open the matching source revision. Private review links render as
plain labels. `build-source.json` records exact input commits and Hugo version.

## Hugo pin

The brief permits the official release binary, verified against its published
SHA-256. Cache these exact files for future builds:

- [hugo_0.167.0_linux-amd64.tar.gz](https://github.com/gohugoio/hugo/releases/download/v0.167.0/hugo_0.167.0_linux-amd64.tar.gz)
  — `4d84519b9f619e6d4c3fb45a50157abeabeb724f859c60605f44c23def6e1169`.
- [hugo_0.167.0_checksums.txt](https://github.com/gohugoio/hugo/releases/download/v0.167.0/hugo_0.167.0_checksums.txt)
  — `3e660936247093840181ea5e0af5be3772e2f0c7720d6ce9e9645a12ab7531ec`.

Hugo is a host build tool, not a vendored binary or target-image input. Its
official archive contains its Apache-2.0 notice. The locally installed binary
was checked against both the pinned hash and the release checksum manifest.

## Publication

Nothing here deploys or changes today's Worker. A later, separately approved
Wrangler deployment can serve the generated directory after the current Worker
name and bindings are checked on horse. Do not create a repository or deploy as
part of a preview build.

`data/project.toml` keeps the 0.0.8 ISO link explicitly unpublished. The target is
`https://packages.pyxis.nexus/images/pyxis-0.0.8.iso`; enable it only after that
image and its signed SHA256SUMS are published. The current packages Worker
allowlists only `/public/` and `/releases/`, so `/images/` support is a separate
publication prerequisite. No placeholder checksum or release notes are invented.
