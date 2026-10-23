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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hugo", default="hugo", help="Hugo 0.167.0 binary")
    parser.add_argument("--destination", type=Path, default=ROOT / "build/website/public",
                        help="generated output directory; Hugo cleans it before building")
    parser.add_argument("--manual", action="append", metavar="VERSION=REF",
                        help="repeat for each release's own docs/manual tree; default 0.0.8=HEAD")
    parser.add_argument("--latest-manual", help="version served by /manual/; defaults to the first --manual")
    parser.add_argument("--gallery-ref", default="HEAD", help="revision supplying the gallery, default HEAD")
    args = parser.parse_args()

    version = subprocess.check_output([args.hugo, "version"], text=True)
    if not re.match(r"hugo v" + re.escape(HUGO_VERSION) + r"(?:[-+ ]|$)", version):
        parser.error("use the pinned Hugo " + HUGO_VERSION)

    project = tomllib.loads((SITE / "data/project.toml").read_text())
    manuals = []
    seen = set()
    for specification in args.manual or [project["defaultManual"] + "=HEAD"]:
        label, separator, ref = specification.partition("=")
        if not separator or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+[a-z]?", label):
            parser.error("manual must be VERSION=REF, for example 0.0.8=0.0.8")
        if label in seen:
            parser.error("duplicate manual version: " + label)
        seen.add(label)
        commit = revision(ref)
        tags = git("tag", "--points-at", commit).decode().splitlines()
        manuals.append({"version": label, "revision": commit, "unreleased": label not in tags})
    latest = args.latest_manual or manuals[0]["version"]
    if latest not in seen:
        parser.error("--latest-manual must name one of the included manuals")

    gallery_revision = revision(args.gallery_ref)
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
        content = work / "content"
        routes = {}

        for manual in manuals:
            label, commit = manual["version"], manual["revision"]
            manual_userland = git("ls-tree", commit, "--", "userspace").decode().split()[2]
            names = files(commit, "docs/manual")
            if "docs/manual/README.md" not in names:
                parser.error(label + " has no docs/manual/README.md at " + commit)
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

        page(content / "manual/_index.md",
             "# User manual\n\nContinue to the [" + latest + " manual](/manual/" + latest + "/).\n",
             redirect="/manual/" + latest + "/")
        (content / "_index.md").write_text('+++\ntitle = "Pyxis OS"\n+++\n')
        source = {"latestRelease": latest_release, "manuals": manuals,
                  "galleryRevision": gallery_revision, "userlandRevision": userland}
        (work / "data/source.json").write_text(json.dumps(source, indent=2) + "\n")
        (work / "data/routes.json").write_text(json.dumps(routes, indent=2) + "\n")
        subprocess.run([args.hugo, "--source", str(work), "--destination", str(destination),
                        "--cleanDestinationDir", "--panicOnWarning"], check=True)
        (destination / "build-source.json").write_text(json.dumps({"hugo": HUGO_VERSION, **source}, indent=2) + "\n")
    print("Preview: " + str(destination))


if __name__ == "__main__":
    main()
