#!/usr/bin/env python3
"""Inventory SDK inputs and stage the native hosted-tool bundles."""

import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile

P1F_MAGIC = 0x54414C4631455850
P1F_EXECUTE = 1
P1F_WRITE = 2
P1F_READ = 4
PAGE_SIZE = 4096
USER_LIMIT = 1 << 47
IMAGE_LIMIT = 128 * 1024 * 1024
SPAN_LIMIT = 256 * 1024 * 1024
STACK_BYTES = 8 * 1024 * 1024
TOOL_COMMANDS = {
    "lld": ("lld", "ld.lld"),
    "clang": ("clang", "clang++", "clang-cpp"),
    "clangd": ("clangd",),
}


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def inventory(root):
    entries = []

    def visit(directory, ancestors):
        resolved = directory.resolve(strict=True)
        if resolved in ancestors:
            raise ValueError(f"Directory symlink cycle: {directory}")
        for path in sorted(directory.iterdir()):
            name = path.relative_to(root).as_posix()
            if path.is_dir():
                entries.append({"path": name, "type": "directory"})
                visit(path, ancestors | {resolved})
            elif path.is_file():
                entries.append({"path": name, "type": "file", "bytes": path.stat().st_size,
                                "sha256": sha256(path)})
            else:
                raise ValueError(f"Unsupported or broken input: {path}")

    visit(root, set())
    return entries


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def image_sizes(path):
    size = path.stat().st_size
    if size > IMAGE_LIMIT or size < 24:
        raise ValueError(f"P1F serialized image outside loader limit: {path}")
    with path.open("rb") as image:
        magic, entry, count = struct.unpack("<3Q", image.read(24))
        if magic != P1F_MAGIC or not count or count > (size - 24) // 32:
            raise ValueError(f"Invalid P1F header: {path}")
        segments = []
        previous_end = PAGE_SIZE
        payload = 24 + count * 32
        executable_entry = False
        for _ in range(count):
            address, file_bytes, memory_bytes, flags = struct.unpack("<4Q", image.read(32))
            end = address + memory_bytes
            rounded = (end + PAGE_SIZE - 1) // PAGE_SIZE * PAGE_SIZE
            if (address % PAGE_SIZE or address < previous_end or address >= USER_LIMIT
                    or not memory_bytes or end > USER_LIMIT or file_bytes > memory_bytes
                    or flags & ~(P1F_READ | P1F_WRITE | P1F_EXECUTE)
                    or not flags & P1F_READ
                    or flags & (P1F_WRITE | P1F_EXECUTE) == (P1F_WRITE | P1F_EXECUTE)):
                raise ValueError(f"Invalid P1F segment: {path}")
            executable_entry |= bool(flags & P1F_EXECUTE and address <= entry < end)
            segments.append({"virtual_address": address, "file_bytes": file_bytes,
                             "memory_bytes": memory_bytes, "backing_bytes": rounded - address,
                             "flags": flags})
            previous_end = rounded
            payload += file_bytes
        span = previous_end - segments[0]["virtual_address"]
        if payload != size or not executable_entry or span > SPAN_LIMIT:
            raise ValueError(f"Invalid P1F payload, entry or mapped span: {path}")
    return {"serialized_bytes": size, "mapped_span_bytes": span, "segments": segments,
            "image_backing_bytes": sum(s["backing_bytes"] for s in segments),
            "eager_stack_bytes": STACK_BYTES}


def copy_tree(source, destination):
    # Native directories have no symlinks. Expand host aliases into regular files.
    shutil.copytree(source, destination, symlinks=False)


def stage(sdk, resource, source, work, output, compiler_provenance, toolchain):
    staging = work / "staging"
    payload = staging / "payload"
    payload.mkdir(parents=True)
    sizes = {}
    # Clang's declared linker root embeds the same qualified leaf LLD bundle.
    # clangd is built in its own threads-on tree.
    for tool, build in (("lld", "target"), ("clang", "target"), ("clangd", "clangd-target")):
        bundle = payload / f"{tool}.pxb"
        app = bundle / "app"
        (app / "bin").mkdir(parents=True)
        binary = app / "bin" / f"{tool}.pxe"
        shutil.copyfile(work / build / "bin" / tool, binary)
        sizes[tool] = image_sizes(binary)
        copy_tree(resource, app / "clang_resources")
        copy_tree(sdk / "sysroot", app / "sysroot")
        copy_tree(sdk / "share/licenses", app / "share/licenses")
        copy_tree(sdk / "share/toolchain", app / "share/toolchain")
        shutil.copyfile(sdk / "manifest.txt", app / "share/sdk-manifest.txt")
        resource_dirs = {"clang_resources": "clang_resources", "sysroot": "sysroot"}
        grants = [
            {"name": name, "resource": name, "rights": rights, "required": True}
            for name, rights in (("memory", ["manage"]), ("threads", ["manage"]),
                                 ("clock", ["read", "sleep"]), ("random", ["read"]))]
        if tool == "clang":
            (app / "linker").mkdir()
            copy_tree(payload / "lld.pxb", app / "linker/lld.pxb")
            resource_dirs["linker"] = "linker"
            grants.append({"name": "launcher", "resource": "launcher",
                           "rights": ["launch"], "required": False})
        write_json(bundle / "manifest.json", {
            "format": 1, "id": f"org.pyxis.{tool}", "entry": f"bin/{tool}.pxe",
            "commands": {name: f"bin/{tool}.pxe" for name in TOOL_COMMANDS[tool]},
            "stack_bytes": STACK_BYTES,
            "resource_dirs": resource_dirs, "grants": grants})

    metadata = payload / "metadata"
    metadata.mkdir()
    shutil.copyfile(work / "sdk-input.json", metadata / "sdk-files.json")
    shutil.copyfile(sdk / "manifest.txt", metadata / "sdk-manifest.txt")
    for build in ("generators", "target", "clangd-target"):
        shutil.copyfile(work / build / "CMakeCache.txt", metadata / f"{build}-cmake-cache.txt")
        for compiler_info in sorted((work / build / "CMakeFiles").glob("*/CMake*Compiler.cmake")):
            shutil.copyfile(compiler_info, metadata / f"{build}-{compiler_info.name}")
    copy_tree(compiler_provenance, metadata / "installed-toolchain")
    shutil.copyfile(source / "llvm/LICENSE.TXT", metadata / "LLVM-LICENSE.TXT")
    write_json(metadata / "image-sizes.json", sizes)
    compiler_bin = compiler_provenance.resolve().parents[1] / "bin"
    installed_tools = {}
    for name in ("clang", "clang++", "ld.lld", "llvm-ar", "llvm-ranlib"):
        executable = compiler_bin / name
        installed_tools[name] = {
            "sha256": sha256(executable),
            "version": subprocess.check_output([str(executable), "--version"], text=True).splitlines()[0]}
    write_json(metadata / "producer.json", {
        "llvm_repository": "https://git.internal/PyxisOS/pyxis-llvm.git",
        "llvm_commit": subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip(),
        "llvm_state": "clean", "sdk": str(sdk), "resource_directory": str(resource),
        "installed_tools": installed_tools,
        "host_generators": {name: sha256(work / "generators/bin" / name)
                            for name in ("llvm-tblgen", "clang-tblgen")},
        "host": subprocess.check_output(["uname", "-sm"], text=True).strip(),
        "producer_files": {name: sha256(toolchain / name)
                           for name in ("build-hosted.sh", "stage-hosted.py")},
        "image_limit_bytes": IMAGE_LIMIT, "mapped_span_limit_bytes": SPAN_LIMIT,
        "runtime_profile": "Clang/LLD workers-off, clangd threads-on; real SDK libc++/libc++abi/libunwind",
        "publication": "manual immutable revision; Clang optional declared launcher; LLD and clangd leaves"})
    # Archive modes and timestamps do not depend on umask or source checkout dates.
    for path in [payload, *payload.rglob("*")]:
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError(f"Payload must contain only regular files/directories: {path}")
        path.chmod(0o755 if path.is_dir() or path.suffix == ".pxe" else 0o644)
    files = inventory(payload)
    identity = hashlib.sha256(json.dumps(files, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    revision = "hosted-" + identity
    write_json(metadata / "payload-files.json", {"revision": revision, "entries": files})
    (metadata / "payload-files.json").chmod(0o644)
    archive = staging / "payload.tar"
    with tarfile.open(archive, "w", format=tarfile.USTAR_FORMAT) as tar:
        for path in sorted(payload.rglob("*")):
            info = tar.gettarinfo(str(path), arcname=path.relative_to(payload).as_posix())
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ""
            if path.is_file():
                with path.open("rb") as contents:
                    tar.addfile(info, contents)
            else:
                tar.addfile(info)
    shutil.copyfile(source / "llvm/LICENSE.TXT", staging / "LICENSE")
    (staging / "payload.tar.sha256").write_text(f"{sha256(archive)}  payload.tar\n")
    # The ordinary package packer verifies these real bundle command exports.
    recipe = [
        "return {", '  id = "clang-hosted",', f"  version = {json.dumps(identity)},",
        '  kind = "toolchain",', "  depends = {},", "  commands = {",
    ]
    for tool, names in TOOL_COMMANDS.items():
        for name in names:
            recipe.append(f"    [{json.dumps(name)}] = {{ path = {json.dumps(tool + '.pxb')}, "
                          f"command = {json.dumps(name)} }},")
    # LICENSE sits beside the recipe, outside payload/, so it is not part of the identity.
    recipe += ['  },', '  entries = {', '    { tree = "payload", at = "" },',
               '    { file = "LICENSE", at = "LICENSE" },', '  },', '}']
    (staging / "clang-hosted.lua").write_text("\n".join(recipe) + "\n")
    destination = output / revision
    # GNU mv's no-clobber also protects an empty existing revision directory.
    subprocess.run(["mv", "-T", "--no-clobber", str(staging), str(destination)], check=True)
    if staging.exists():
        raise ValueError(f"Published revision already exists: {destination}")
    print(destination)


if __name__ == "__main__":
    try:
        if len(sys.argv) == 3 and sys.argv[1] == "inventory":
            print(json.dumps(inventory(Path(sys.argv[2])), indent=2, sort_keys=True))
        elif len(sys.argv) == 9 and sys.argv[1] == "stage":
            stage(*(Path(argument) for argument in sys.argv[2:]))
        else:
            raise ValueError("Usage: stage-hosted.py inventory SDK | stage SDK RESOURCE SOURCE WORK OUTPUT COMPILER_PROVENANCE TOOLCHAIN")
    except (ValueError, OSError, struct.error, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
