#!/usr/bin/env python3
"""Build pyxis.nexus from the custom theme and versioned repository documents."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib


ROOT = Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
HUGO_VERSION = "0.167.0"


def git(*arguments):
    return subprocess.check_output(["git", "-C", str(ROOT), *arguments])


def revision(ref):
    return git("rev-parse", "--verify", ref + "^{commit}").decode().strip()


def files(ref, directory):
    listing = git("ls-tree", "-r", "--name-only", "-z", ref, "--", directory)
    return [name.decode() for name in listing.split(b"\0") if name]


def read(ref, name):
    return git("show", ref + ":" + name)


def page(destination, text, **parameters):
    destination.parent.mkdir(parents=True, exist_ok=True)
    title = re.search(r"^# (.+)$", text, re.MULTILINE)
    metadata = {"title": title[1] if title else "Pyxis", **parameters}
    frontmatter = "+++\n"
    for key, value in metadata.items():
        frontmatter += key + " = " + json.dumps(value, ensure_ascii=False) + "\n"
    destination.write_text(frontmatter + "+++\n\n" + text)


def releases_page(commit, iso_version, image_published=False):
    """docs/releases.md at commit, with a stable anchor per version and the
    latest entry's install links. The text is never maintained a second time."""
    lines = read(commit, "docs/releases.md").decode().split("\n")
    out = []
    latest = None
    for line in lines:
        entry = re.match(r"## (\d+\.\d+\.\d+[a-z]?)\b.*$", line)
        if entry:
            out.append(line + " {#" + entry[1] + "}")
            if latest is None:
                latest = entry[1]
                out.append("")
                out.append("Install ISO: [pyxis-" + latest + ".iso]"
                           "(https://packages.pyxis.nexus/images/pyxis-" + latest + ".iso), "
                           "and [how to verify it](/verify/).")
                if image_published:
                    out.append("")
                    out.append("QEMU image: [pyxis-" + latest + ".qcow2]"
                               "(https://packages.pyxis.nexus/images/pyxis-" + latest + ".qcow2), "
                               "[how to run it](/manual/" + latest + "/install/#run-the-ready-made-image) "
                               "and [how to verify it](/verify/).")
        else:
            out.append(line)
    if latest != iso_version:
        raise SystemExit("releases.md's newest entry is " + str(latest) + " but site/data/project.toml "
                         "isoVersion is " + iso_version)
    return "\n".join(out)


def iso_verification(commit, version, image_published=False):
    reference = read(commit, "packages/signing/README.md").decode()
    section = reference.split("## Checking an install ISO\n", 1)[1]
    fingerprint = re.search(r"\| `pyxis-signing-ca\.pem` \|[^\n]+\| `([^`]+)` \|", reference)[1]
    signer = re.search(r"- `(signer-[^`]+\.pem)` and `pyxis-signing-ca\.pem`", section)[1]
    commands = re.search(r"```sh\n(.*?)```", section, re.DOTALL)[1]
    first, remaining = commands.split("\n", 1)
    iso = "pyxis-" + version + ".iso"
    image = "pyxis-" + version + ".qcow2"
    names = [iso, "SHA256SUMS", "SHA256SUMS.sig", signer, "pyxis-signing-ca.pem"]
    if image_published:
        names.insert(1, image)
    downloads = "\n".join("- [" + name + "](https://packages.pyxis.nexus/images/" + name + ")" for name in names)
    count = "six" if image_published else "five"
    image_check = ""
    image_note = ""
    if image_published:
        image_check = (f" Verify the ready-to-run QEMU image the same way: the checksum check must also name "
                       f"**`{image}: OK`**, and a disk image that fails it must not be booted.")
        image_note = (f" The QEMU image `{image}` is listed in the same manifest; "
                      f"[run it with the launcher in the manual](/manual/{version}/install/#run-the-ready-made-image).")
    return f"""# Verify the install ISO{" and QEMU image" if image_published else ""}

Use OpenSSL 3.2 or later with the `codesign` purpose and GNU `sha256sum`.
Run the steps in order. Stop if a command fails or a value does not match.

## 1. Download the files

Save these {count} files in the same directory, then open a terminal there:

{downloads}

## 2. Check the CA fingerprint first

The Pyxis Software Signing CA's SHA-256 certificate fingerprint must be:

```text
{fingerprint}
```

Run this command by itself:

```sh
{first}
```

Compare the entire printed fingerprint with the value above. **Stop on a
mismatch; do not continue.** This is a certificate fingerprint, not the hash of
the PEM file's bytes. Printing it alone does not authenticate the certificate.

## 3. Verify the signer, signature and ISO

After the fingerprint matches, run each command below. Stop at any error:

```sh
{remaining.rstrip()}
```

The certificate check must print `{signer}: OK`; the signature check must print
`Verified OK`; and the checksum check must name **`{iso}: OK`**.{image_check} Extracting the
public key normally prints nothing.

The manifest is cumulative, so `--ignore-missing` skips other releases you did
not download. An OK for another ISO is not enough: require the exact filename
above. Do not write or boot an image that fails verification.{image_note}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hugo", default="hugo", help="Hugo 0.167.0 binary")
    parser.add_argument("--destination", type=Path, default=ROOT / "build/website/public",
                        help="generated output directory; Hugo cleans it before building")
    parser.add_argument("--manual", action="append", metavar="VERSION=REF",
                        help="repeat for each release's docs/manual tree; default 0.0.8=origin/main")
    parser.add_argument("--latest-manual", help="version served by /manual/; defaults to the first --manual")
    parser.add_argument("--gallery-ref", default="origin/main", help="revision supplying the gallery, default origin/main")
    parser.add_argument("--releases-ref", default="origin/main",
                        help="revision supplying docs/releases.md for /releases/, default origin/main")
    parser.add_argument("--published-iso", action="store_true",
                        help="prepare post-publication copy; deploy only after the signed ISO is published")
    parser.add_argument("--published-image", action="store_true",
                        help="also link the QEMU image; only once pyxis-VERSION.qcow2 is live and listed in SHA256SUMS")
    args = parser.parse_args()

    version = subprocess.check_output([args.hugo, "version"], text=True)
    if not re.match(r"hugo v" + re.escape(HUGO_VERSION) + r"(?:[-+ ]|$)", version):
        parser.error("use the pinned Hugo " + HUGO_VERSION)

    project = tomllib.loads((SITE / "data/project.toml").read_text())
    project["isoPublished"] = args.published_iso
    if args.published_image and not args.published_iso:
        parser.error("--published-image needs --published-iso: the image is listed beside the ISO")
    manuals = []
    seen = set()
    for specification in args.manual or [project["defaultManual"] + "=origin/main"]:
        label, separator, ref = specification.partition("=")
        if not separator or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+[a-z]?", label):
            parser.error("manual must be VERSION=REF, for example 0.0.8=0.0.8")
        if label in seen:
            parser.error("duplicate manual version: " + label)
        seen.add(label)
        commit = revision(ref)
        tag = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--verify", "--quiet",
                              "refs/tags/" + label + "^{commit}"], capture_output=True, text=True)
        release_revision = tag.stdout.strip() if tag.returncode == 0 else None
        released = release_revision is not None and subprocess.run(
            ["git", "-C", str(ROOT), "merge-base", "--is-ancestor", release_revision, commit]).returncode == 0
        manuals.append({"version": label, "revision": commit, "releaseRevision": release_revision,
                        "unreleased": not released, "pages": []})
    latest = args.latest_manual or manuals[0]["version"]
    if latest not in seen:
        parser.error("--latest-manual must name one of the included manuals")

    releases_revision = revision(args.releases_ref)
    gallery_revision = revision(args.gallery_ref)
    signing_revision = revision("HEAD")
    latest_release = git("describe", "--tags", "--abbrev=0", "--match", "0.0.*",
                         gallery_revision).decode().strip()
    userland = git("ls-tree", gallery_revision, "--", "userspace").decode().split()[2]
    build = ROOT / "build/website"
    build.mkdir(parents=True, exist_ok=True)
    destination = args.destination.resolve()
    if (ROOT.is_relative_to(destination) or destination.is_relative_to(SITE) or
            (destination.is_relative_to(ROOT) and not destination.is_relative_to(ROOT / "build")) or
            build.is_relative_to(destination)):
        parser.error("destination must be a generated output directory, not the source tree")

    with tempfile.TemporaryDirectory(prefix="hugo-", dir=build) as temporary:
        work = Path(temporary)
        shutil.copy2(SITE / "hugo.toml", work / "hugo.toml")
        shutil.copytree(SITE / "themes", work / "themes")
        shutil.copytree(SITE / "data", work / "data")
        (work / "data/project.toml").write_text((SITE / "data/project.toml").read_text() +
                                               "isoPublished = " + str(args.published_iso).lower() + "\n" +
                                               "imagePublished = " + str(args.published_image).lower() + "\n")
        content = work / "content"
        routes = {}

        for manual in manuals:
            label, commit = manual["version"], manual["revision"]
            manual_userland = git("ls-tree", commit, "--", "userspace").decode().split()[2]
            names = files(commit, "docs/manual")
            if "docs/manual/README.md" not in names:
                parser.error(label + " has no docs/manual/README.md at " + commit)
            manual_routes = {}
            for name in names:
                relative = Path(name).relative_to("docs/manual")
                if relative.name == "README.md":
                    parent = "" if relative.parent == Path(".") else relative.parent.as_posix() + "/"
                    route = "/manual/" + label + "/" + parent
                    output = content / "manual" / label / relative.parent / "_index.md"
                elif relative.suffix == ".md":
                    route = "/manual/" + label + "/" + relative.with_suffix("").as_posix() + "/"
                    output = content / "manual" / label / relative
                else:
                    route = "/manual/" + label + "/" + relative.as_posix()
                    output = work / "static" / "manual" / label / relative
                    output.parent.mkdir(parents=True, exist_ok=True)
                    output.write_bytes(read(commit, name))
                    routes[label + ":" + commit + ":" + name] = route
                    continue
                route = route.rstrip("/") + "/"
                routes[label + ":" + commit + ":" + name] = route
                page(output, read(commit, name).decode(), url=route,
                     source_path=name, source_revision=commit, manual_version=label,
                     source_userland_revision=manual_userland,
                     manual_unreleased=manual["unreleased"])
                manual_routes[relative.as_posix()] = route
            readme = read(commit, "docs/manual/README.md").decode()
            for title, target in re.findall(r"\[([^\]]+)\]\(([^)]+\.md)\)", readme):
                if target in manual_routes:
                    manual["pages"].append({"title": title, "url": manual_routes[target]})

        gallery = read(gallery_revision, "docs/gallery/README.md").decode()
        gallery = "# Gallery\n\nScreenshots and photos of Pyxis running. Captions describe the stated revision and machine.\n\n" + gallery[gallery.index("## Highlights"):]
        page(content / "gallery/_index.md", gallery,
             source_path="docs/gallery/README.md", source_revision=gallery_revision,
             source_userland_revision=userland)
        routes[":" + gallery_revision + ":docs/gallery/README.md"] = "/gallery/"
        for name in files(gallery_revision, "docs/gallery/images"):
            relative = Path(name).relative_to("docs/gallery")
            output = work / "assets" / "gallery" / relative
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(read(gallery_revision, name))
            routes[":" + gallery_revision + ":" + name] = "/gallery/" + relative.as_posix()

        latest_manual = next(manual for manual in manuals if manual["version"] == latest)
        if args.published_image:
            install = read(latest_manual["revision"], "docs/manual/install.md").decode()
            if latest != project["isoVersion"] or "\n## Run the ready-made image\n" not in install:
                parser.error("--published-image links the " + project["isoVersion"] + " manual's image section, "
                             "but the served manual " + latest + " has none; pass --manual " +
                             project["isoVersion"] + "=REF and --latest-manual")
        for name in files(latest_manual["revision"], "docs/manual"):
            route = routes.get(latest + ":" + latest_manual["revision"] + ":" + name)
            if route:
                routes[":" + releases_revision + ":" + name] = route
        page(content / "releases/_index.md", releases_page(releases_revision, project["isoVersion"], args.published_image),
             source_path="docs/releases.md", source_revision=releases_revision,
             source_userland_revision=userland)
        routes[":" + releases_revision + ":docs/releases.md"] = "/releases/"
        page(content / "manual/_index.md",
             "# User manual\n\nContinue to the [" + latest + " manual](/manual/" + latest + "/).\n",
             redirect="/manual/" + latest + "/")
        page(content / "verify.md", iso_verification(signing_revision, project["isoVersion"], args.published_image),
             source_path="packages/signing/README.md", source_revision=signing_revision,
             source_userland_revision=userland)
        (content / "_index.md").write_text('+++\ntitle = "Pyxis OS"\n+++\n')
        source = {"siteRevision": revision("HEAD"), "latestRelease": latest_release, "manuals": manuals,
                  "galleryRevision": gallery_revision, "userlandRevision": userland,
                  "signingRevision": signing_revision}
        (work / "data/source.json").write_text(json.dumps(source, indent=2) + "\n")
        (work / "data/routes.json").write_text(json.dumps(routes, indent=2) + "\n")
        subprocess.run([args.hugo, "--source", str(work), "--destination", str(destination),
                        "--cleanDestinationDir", "--panicOnWarning"], check=True)
        (destination / "build-source.json").write_text(json.dumps({"hugo": HUGO_VERSION, **project, **source}, indent=2) + "\n")
    print("Preview: " + str(destination))


if __name__ == "__main__":
    main()
