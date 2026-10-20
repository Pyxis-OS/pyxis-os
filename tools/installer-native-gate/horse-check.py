#!/usr/bin/env python3
"""Read-only native gate checks on horse (CachyOS); no stick writes or mounts.

Only the explicit host state directory is written. Target admission still
requires the installer's own Review on vela.
"""
import argparse
import importlib.util
import hashlib
import contextlib
import io
import os
import re
import json
from pathlib import Path
import shutil
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent


def preflight():
    for name in ("lsblk", "blockdev", "sha256sum", "sudo"):
        if shutil.which(name) is None:
            raise ValueError("missing base-system command: " + name)
    probe = subprocess.run([str(HERE / "fat32-inspect"), "--help"],
                           text=True, capture_output=True, timeout=15)
    if "usage: fat32-inspect" not in probe.stdout + probe.stderr:
        raise ValueError("supplied inspector cannot run: " + (probe.stderr or probe.stdout).strip())
    probe = subprocess.run([str(HERE / "media-consent"), "--help"],
                           text=True, capture_output=True, timeout=15)
    if probe.returncode or "usage: media-consent" not in probe.stdout:
        raise ValueError("supplied read-only consent inspector cannot run")
    print("HOST CHECK PASS: Python stdlib, base commands, read-only FAT and consent inspectors")


def media_module():
    spec = importlib.util.spec_from_file_location("media_check", HERE / "check-media.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def identity_record(device, size):
    return {"media": "green-reference-64gb", "bytes": size,
            "identity_sha256": hashlib.sha256(device.encode("utf-8")).hexdigest()}


def check_record(record, device, size):
    if not isinstance(record, dict):
        raise ValueError("invalid recorded identity")
    expected = identity_record(device, size)
    digest = record.get("identity_sha256")
    if "device" in record:
        legacy = record["device"]
        if not isinstance(legacy, str):
            raise ValueError("invalid recorded identity")
        legacy_digest = hashlib.sha256(legacy.encode("utf-8")).hexdigest()
        if digest is not None and digest != legacy_digest:
            raise ValueError("inconsistent recorded identity")
        digest = legacy_digest
    if (record.get("media") != expected["media"] or
            type(record.get("bytes")) is not int or record["bytes"] != size or
            digest != expected["identity_sha256"]):
        raise ValueError("recorded target differs; refusing to replace its identity")
    return expected


def record_target(path, device, size):
    expected = identity_record(device, size)
    serialized = json.dumps(expected, sort_keys=True) + "\n"
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        recorded = json.loads(path.read_text())
        check_record(recorded, device, size)
        if "device" in recorded:
            # Migrate only an identical old record, removing its raw by-id.
            temporary = path.with_name(path.name + ".redacted")
            fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            try:
                with os.fdopen(fd, "w") as output:
                    output.write(serialized)
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(temporary, path)
            finally:
                temporary.unlink(missing_ok=True)
        print("PASS: horse target already recorded, identical (identity not printed)")
        return
    with os.fdopen(fd, "w") as output:
        output.write(serialized)
        output.flush()
        os.fsync(output.fileno())
    print("PASS: horse target recorded (identity digest only; no serial stored)")


def classify(device, size):
    # Resolve the checked alias so the C adapter can refuse symlink sources.
    try:
        result = subprocess.run([str(HERE / "media-consent"),
                                 str(Path(device).resolve(strict=True)), str(size)],
                                capture_output=True, text=True, timeout=120)
        lines = result.stdout.splitlines()
    except subprocess.TimeoutExpired:
        result, lines = None, []
    if (result is None or result.returncode or len(lines) != 3 or
            lines[0] not in ("blank", "marked", "vetoing")):
        kind, reason, counts = "vetoing", "consent unreadable or inspection failed", "counts unavailable"
    else:
        kind, reason, counts = lines
    print(f"MEDIA CLASS: {kind} — {reason}; {counts}")
    if kind == "vetoing":
        print("STOP: do not move to vela for Install. No media changes made.")
        print("Fix for this disposable gate (DESTRUCTIVE; owner only): recheck the same "
              "sole 64 GB USB identity, then sudo wipefs -a -- <EXACT-BY-ID> and "
              "sudo dd if=/dev/zero of=<EXACT-BY-ID> bs=1M count=1 conv=fsync; "
              "rerun identity. Never substitute /dev/sdX or publish the by-id/serial.")
        raise ValueError("media vetoes this fresh-Install gate")


def redact(text):
    return re.sub(r"/dev/disk/by-id/[^\s'\"),]+", "[USB identity redacted]", text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("phase", choices=("identity", "final"))
    parser.add_argument("--state-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        if not args.state_dir.is_absolute() or str(args.state_dir).startswith("/dev/"):
            raise ValueError("state-dir must be an absolute horse directory, outside /dev")
        preflight()
        target_file = args.state_dir / "target.json"
        if args.phase == "identity":
            media = media_module()
            device, size = media.discover()
            media.check(argparse.Namespace(device=device, expected_bytes=size))
            args.state_dir.mkdir(parents=True, exist_ok=True)
            record_target(target_file, device, size)
            classify(device, size)
            with contextlib.redirect_stdout(io.StringIO()):
                media.check(argparse.Namespace(device=device, expected_bytes=size))
            print("PASS: identity and Install consent checked; move this stick to vela")
            return 0
        target = json.loads(target_file.read_text())
        media = media_module()
        device, size = media.discover()
        check_record(target, device, size)
        if "device" in target:
            record_target(target_file, device, size)
        result = subprocess.run([sys.executable, str(HERE / "check-publication.py"),
                        "--device", device, "--expected-bytes", str(size),
                        "--expected-install", str(HERE / "expected-install.json"),
                        "--require-previous"], text=True, capture_output=True)
        report = redact(result.stdout + result.stderr)
        (args.state_dir / "final-readback.txt").write_text(report)
        print(report, end="", flush=True)
        if result.returncode:
            raise ValueError("final readback refused; report saved on horse")
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print("FAIL: " + redact(str(error)), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
