#!/usr/bin/env python3
"""Freeze reviewed signed-index floors from controlled signing trees.

Usage: tools/freeze-repository-floors.py --repository NAME URL TREE [--repository ...]
       [--check | --write]

TREE is chronium's controlled signing directory, not a downloaded copy. The
published URL is used only to compare bytes; it never supplies a floor alone.
"""

import argparse
from datetime import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

import pxpkg


ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "packages/signing/repository-floors.lua"
ANCHOR = ROOT / "packages/signing/pyxis-signing-ca.pem"
MAX_INDEX = 1024 * 1024
MAX_CERT = 8192
MAX_SIGNATURE = 4096


class Refuse(Exception):
    pass


def require(condition, message):
    if not condition:
        raise Refuse(message)


def bounded_file(path, limit):
    with path.open("rb") as stream:
        data = stream.read(limit + 1)
    require(len(data) <= limit, f"{path} exceeds {limit} bytes")
    return data


def published(base, path, limit):
    result = subprocess.run(("curl", "--fail", "--silent", "--show-error",
                             "--max-time", "15", "--max-filesize", str(limit + 1),
                             "--proto", "=https", "--write-out", "\n%{http_code}",
                             base + path),
                            capture_output=True, check=False)
    require(result.returncode == 0,
            f"published {path} could not be read: {result.stderr.decode(errors='replace').strip()}")
    require(len(result.stdout) >= 4 and result.stdout[-4] == 10 and
            result.stdout[-3:] == b"200", f"published {path} did not return HTTP 200")
    data = result.stdout[:-4]
    require(len(data) <= limit, f"published {path} exceeds {limit} bytes")
    return data


def run(*arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, check=False)
    require(result.returncode == 0,
            f"{' '.join(arguments[:3])} failed: {(result.stderr or result.stdout).strip()}")
    return result.stdout


def verified_entry(name, base, tree):
    require(pxpkg.ID_PATTERN.fullmatch(name) is not None, f"invalid repository name {name}")
    uri = urlsplit(base)
    require(uri.scheme == "https" and uri.netloc and not uri.username and
            not uri.password and not uri.query and not uri.fragment and base.endswith("/") and
            len(base) <= 2048, f"{name}: need a canonical HTTPS base URL")
    current = bounded_file(tree / "current", 32)
    serial = pxpkg.read_current(tree)
    require(serial and serial <= pxpkg.SERIAL_MAX, f"{name}: no bounded current serial")
    path = f"index/{serial}.lua"
    index = bounded_file(tree / path, MAX_INDEX)
    signature = bounded_file(tree / f"{path}.sig", MAX_SIGNATURE)
    header, _ = pxpkg.snapshot_check(pxpkg.parse_literal(index))
    require(header["repository"] == name and header["serial"] == serial,
            f"{name}: the controlled index has the wrong identity")
    signer_name = header.get("signer")
    require(signer_name is not None, f"{name}: the controlled index is unsigned")
    signer = bounded_file(tree / signer_name, MAX_CERT)
    for relative, local, limit in (("current", current, 32), (path, index, MAX_INDEX),
                                   (f"{path}.sig", signature, MAX_SIGNATURE),
                                   (signer_name, signer, MAX_CERT)):
        require(published(base, relative, limit) == local,
                f"{name}: controlled {relative} differs from published bytes")
    signed_at = header["signed"]
    at = int(datetime.fromisoformat(signed_at.replace("Z", "+00:00")).timestamp())
    with tempfile.TemporaryDirectory(prefix="pyxis-floor-") as temporary:
        directory = Path(temporary)
        certificate = directory / "signer.pem"
        public_key = directory / "signer.pub"
        index_path = directory / "index.lua"
        signature_path = directory / "index.sig"
        certificate.write_bytes(signer)
        index_path.write_bytes(index)
        signature_path.write_bytes(signature)
        run("openssl", "verify", "-x509_strict", "-partial_chain", "-purpose", "codesign",
            "-attime", str(at), "-CAfile", str(ANCHOR), str(certificate))
        constraints = run("openssl", "x509", "-in", str(certificate), "-noout",
                          "-ext", "basicConstraints")
        require("CA:FALSE" in constraints, f"{name}: signer is not CA:FALSE")
        description = run("openssl", "x509", "-in", str(certificate), "-noout", "-text")
        require("Digital Signature" in description and "Code Signing" in description and
                "prime256v1" in description, f"{name}: signer lacks P-256 code-signing use")
        public_key.write_text(run("openssl", "x509", "-in", str(certificate), "-pubkey",
                                  "-noout"))
        run("openssl", "dgst", "-sha256", "-verify", str(public_key),
            "-signature", str(signature_path), str(index_path))
    return {"name": name, "url": base, "signed": True, "serial": serial,
            "signed_at": signed_at, "sha256": hashlib.sha256(index).hexdigest()}


def render(entries):
    lines = ["-- Verified against chronium's controlled signing trees and published copies.",
             "-- Refresh with tools/freeze-repository-floors.py before a release.",
             "return {", "  version = 1,", "  repositories = {"]
    for entry in entries:
        lines.append("    { " + ", ".join(
            f"{key} = {json.dumps(value) if isinstance(value, str) else str(value).lower()}"
            for key, value in entry.items()) + " },")
    return "\n".join(lines + ["  },", "}", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", nargs=3, action="append", required=True,
                        metavar=("NAME", "URL", "TREE"))
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--check", action="store_true")
    action.add_argument("--write", action="store_true")
    args = parser.parse_args()
    require(len(args.repository) <= 8, "at most eight repository floors")
    entries = []
    for name, base, directory in args.repository:
        require(not any(entry["name"] == name or entry["url"] == base for entry in entries),
                f"duplicate floor identity for {name}")
        entries.append(verified_entry(name, base, Path(directory)))
    result = render(entries)
    if args.check:
        require(OUTPUT.read_text() == result, f"{OUTPUT} differs from verified publish state")
    else:
        with tempfile.NamedTemporaryFile(mode="w", dir=OUTPUT.parent, prefix=".repository-floors-",
                                         delete=False) as temporary:
            staged = Path(temporary.name)
            temporary.write(result)
            temporary.flush()
            os.fsync(temporary.fileno())
        try:
            os.replace(staged, OUTPUT)
        finally:
            staged.unlink(missing_ok=True)
    for entry in entries:
        print(f"{entry['name']}: serial {entry['serial']} {entry['sha256']}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, pxpkg.PackageError, Refuse) as error:
        sys.exit(f"freeze-repository-floors: {error}")
