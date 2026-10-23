#!/usr/bin/env python3
"""Export the built target archive closure and host llvm-config as one prefix."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import importlib.util

spec = importlib.util.spec_from_file_location("stage_hosted", Path(__file__).with_name("stage-hosted.py"))
hosted = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hosted)
COMPONENTS = ["bitwriter", "engine", "mcdisassembler", "core", "executionengine",
              "scalaropts", "transformutils", "instcombine", "native", "mcjit"]


def main():
    if len(sys.argv) != 5:
        raise ValueError("Usage: stage-llvm-development.py SDK SOURCE WORK OUTPUT")
    sdk, source, work, output = [Path(arg).resolve(strict=True) for arg in sys.argv[1:]]
    staging = output / ".llvm-development-staging"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir()
    subprocess.run(["cmake", "--install", str(work / "target"), "--prefix", str(staging),
                    "--component", "llvm-headers"], check=True)
    (staging / "bin").mkdir()
    (staging / "lib").mkdir(exist_ok=True)
    shutil.copy2(work / "llvm-config", staging / "bin/llvm-config")
    libraries = sorted((work / "target/lib").glob("libLLVM*.a"))
    if not libraries:
        raise ValueError("No target archives built")
    for library in libraries:
        shutil.copy2(library, staging / "lib" / library.name)
    config = staging / "bin/llvm-config"

    def query(*options):
        return subprocess.check_output([str(config), *options], text=True).strip()

    if query("--host-target") != "x86_64-unknown-pyxis" or query("--has-rtti") != "NO":
        raise ValueError("Configuration tool reports wrong target or RTTI")
    # This traverses the actual generated dependency graph, and fails if any
    # component's archive is absent. It does not substitute host libraries.
    reported = query("--link-static", "--libfiles", *COMPONENTS).split()
    if not reported or any(Path(name).parent != staging / "lib" for name in reported):
        raise ValueError("Configuration tool escapes the target archive directory")
    metadata = staging / "share/pyxis-llvm-development"
    metadata.mkdir(parents=True)
    shutil.copy2(source / "llvm/LICENSE.TXT", metadata / "LICENSE.TXT")
    shutil.copytree(sdk / "share/licenses", metadata / "sdk-licenses")
    # These Support sources carry embedded third-party terms in addition to
    # the LLVM licence. Keep the original material, not a rewritten summary.
    support = source / "llvm/lib/Support"
    notices = list(support.glob("reg*")) + [support / name for name in
        ("xxhash.cpp", "MD5.cpp", "SHA1.cpp", "SHA256.cpp", "ConvertUTF.cpp",
         "UnicodeNameToCodepointGenerated.cpp", "BLAKE3/LICENSE")]
    notices.append(source / "third-party/siphash/include/siphash/SipHash.h")
    for path in notices:
        if path.is_file():
            destination = metadata / "source-notices" / path.relative_to(source)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    shutil.copy2(work / "sdk-input.json", metadata / "sdk-input.json")
    hosted.write_cmake_cache(work / "target/CMakeCache.txt", metadata / "target-cmake.json")
    information = {
        "repository": "https://git.internal/PyxisOS/pyxis-llvm.git",
        "commit": subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip(),
        "version": query("--version"), "components": COMPONENTS,
        "archives": [library.name for library in libraries],
        "threads": True, "rtti": False, "exceptions": False,
        "system_libraries": query("--link-static", "--system-libs", *COMPONENTS),
        "host_config_sha256": hosted.sha256(config),
        "producer_sha256": {name: hosted.sha256(Path(__file__).parent / name) for name in
                            ("build-llvm-development.sh", "stage-llvm-development.py",
                             "llvm-development/revision", "llvm-development/CMakeLists.txt",
                             "llvm-development/configure-tool.py", "llvm-development/object-check.cpp")},
    }
    (metadata / "build.json").write_text(json.dumps(information, sort_keys=True, indent=2) + "\n")
    inventory = hosted.inventory(staging)
    identity = hashlib.sha256(json.dumps(inventory, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    (metadata / "inventory.json").write_text(json.dumps(inventory, sort_keys=True, indent=2) + "\n")
    destination = output / ("llvm-development-" + identity)
    if destination.exists():
        if hosted.inventory(destination) != hosted.inventory(staging):
            raise ValueError("Existing immutable prefix differs")
        shutil.rmtree(staging)
    else:
        staging.rename(destination)
    archive = output / (destination.name + ".tar")
    # The prefix has no symlinks; deterministic uncompressed host transfer.
    with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as tar:
        for path in sorted(destination.rglob("*")):
            info = tar.gettarinfo(str(path), arcname=str(path.relative_to(destination)))
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ""
            if path.is_file():
                with path.open("rb") as stream:
                    tar.addfile(info, stream)
            else:
                tar.addfile(info)
    archive.with_suffix(".tar.sha256").write_text(hosted.sha256(archive) + "  " + archive.name + "\n")
    print(f"LLVM_DEVELOPMENT COMPLETE prefix={destination} archives={len(libraries)}")


if __name__ == "__main__":
    main()
