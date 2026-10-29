# pyxis.nexus

The custom Hugo theme keeps the landing page's palette, font stack, logo and
JVM figure. A compact logo sits beside the shared top navigation, including on
phone widths; Download links directly to the ISO. `site/` stays with pyxis-os so a site build can read
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

Default input is `docs/manual/` at the fetched `origin/main`, under
`/manual/0.0.9/`. The release's tag establishes that the manual is released;
documentation updates after that tag can still describe the same release.
`build-source.json` records both the exact documentation commit and release-tag
commit. The sidebar follows links in the manual's own contents table; unwritten
topics stay unlinked. Fetch main and tags before building. To select exact refs:

```sh
python3 site/build.py --hugo /absolute/path/to/hugo \
  --manual 0.0.9=origin/main --latest-manual 0.0.9
```

Repeat `--manual` to include other versions and their selected refs. Only request
refs that actually contain the manual; no manual is invented for
older releases. For example, `--manual 0.0.9=origin/main` includes current 0.0.9
documentation additions. Each page links its source commit. The gallery imports the
source captions and images, including the Caddy nested-QEMU/browser composite.
Relative links within an imported manual remain in that version; references
outside it open the matching source revision. Private review links render as
plain labels. `build-source.json` records exact input commits and Hugo version.

## Releases page

`/releases/` renders `docs/releases.md` at `--releases-ref` (default
`origin/main`), newest entry first, with an anchor per version such as
`/releases/#0.0.9`. The newest entry gets a line linking its install ISO and
`/verify/`. The build refuses a releases page whose newest entry differs from
`isoVersion` in `data/project.toml`, so the ISO link and the Download link
cannot name different versions. The text is copied at build time and never
maintained twice; links into the latest manual point at its pages.

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

[`wrangler.toml`](wrangler.toml) updates the existing **pyxis-nexus** Worker on
the **pyxis.nexus** custom domain. It serves only the generated static assets;
there is no Worker JavaScript, account ID or zone ID. Do not delete or rename
the existing Worker/domain. Wrangler is already installed and logged in on horse.

Build and deploy are separate commands. For a post-publication build, use
`--published-iso` only when preparing the site for the signed ISO publication:

```sh
python3 site/build.py --hugo /absolute/path/to/hugo --published-iso
wrangler deploy --dry-run --config site/wrangler.toml
# Run at publication, after the signed ISO and its verification files are uploaded:
wrangler deploy --config site/wrangler.toml
```

`--published-image` additionally links the ready-to-run QEMU image from the
homepage, the releases page and `/verify/`. Pass it only once
`pyxis-VERSION.qcow2` is uploaded and listed in the signed `SHA256SUMS` (a failed
image build leaves it off). It requires `--published-iso`, and the build refuses
unless the served manual is the `isoVersion` release's and has the "Run the
ready-made image" section, since the links point there.

Without `--published-iso`, the preview keeps the explicit unpublished label.
With it, the homepage names the configured install ISO without claiming that a
Git tag alone proves an upload. `build-source.json` records that input as well
as all source commits. The Download destination is
`https://packages.pyxis.nexus/images/pyxis-0.0.9.iso`. `/verify/` imports the
commands and CA fingerprint from
[`packages/signing/README.md`](../packages/signing/README.md#checking-an-install-iso);
the trusted-CA comparison comes before any signature/hash checks. No fingerprint
is maintained separately here.

For the VM-built shared packet, no Hugo installation is needed on horse. The
assets are `/home/chronium/shared/present/website/deploy/`, with a copy of this
config beside them and a packet manifest. On horse:

```sh
cd /home/chronium/shared/present/website
sha256sum --check --status DEPLOY-SHA256SUMS
/home/chronium/.local/bin/wrangler deploy --dry-run \
  --config wrangler.toml --assets /home/chronium/shared/present/website/deploy
# At publication, after the signed ISO and all four verification files are live:
/home/chronium/.local/bin/wrangler deploy \
  --config wrangler.toml --assets /home/chronium/shared/present/website/deploy
```

The `--assets` argument overrides the config's repository-local build directory.
The packet is built with `--published-iso`; its commands and provenance are also
in `/shared/present/website/DEPLOY.md`. Do not deploy the preview directory.

Wrangler's [static-assets configuration](https://developers.cloudflare.com/workers/static-assets/binding/)
and [custom domains](https://developers.cloudflare.com/workers/configuration/routing/custom-domains/)
describe these fields. The separate packages Worker serves `/images/` after
the ISO-publication change; its deployment and signed uploads are separate from
this site's deployment. No placeholder checksum or release notes are invented.

## GitHub mark

The top-right source link embeds the official GitHub Octicons `mark-github-16`
and replaces the landing page's former bottom Source line entirely. Its SVG
path is unchanged from [v19.19.0](https://github.com/primer/octicons/blob/54f0746b130b3b75c195cf3bfb2d05877614888c/icons/mark-github-16.svg),
commit `54f0746b130b3b75c195cf3bfb2d05877614888c`. Only the SVG wrapper adds
accessibility attributes; CSS uses the bar's text size and palette. Pages do not
fetch the mark or any script from GitHub. The complete upstream MIT notice is
retained and published as [`/licenses/github-mark.txt`](themes/pyxis/static/licenses/github-mark.txt).
