#!/usr/bin/env python3
"""Export a built fork into a fresh standalone host prefix.

SOURCE PREFIX PREBUILT must be absolute. SOURCE uses upstream's build directory;
PREBUILT is the pinned stage-1 installation, supplying its unchanged Cargo and
distribution notices. Build host std/proc_macro and target core/alloc first.
"""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def run(*arguments):
    environment = os.environ.copy()
    environment.pop("RUSTC_BOOTSTRAP", None)
    return subprocess.check_output(arguments, text=True, env=environment).strip()


def export(source, prefix, prebuilt):
    if any(not path.is_absolute() for path in (source, prefix, prebuilt)):
        raise ValueError("SOURCE, PREFIX and PREBUILT must be absolute")
    if os.path.lexists(prefix):
        raise ValueError("PREFIX must be fresh")
    source, prefix, prebuilt = source.resolve(), prefix.resolve(), prebuilt.resolve()
    manifest = HERE / "rust/fork-toolchain.json"
    fork = json.loads(manifest.read_text())
    official = json.loads((HERE / "rust/host-toolchain.json").read_text())
    if json.loads((prebuilt / "share/pyxis-rust/host-toolchain.json").read_text()) != official:
        raise ValueError("PREBUILT is not the pinned stage-1 installation")
    subprocess.run(["git", "-C", str(source), "merge-base", "--is-ancestor",
                    fork["rust_base_commit"], "HEAD"], check=True)
    if run("git", "-C", str(source), "status", "--porcelain"):
        raise ValueError("SOURCE has uncommitted changes")
    head = run("git", "-C", str(source), "rev-parse", "HEAD")
    stage = source / "build" / fork["host"] / "stage2"
    if any(prefix.is_relative_to(path) for path in (stage, source / "library", prebuilt)):
        raise ValueError("PREFIX must not be inside an input tree")
    for target, crates in ((fork["host"], ("std", "proc_macro")),
                           (fork["target"], ("core", "alloc", "compiler_builtins"))):
        for crate in crates:
            if not list((stage / "lib/rustlib" / target / "lib").glob(f"lib{crate}-*.rlib")):
                raise ValueError(f"Build {crate} for {target} before exporting")
    version = run(str(stage / "bin/rustc"), "-Vv")
    if "commit-hash: " + head not in version:
        raise ValueError("Built compiler does not match SOURCE HEAD")
    cargo = next(c for c in official["components"] if c["name"] == "cargo")
    archive = prebuilt / "share/pyxis-rust/archives" / cargo["filename"]
    if digest(archive) != cargo["sha256"]:
        raise ValueError("Pinned Cargo archive hash mismatch")
    notices = prebuilt / "share/doc/rust"
    if not (notices / "COPYRIGHT-library.html").is_file() or not (notices / "licenses").is_dir():
        raise ValueError("PREBUILT lacks its distribution notices")

    # Bootstrap's source links point back through build/, including this sysroot.
    def ignore(directory, names):
        return {"src", "rustc-src"} if Path(directory) == stage / "lib/rustlib" else set()

    shutil.copytree(stage, prefix, symlinks=False, ignore=ignore)
    installer_spec = importlib.util.spec_from_file_location("pyxis_rust_installer", HERE / "install-rust.py")
    installer = importlib.util.module_from_spec(installer_spec)
    installer_spec.loader.exec_module(installer)
    with tempfile.TemporaryDirectory(prefix="pyxis-cargo-") as temporary:
        component = installer.extract(archive, Path(temporary))
        subprocess.run(["sh", str(component / "install.sh"), f"--prefix={prefix}",
                        "--disable-ldconfig"], cwd=component, check=True)
    shutil.copytree(notices, prefix / "share/doc/rust", dirs_exist_ok=True)
    shutil.copytree(source / "library", prefix / "lib/rustlib/src/rust/library",
                    ignore=shutil.ignore_patterns(".git", "target"))
    metadata = prefix / "share/pyxis-rust"
    metadata.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(manifest, metadata / "fork-toolchain.json")
    files = {}
    for path in [prefix / "bin/rustc", prefix / "bin/cargo",
                 *list((prefix / "lib").rglob("*.rlib")), *list((prefix / "lib").rglob("*.so*"))]:
        if path.is_file():
            files[str(path.relative_to(prefix))] = digest(path)
    receipt = {
        "rustc_commit": head,
        "rustc_verbose_version": run(str(prefix / "bin/rustc"), "-Vv"),
        "cargo_verbose_version": run(str(prefix / "bin/cargo"), "-Vv"),
        "target": fork["target"],
        "bootstrap_manifest_sha256": digest(manifest),
        "cargo_archive_sha256": cargo["sha256"],
        "files": files,
    }
    if receipt["rustc_verbose_version"] != version:
        raise ValueError("Exported compiler identity changed")
    (metadata / "fork-installation.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(f"Rust fork prefix: {prefix}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("source", "prefix", "prebuilt"):
        parser.add_argument(key, type=Path)
    arguments = parser.parse_args()
    try:
        export(arguments.source, arguments.prefix, arguments.prebuilt)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Rust fork export failed: {error}\n")


if __name__ == "__main__":
    main()
