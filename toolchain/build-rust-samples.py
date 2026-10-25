#!/usr/bin/env python3
"""Build opt-in Rust qualification bundles with the existing SDK.

PREFIX SDK OUTPUT WORK must be absolute paths. OUTPUT must be fresh; WORK keeps
Cargo's cache and content-keyed target directories. --fetch allows the closed
internal Cargo source to seed missing inputs; normal builds are frozen/offline.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
RUST = HERE / "rust"
SAMPLES = RUST / "samples"


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def build(arguments):
    prefix, sdk, output, work = (getattr(arguments, key) for key in
                                 ("prefix", "sdk", "output", "work"))
    if any(not p.is_absolute() for p in (prefix, sdk, output, work)):
        raise ValueError("PREFIX, SDK, OUTPUT and WORK must be absolute")
    if output.exists() or output.is_symlink():
        raise ValueError("OUTPUT must be fresh; select a new output directory")
    pin = json.loads((RUST / "host-toolchain.json").read_text())
    installed = prefix / "share/pyxis-rust/host-toolchain.json"
    if json.loads(installed.read_text()) != pin:
        raise ValueError("Rust prefix does not match toolchain/rust/host-toolchain.json")
    environment = os.environ.copy()
    for key in list(environment):
        if (key in {"RUSTC_BOOTSTRAP", "RUSTFLAGS", "CARGO_ENCODED_RUSTFLAGS",
                    "CARGO_BUILD_TARGET", "CARGO_BUILD_RUSTFLAGS", "RUSTC_WRAPPER",
                    "RUSTC_WORKSPACE_WRAPPER"} or key.startswith("CARGO_PROFILE_")
                or key.startswith("CARGO_TARGET_")):
            environment.pop(key)
    environment["RUSTC"] = str(prefix / "bin/rustc")
    identity = subprocess.check_output([environment["RUSTC"], "-Vv"], text=True,
                                        env=environment)
    if "commit-hash: " + pin["rustc_commit"] not in identity:
        raise ValueError("Installed rustc identity differs from its pin")
    paths = [sdk / "manifest.txt", sdk / "share/pyxis.mk"]
    paths += [sdk / "sysroot/usr/lib" / n for n in
              ("crt0.o", "libc.a", "libterm.a", "libpyxis.a",
               "libclang_rt.builtins.a", "pyxis.ld")]
    paths += [RUST / n for n in ("x86_64-unknown-pyxis.json", "pyxis-link.py",
                                 "link-flags.mk", "host-toolchain.json")]
    environment["PYXIS_RUST_SDK"] = str(sdk)
    link_settings = subprocess.check_output(
        ["make", "--no-print-directory", "-sf", str(RUST / "link-flags.mk"),
         "print-rust-link"], env=environment, text=True).splitlines()
    compiler = shlex.split(link_settings[0])
    executable = shutil.which(compiler[0], path=environment.get("PATH"))
    if not executable:
        raise ValueError("Selected SDK compiler is unavailable")
    runtime = Path(subprocess.check_output(compiler + ["-print-libgcc-file-name"],
                                          env=environment, text=True).strip())
    if digest(runtime) != digest(sdk / "sysroot/usr/lib/libclang_rt.builtins.a"):
        raise ValueError("Selected compiler runtime differs from SDK; rebuild the SDK")
    paths += [Path(executable).resolve(), runtime]
    inputs = {str(p): digest(p) for p in paths}
    inputs["effective_sdk_link_settings"] = hashlib.sha256(
        json.dumps(link_settings).encode()).hexdigest()
    key = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    target = work / "targets" / key
    work.mkdir(parents=True, exist_ok=True)
    environment.update({
        "CARGO_HOME": str(work / "cargo-home"),
        "CARGO_TARGET_DIR": str(target),
        "CARGO_TARGET_X86_64_UNKNOWN_PYXIS_LINKER": str(RUST / "pyxis-link.py"),
        "CARGO_TARGET_X86_64_UNKNOWN_PYXIS_RUSTFLAGS": "-Cembed-bitcode=no -Clto=off",
        "PYXIS_RUST_SDK": str(sdk),
        "PYXIS_RUST_DEBUG_DIR": str(target / "debug-elf"),
        "PYXIS_RUST_LINK_TRACE": str(work / "link-commands.jsonl"),
    })
    cargo = str(prefix / "bin/cargo")
    mode = "--locked" if arguments.fetch else "--frozen"
    command = [cargo, "build", mode, "--release", "-p", "rust-hello", "-p",
               "rust-raster", "--target", str(RUST / "x86_64-unknown-pyxis.json"),
               "-Zbuild-std=core,alloc", "-Zjson-target-spec"]
    subprocess.run(command, cwd=SAMPLES, env=environment, check=True)
    subprocess.run([cargo, "build", mode, "--release", "-p", "rust-raster-host",
                    "--target", pin["host"]],
                   cwd=SAMPLES, env=environment, check=True)
    reference = subprocess.check_output([str(target / pin["host"] / "release/rust-raster-host")],
                                         text=True).strip()
    provenance = {
        "producer": "pyxis-rust-samples", "rustc": identity.strip(),
        "host_toolchain": pin, "sdk_inputs_sha256": inputs,
        "source_repository": "https://git.internal/PyxisOS/pyxis-os",
        "source_revision": subprocess.check_output(["git", "rev-parse", "HEAD"],
                                                    cwd=ROOT, text=True).strip(),
        "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"],
                                                      cwd=ROOT, text=True)),
        "cargo_lock_sha256": digest(SAMPLES / "Cargo.lock"),
        "rust_library_lock_sha256": digest(prefix / "lib/rustlib/src/rust/library/Cargo.lock"),
        "target_spec_sha256": digest(RUST / "x86_64-unknown-pyxis.json"),
        "host_reference": reference, "lto": False,
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="rust-samples-", dir=output.parent) as temporary:
        stage = Path(temporary) / "stage"
        stage.mkdir()
        for name, source in (("rust-hello", "hello"), ("rust-raster", "raster")):
            bundle = stage / (name + ".pxb")
            binary = bundle / "app/bin" / (name + ".pxe")
            binary.parent.mkdir(parents=True)
            compiled = target / "x86_64-unknown-pyxis/release" / name
            if compiled.read_bytes()[:8] != b"PXE1FLAT":
                raise ValueError(f"Compiler did not produce a P1F image: {name}")
            shutil.copyfile(compiled, binary)
            shutil.copyfile(SAMPLES / source / "manifest.json", bundle / "manifest.json")
            notices = bundle / "app/share/licenses"
            shutil.copytree(SAMPLES / "licenses", notices / "rust-crates")
            shutil.copyfile(ROOT / "LICENSE", notices / "Pyxis-LICENSE")
            rust_notices = notices / "rust"
            rust_notices.mkdir()
            shutil.copyfile(prefix / "share/doc/rust/COPYRIGHT-library.html",
                            rust_notices / "COPYRIGHT-library.html")
            shutil.copytree(prefix / "share/doc/rust/licenses", rust_notices / "licenses")
            shutil.copyfile(prefix / "lib/rustlib/src/rust/library/compiler-builtins/LICENSE.txt",
                            rust_notices / "compiler-builtins-LICENSE.txt")
            shutil.copytree(sdk / "share/licenses", notices / "sdk")
            shutil.copytree(sdk / "share/toolchain", notices / "pyxis-llvm")
            for filename in ("Cargo.lock", "dependencies.json"):
                shutil.copyfile(SAMPLES / filename, notices / filename)
            sources = bundle / "app/share/source"
            shutil.copytree(SAMPLES, sources / "samples", ignore=shutil.ignore_patterns("target", ".git"))
            for filename in ("x86_64-unknown-pyxis.json", "pyxis-link.py", "link-flags.mk",
                             "host-toolchain.json"):
                shutil.copyfile(RUST / filename, sources / filename)
            shutil.copyfile(Path(__file__), sources / "build-rust-samples.py")
            facts = bundle / "app/share/pyxis-rust"
            facts.mkdir(parents=True)
            shutil.copyfile(sdk / "manifest.txt", facts / "sdk-manifest.txt")
            facts.joinpath("provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
        shutil.copytree(target / "debug-elf", stage / "debug")
        (stage / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
        stage.rename(output)
    print(reference)
    print(f"Rust qualification bundles: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("prefix", "sdk", "output", "work"):
        parser.add_argument(key, type=Path)
    parser.add_argument("--fetch", action="store_true")
    try:
        build(parser.parse_args())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Rust sample build failed: {error}\n")


if __name__ == "__main__":
    main()
