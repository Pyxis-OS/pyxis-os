#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Publish OpenArena's verified source downloads beside its binary package."""

import argparse
import os
from pathlib import Path
import re
import shutil

from pxpkg import CHUNK, PackageError, RepositoryLock, file_digest, sync_directory, temporary_file


def copy_immutable(source, target, expected):
    if source.is_symlink() or not source.is_file() or file_digest(source)[0] != expected:
        raise PackageError(f"source checksum/type mismatch: {source}")
    if target.exists() or target.is_symlink():
        if target.is_symlink() or not target.is_file() or file_digest(target)[0] != expected:
            raise PackageError(f"published source has different bytes: {target}")
        return
    fd, temporary = temporary_file(target.parent, target.name)
    try:
        with source.open("rb") as original, os.fdopen(fd, "wb") as output:
            shutil.copyfileobj(original, output, CHUNK)
            output.flush()
            os.fchmod(output.fileno(), 0o644)
            os.fsync(output.fileno())
        if file_digest(temporary)[0] != expected:
            raise PackageError(f"source changed while copying: {source}")
        os.link(temporary, target)
        sync_directory(target.parent)
    finally:
        temporary.unlink(missing_ok=True)


def publish(stage, repository):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+\-]*", stage.name):
        raise PackageError("source version must be a plain directory name")
    sums = stage / "SHA256SUMS"
    if sums.is_symlink() or not sums.is_file():
        raise PackageError(f"missing regular source inventory: {sums}")
    files = {}
    for line in sums.read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._+\-]*)", line)
        if not match or match[2] in files or match[2] in {"README.txt", "SHA256SUMS"}:
            raise PackageError(f"invalid source inventory line: {line!r}")
        files[match[2]] = match[1]
    if not files:
        raise PackageError("empty source inventory")
    # Verify the entire set before publishing any source file.
    for name, digest in files.items():
        source = stage / name
        if source.is_symlink() or not source.is_file() or file_digest(source)[0] != digest:
            raise PackageError(f"source checksum/type mismatch: {source}")
    for name in ("README.txt", "SHA256SUMS"):
        source = stage / name
        if source.is_symlink() or not source.is_file():
            raise PackageError(f"missing regular source notice: {source}")
        files[name] = file_digest(source)[0]
    if not repository.exists():
        repository.mkdir(parents=True)
        sync_directory(repository.parent)
    with RepositoryLock(repository, exclusive=True):
        target = repository
        for part in ("sources", "openarena", stage.name):
            target = target / part
            if not target.exists():
                target.mkdir()
                sync_directory(target.parent)
        for name, digest in files.items():
            copy_immutable(stage / name, target / name, digest)
    print(f"{target}: verified {len(files) - 2} separate source downloads; not installed with the game")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("repository", type=Path)
    args = parser.parse_args()
    try:
        publish(args.stage, args.repository)
    except (OSError, ValueError, PackageError) as error:
        parser.exit(1, f"openarena-sources: {error}\n")


if __name__ == "__main__":
    main()
