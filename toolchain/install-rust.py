#!/usr/bin/env python3
"""Install pinned host Rust outside the builder image (Python 3.12 or newer).

Usage: toolchain/install-rust.py PREFIX WORKDIR
Both paths must be absolute; PREFIX must not exist. WORKDIR retains verified
component downloads. On failure, inspect the incomplete prefix before removing
it and retrying. No existing installation is changed or selected globally.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import posixpath
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request


MANIFEST = Path(__file__).resolve().parent / "rust/host-toolchain.json"


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


class CacheRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, response, code, message, headers, url):
        if not url.startswith("https://repo.internal/repository/raw-rust/"):
            raise ValueError(f"Rust cache redirected outside its approved route: {url}")
        return super().redirect_request(request, response, code, message, headers, url)


def download(component, manifest, cache):
    destination = cache / component["filename"]
    if destination.is_symlink():
        raise ValueError(f"Cached archive must not be a symlink: {destination}")
    if not destination.exists():
        url = f'{manifest["cache_base"]}/{manifest["date"]}/{component["filename"]}'
        print(f"Downloading {url}", flush=True)
        opener = urllib.request.build_opener(CacheRedirect())
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=cache, delete=False) as output:
                temporary = Path(output.name)
                with opener.open(url, timeout=120) as response:
                    shutil.copyfileobj(response, output, length=1024 * 1024)
            if sha256(temporary) != component["sha256"]:
                raise ValueError(f"SHA-256 mismatch for downloaded {component['filename']}")
            temporary.replace(destination)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    if not destination.is_file() or sha256(destination) != component["sha256"]:
        raise ValueError(f"SHA-256 mismatch in cache; inspect and remove {destination}")
    print(f"Verified {component['filename']}: {component['sha256']}", flush=True)
    return destination


def extract(archive, destination):
    root = archive.name.removesuffix(".tar.xz")
    with tarfile.open(archive, "r:xz") as source:
        members = source.getmembers()
        for member in members:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or ".." in path.parts or not path.parts
                    or path.parts[0] != root):
                raise ValueError(f"Archive member escapes component root: {member.name}")
            if not (member.isdir() or member.isfile() or member.issym() or member.islnk()):
                raise ValueError(f"Unsupported archive member: {member.name}")
            if member.issym() or member.islnk():
                link = PurePosixPath(member.linkname)
                target = (posixpath.join(str(path.parent), member.linkname)
                          if member.issym() else member.linkname)
                resolved = PurePosixPath(posixpath.normpath(target))
                if link.is_absolute() or not resolved.parts or resolved.parts[0] != root:
                    raise ValueError(f"Archive link escapes component root: {member.name}")
        # data_filter also checks actual symlink resolution during extraction,
        # rejects special files and strips privileged modes/ownership metadata.
        source.extractall(destination, members=members, filter="data")
    return destination / root


def tool_output(prefix, tool, *arguments):
    environment = os.environ.copy()
    environment.pop("RUSTC_BOOTSTRAP", None)
    return subprocess.check_output([str(prefix / "bin" / tool), *arguments],
                                   env=environment, text=True).strip()


def install(prefix, work):
    if platform.system() != "Linux" or platform.machine() != "x86_64":
        raise ValueError("The pinned host toolchain requires Linux x86_64")
    if not hasattr(tarfile, "data_filter"):
        raise ValueError("Safe Rust archive extraction requires Python 3.12 or newer")
    if not prefix.is_absolute() or not work.is_absolute():
        raise ValueError("PREFIX and WORKDIR must be absolute paths")
    if os.path.lexists(prefix):
        raise ValueError(f"PREFIX must not already exist: {prefix}")
    prefix = prefix.resolve()
    work = work.resolve()
    if prefix.is_relative_to(work) or work.is_relative_to(prefix):
        raise ValueError("PREFIX and WORKDIR must be separate directory trees")
    manifest = json.loads(MANIFEST.read_text())
    cache = work / "downloads"
    cache.mkdir(parents=True, exist_ok=True)
    archives = [(component, download(component, manifest, cache))
                for component in manifest["components"]]
    prefix.mkdir(parents=True)
    provenance = prefix / "share/pyxis-rust"
    retained = provenance / "archives"
    retained.mkdir(parents=True)
    environment = os.environ.copy()
    environment.pop("RUSTC_BOOTSTRAP", None)
    for component, archive in archives:
        print(f"Installing {component['name']}", flush=True)
        with tempfile.TemporaryDirectory(prefix="extract-", dir=work) as directory:
            root = extract(archive, Path(directory))
            subprocess.run(["sh", str(root / "install.sh"), f"--prefix={prefix}",
                            "--disable-ldconfig"], cwd=root, env=environment, check=True)
        # Retain exact verified distributions, including every original notice,
        # installer/source manifest and the rust-src archive, beyond WORKDIR.
        shutil.copyfile(archive, retained / archive.name)
    rustc = tool_output(prefix, "rustc", "-Vv")
    identity = dict(line.split(": ", 1) for line in rustc.splitlines() if ": " in line)
    if (identity.get("commit-hash") != manifest["rustc_commit"]
            or identity.get("release") != manifest["rustc_release"]
            or identity.get("host") != manifest["host"]):
        raise ValueError(f"Installed rustc does not match manifest:\n{rustc}")
    cargo = tool_output(prefix, "cargo", "-Vv")
    cargo_identity = dict(line.split(": ", 1) for line in cargo.splitlines() if ": " in line)
    if (cargo_identity.get("release") != manifest["rustc_release"]
            or cargo_identity.get("host") != manifest["host"]):
        raise ValueError(f"Installed Cargo does not match manifest:\n{cargo}")
    library = prefix / "lib/rustlib/src/rust/library"
    if not all((library / name).is_file() for name in ("Cargo.lock", "core/src/lib.rs",
                                                      "alloc/src/lib.rs")):
        raise ValueError("Installed rust-src lacks the core/alloc sources or lockfile")
    host_lib = prefix / "lib/rustlib" / manifest["host"] / "lib"
    if not list(host_lib.glob("libstd-*.rlib")):
        raise ValueError("Installed host standard library is missing")
    shutil.copyfile(MANIFEST, provenance / "host-toolchain.json")
    (provenance / "installation.json").write_text(json.dumps({
        "prefix": str(prefix),
        "rustc_verbose_version": rustc,
        "cargo_verbose_version": cargo,
        "components": [{**component,
                        "cache_url": f'{manifest["cache_base"]}/{manifest["date"]}/{component["filename"]}',
                        "upstream_url": f'{manifest["upstream_base"]}/{manifest["date"]}/{component["filename"]}'}
                       for component in manifest["components"]],
    }, indent=2) + "\n")
    print(f"{rustc}\n{cargo}\nInstalled Rust prefix: {prefix}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("workdir", type=Path)
    arguments = parser.parse_args()
    try:
        install(arguments.prefix, arguments.workdir)
    except (OSError, ValueError, tarfile.TarError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Rust installation failed: {error}\n")


if __name__ == "__main__":
    main()
