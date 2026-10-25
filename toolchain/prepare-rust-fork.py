#!/usr/bin/env python3
"""Verify or fetch pinned Rust bootstrap inputs into its ordinary cache layout.

SOURCE CACHE must be absolute, separate paths. Default operation is offline;
--fetch permits only the pinned internal cache URLs. This prepares inputs, not
a compiler installation, and never invokes downloaded code or a build.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request


MANIFEST = Path(__file__).resolve().parent / "rust/fork-toolchain.json"
ROUTES = (
    "https://repo.internal/repository/raw-rust/",
    "https://repo.internal/repository/raw-rust-ci/",
)
LLVM_PATHS = ("src/llvm-project", "src/bootstrap/download-ci-llvm-stamp", "src/version")


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def git(source, *arguments):
    return subprocess.check_output(["git", "-C", str(source), *arguments], text=True).strip()


class CacheRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, url):
        if not url.startswith(ROUTES):
            raise ValueError(f"Redirect outside pinned Rust cache routes: {url}")
        return super().redirect_request(request, response, code, message, headers, url)


def prepare(source, cache, fetch):
    if not source.is_absolute() or not cache.is_absolute():
        raise ValueError("SOURCE and CACHE must be absolute paths")
    source, cache = source.resolve(), cache.resolve()
    if source == cache or source.is_relative_to(cache) or cache.is_relative_to(source):
        raise ValueError("SOURCE and CACHE must be separate directory trees")
    manifest = json.loads(MANIFEST.read_text())
    pin = manifest["rust_base_commit"]
    subprocess.run(["git", "-C", str(source), "merge-base", "--is-ancestor", pin, "HEAD"],
                   check=True)
    if digest(source / "src/stage0") != manifest["stage0_metadata_sha256"]:
        raise ValueError("Rust stage0 metadata differs from the pinned inputs")
    upstream = git(source, "rev-list", "--author-date-order",
                   "--author=bors@rust-lang\\.org", "-n1", "HEAD")
    if not upstream:
        raise ValueError("Rust checkout lacks upstream bootstrap history")
    revision = git(source, "rev-list", "--first-parent", "-n1", upstream,
                   "--author=bors@rust-lang\\.org", "--", *LLVM_PATHS)
    if revision != manifest["ci_llvm_revision"]:
        raise ValueError(f"Rust CI LLVM history differs from its pin: {revision}")
    if git(source, "diff", pin, "--", *LLVM_PATHS):
        raise ValueError("Fork changed CI LLVM selection; resolve and pin new inputs first")
    expected = {"rustc", "rust-std", "cargo", "ci-llvm"}
    if {c["name"] for c in manifest["components"]} != expected:
        raise ValueError("Incomplete Rust bootstrap component manifest")
    cache.mkdir(parents=True, exist_ok=True)
    results = []
    for component in manifest["components"]:
        url = component["cache_url"]
        if not url.startswith(ROUTES):
            raise ValueError(f"Unapproved Rust input URL: {url}")
        destination = cache / component["cache_path"]
        if not destination.resolve().is_relative_to(cache) or destination.is_symlink():
            raise ValueError(f"Rust input path escapes cache or is a symlink: {destination}")
        if not destination.exists():
            if not fetch:
                raise ValueError(f"Missing Rust input; use --fetch to seed it: {destination}")
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
                    temporary = Path(output.name)
                    print(f"Fetching {url}", flush=True)
                    opener = urllib.request.build_opener(CacheRedirect())
                    with opener.open(url, timeout=120) as response:
                        shutil.copyfileobj(response, output, length=1024 * 1024)
                if digest(temporary) != component["sha256"]:
                    raise ValueError(f"Rust input hash mismatch: {component['name']}")
                os.replace(temporary, destination)
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
        if not destination.is_file() or digest(destination) != component["sha256"]:
            raise ValueError(f"Rust cache hash mismatch: {destination}")
        print(f"Verified {component['name']}: {component['sha256']}", flush=True)
        results.append({**component, "path": str(destination), "size": destination.stat().st_size})
    (cache / "pyxis-inputs.json").write_text(json.dumps({
        "manifest_sha256": digest(MANIFEST), "rust_commit": git(source, "rev-parse", "HEAD"),
        "rust_dirty": bool(git(source, "status", "--porcelain")),
        "components": results,
    }, indent=2) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("cache", type=Path)
    parser.add_argument("--fetch", action="store_true")
    args = parser.parse_args()
    try:
        prepare(args.source, args.cache, args.fetch)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Rust fork input preparation failed: {error}\n")


if __name__ == "__main__":
    main()
