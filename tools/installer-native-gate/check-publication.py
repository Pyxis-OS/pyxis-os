#!/usr/bin/env python3
"""Read-only quiescent-ESP hash report, not publication or boot authority.

Native use repeats check-media.py. --image is ONLY for disposable host fixtures.
--before captures explicitly named foreign files and any existing rescue bytes;
--after compares those snapshots. Neither mode ever creates an ESP sentinel.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import selectors
import stat
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True

HERE = Path(__file__).resolve().parent
HEX = re.compile(r"[0-9a-f]{64}\Z")
GUID = re.compile(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\Z")
CONFIG = "/EFI/pyxis/config/"
RECOVERY = "/EFI/pyxis/recovery/"
MAX_PAYLOAD = 512 * 1024 * 1024


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def safe_path(value):
    require(value.startswith("/") and len(value) <= 240, "invalid absolute ESP path")
    parts = value[1:].split("/")
    require(all(part and part not in (".", "..") and len(part) < 64 and
                all(33 <= ord(c) <= 126 and c not in "\\:" for c in part)
                for part in parts), "unsafe ESP path component")
    return value


def foreign_path(value):
    safe_path(value)
    lower = value.lower()
    require(not (lower == "/efi/pyxis" or lower.startswith("/efi/pyxis/") or
                 lower == "/boot/limine" or lower.startswith("/boot/limine/") or
                 lower == "/boot/limine.conf"), "foreign baseline overlaps owned namespace")
    return value


class Inspector:
    def __init__(self, tool, source):
        self.prefix = [str(tool), "--image", str(source), "--gpt-partition", "1"]

    def read(self, path, expected=None, limit=1024 * 1024, optional=False):
        safe_path(path)
        digest = hashlib.sha256()
        retained = bytearray() if expected is None else None
        total = 0
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen(self.prefix + ["read", path], stdout=subprocess.PIPE,
                                       stderr=errors)
            try:
                selector = selectors.DefaultSelector()
                selector.register(process.stdout, selectors.EVENT_READ)
                deadline = time.monotonic() + 180
                while True:
                    remaining = deadline - time.monotonic()
                    require(remaining > 0, "read deadline exceeded")
                    require(selector.select(remaining), "read deadline exceeded")
                    chunk = os.read(process.stdout.fileno(), 1024 * 1024)
                    if not chunk:
                        break
                    total += len(chunk)
                    require(total <= limit, f"file exceeds bound: {path}")
                    digest.update(chunk)
                    if retained is not None:
                        retained.extend(chunk)
                selector.close()
                result = process.wait(timeout=max(0.01, deadline - time.monotonic()))
                errors.seek(0)
                error = errors.read(4096).decode("utf-8", "replace")
                if result and optional and "read: path not found" in error:
                    return None
                require(result == 0, f"inspector read failed: {path}")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stdout.close()
        evidence = {"size": total, "sha256": digest.hexdigest()}
        if expected is not None:
            require(evidence == expected, f"size/hash mismatch: {path}")
            return evidence
        return bytes(retained), evidence

    def check(self):
        result = subprocess.run(self.prefix + ["check"], stdout=subprocess.DEVNULL,
                                stderr=subprocess.PIPE, timeout=180)
        require(result.returncode == 0, "read-only whole-volume FAT check failed")


def text(value):
    require(value is not None, "required file absent")
    return value[0].decode("ascii")


def evidence(size, digest):
    require(size.isdecimal() and 0 < int(size) <= MAX_PAYLOAD and HEX.fullmatch(digest),
            "invalid recorded size/hash")
    return {"size": int(size), "sha256": digest}


def rescue(inspector, optional=False):
    manifest = inspector.read(RECOVERY + "ownership.record", limit=4096, optional=optional)
    if manifest is None:
        return None
    lines = text(manifest).splitlines()
    require(len(lines) == 5 and lines[0] == "PYXIS-RECOVERY 1", "unknown rescue evidence")
    disk, esp = lines[1].split(), lines[2].split()
    require(len(disk) == len(esp) == 2 and disk[0] == "disk" and esp[0] == "esp" and
            GUID.fullmatch(disk[1]) and GUID.fullmatch(esp[1]), "bad rescue GUID evidence")
    result = {"ownership.record": manifest[1]}
    for row, label, path in zip(lines[3:], ("kernel", "archive"), ("kernel.elf", "boot.arc")):
        pieces = row.split()
        require(len(pieces) == 3 and pieces[0] == label, "bad rescue payload evidence")
        result[path] = inspector.read(RECOVERY + path,
            evidence(pieces[1], pieces[2]), MAX_PAYLOAD)
    fallback = inspector.read("/boot/limine.conf", limit=4096)
    expected = ("# PYXIS STATIC RECOVERY 1\ntimeout: no\n/Pyxis immutable rescue\n"
                "  protocol: limine\n"
                f"  path: guid({esp[1]}):/EFI/pyxis/recovery/kernel.elf\n"
                f"  module_path: guid({esp[1]}):/EFI/pyxis/recovery/boot.arc\n"
                "  paging_mode: 4level\n  kaslr: no\n"
                "  cmdline: init=boot://boot-init.pxe boot.default_config=1\n")
    require(text(fallback) == expected, "static fallback does not match pinned rescue evidence")
    result["fallback"] = fallback[1]
    return result


def menu_parts(menu):
    outside = menu
    blocks = {}
    for key in ("normal", "rescue"):
        pattern = re.compile(r"(?m)^# pyxis:begin boot\." + key +
                             r"\n.*?^# pyxis:end boot\." + key + r"\n?", re.S)
        found = list(pattern.finditer(menu))
        require(len(found) == 1, "primary menu lacks one exact owned " + key + " span")
        blocks[key] = found[0].group()
        outside, count = pattern.subn("", outside)
        require(count == 1, "ambiguous primary owned spans")
    return blocks, hashlib.sha256(outside.encode("ascii")).hexdigest()


def publication(inspector, gate=None, require_previous=False):
    record = text(inspector.read(CONFIG + "config.record", limit=8192))
    rows = [line.split() for line in record.splitlines()]
    require(rows[:2] == [["PYXIS-CONFIG", "1"], ["state", "committed"]],
            "config.record is not committed format 1")
    sections = {}
    active = None
    selected = {}
    names = {"kernel": "kernel.elf", "archive": "boot.arc", "revision": "revision.txt"}
    for row in rows[2:]:
        require(row, "empty record row")
        if row[0] in ("boot", "previous-boot"):
            require(row[0] not in sections, "duplicate boot section")
            active = row[0]
            if row[1:] == ["unbound"]:
                sections[active] = None
                continue
            require(len(row) == 6 and row[1] == "bound" and row[2] in ("esp", "archive") and
                    re.fullmatch(r"[0-9a-f]{32}", row[5]), "unknown boot binding")
            safe_path("/" + row[3])
            sections[active] = {"revision": row[3], "attempt": row[5], "payloads": {}}
        elif row[0] == "payload":
            require(active and sections[active] is not None and len(row) == 5 and
                    row[1] in names, "payload outside known boot binding")
            section = sections[active]
            expected = f"EFI/pyxis/{section['revision']}/{section['attempt']}/{names[row[1]]}"
            require(row[2] == expected and row[1] not in section["payloads"],
                    "payload is not the exact owned attempt path")
            section["payloads"][row[1]] = (safe_path("/" + row[2]), evidence(row[3], row[4]))
        elif row[0] == "selected":
            require(len(row) == 4 and row[1] in ("boot.lua", "session.lua", "network.lua", "catalog.lua") and
                    row[1] not in selected, "unknown or duplicate selected configuration")
            selected[row[1]] = evidence(row[2], row[3])
    require(set(sections) == {"boot", "previous-boot"} and sections["boot"] is not None,
            "missing current/previous binding")
    if require_previous:
        require(sections["previous-boot"] is not None and
                sections["boot"]["attempt"] != sections["previous-boot"]["attempt"],
                "Update did not retain a distinct previous boot attempt")
    payload_count = 0
    for section in sections.values():
        if section is None:
            continue
        require(set(section["payloads"]) == set(names), "incomplete payload evidence")
        if gate:
            require(section["revision"] == gate["revision"], "payload revision differs from gate source")
        for label, (path, wanted) in section["payloads"].items():
            if gate:
                require(wanted == gate["payloads"][label], "payload differs from expected post-Install bytes: " + label)
            inspector.read(path, wanted, MAX_PAYLOAD)
            payload_count += 1
    require(len(selected) == 4, "incomplete selected configuration evidence")
    for name, wanted in selected.items():
        if gate:
            require(wanted == gate["selected"][name], "configuration differs from gate defaults: " + name)
        inspector.read(CONFIG + name, wanted)
    menu = text(inspector.read("/boot/limine/limine.conf", limit=1024 * 1024))
    blocks, outside = menu_parts(menu)
    kernel = sections["boot"]["payloads"]["kernel"][0]
    archive = sections["boot"]["payloads"]["archive"][0]
    for block in blocks.values():
        paths = re.findall(r"(?m)^  path: (.+)$", block)
        modules = re.findall(r"(?m)^  module_path: (.+)$", block)
        require(paths == ["boot():" + kernel] and modules == ["boot():" + archive],
                "primary owned entry does not select current payloads")
    for shadow in ("/EFI/BOOT/limine.conf", "/EFI/pyxis/limine.conf"):
        require(inspector.read(shadow, optional=True) is None, "higher-priority Limine config shadows primary")
    if gate:
        require(outside == gate["menu_outside"], "menu outside owned spans differs from expected clean Install")
        inspector.read("/EFI/BOOT/BOOTX64.EFI", gate["efi"], MAX_PAYLOAD)
        recovery = rescue(inspector)
        for name, wanted in gate["recovery"].items():
            require(recovery.get(name) == wanted, "immutable rescue differs from gate source: " + name)
    return payload_count, outside


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--device", help="native: absolute whole-device /dev/disk/by-id symlink")
    source.add_argument("--image", help="DEVELOPMENT ONLY: absolute disposable regular-image path")
    parser.add_argument("--expected-bytes", type=int)
    parser.add_argument("--expected-install", type=Path,
                        help="packet's expected default clean-Install hashes, independent of device records")
    parser.add_argument("--require-previous", action="store_true",
                        help="final Update gate requires a distinct retained previous boot attempt")
    parser.add_argument("--inspector", type=Path, default=HERE / "fat32-inspect")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--before", type=Path, help="write baseline JSON to this absolute HOST path only")
    mode.add_argument("--after", type=Path, help="compare existing HOST baseline JSON")
    parser.add_argument("--foreign", action="append", default=[], help="explicit existing foreign ESP file; never created")
    args = parser.parse_args()
    try:
        if args.device:
            require(args.expected_bytes is not None, "native mode requires explicit --expected-bytes")
            spec = importlib.util.spec_from_file_location("media_check", HERE / "check-media.py")
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            with contextlib.redirect_stdout(io.StringIO()):
                module.check(argparse.Namespace(device=args.device, expected_bytes=args.expected_bytes))
            source_path = Path(args.device)
            identity = args.device
        else:
            source_path = Path(args.image)
            require(source_path.is_absolute() and source_path.is_file() and
                    stat.S_ISREG(source_path.stat().st_mode), "development image must be an absolute regular-file path")
            identity = "development-image"
        require(args.inspector.is_absolute() and args.inspector.is_file(), "need absolute read-only fat32-inspect tool")
        inspector = Inspector(args.inspector, source_path)
        inspector.check()
        baseline = None
        if args.after:
            require(args.after.is_absolute(), "baseline path must be absolute")
            baseline = json.loads(args.after.read_text())
            require(baseline.get("format") == "PYXIS-NATIVE-READBACK-1" and
                    baseline.get("identity") == identity, "baseline identity/format mismatch")
        foreign = {}
        paths = args.foreign or (list(baseline.get("foreign", {})) if baseline else [])
        for path in paths:
            foreign_path(path)
            data = inspector.read(path, limit=MAX_PAYLOAD)
            foreign[path] = data[1]
        recovery = rescue(inspector, optional=bool(args.before))
        if args.before:
            require(args.before.is_absolute() and not str(args.before).startswith("/dev/"), "baseline must be an absolute host file")
            menu_outside = None
            before_menu = inspector.read("/boot/limine/limine.conf", optional=True)
            if before_menu is not None and "# pyxis:begin boot.normal" in text(before_menu):
                _, menu_outside = menu_parts(text(before_menu))
            snapshot = {"format": "PYXIS-NATIVE-READBACK-1", "identity": identity,
                        "bytes": args.expected_bytes if args.device else source_path.stat().st_size,
                        "foreign": foreign, "recovery": recovery, "menu_outside": menu_outside}
            with args.before.open("x") as output:
                json.dump(snapshot, output, indent=2)
                output.write("\n")
            print("PASS: quiescent media identity and read-only FAT check")
            print(f"PASS: baseline saved; explicit foreign files={len(foreign)}; existing rescue={'yes' if recovery else 'absent'}")
            print("LIMIT: host baseline only; no media writes or activation proof")
            return 0
        gate = None
        if args.expected_install:
            require(args.expected_install.is_absolute() and args.expected_install.stat().st_size <= 65536,
                    "expected-install must be a bounded absolute host file")
            gate = json.loads(args.expected_install.read_text())
            require(gate.get("format") == "PYXIS-GATE-EXPECTED-1", "unknown gate expectations")
        count, outside = publication(inspector, gate, args.require_previous)
        if baseline:
            require(foreign == baseline["foreign"], "foreign evidence changed or requested set differs")
            require(baseline.get("bytes") == (args.expected_bytes if args.device else source_path.stat().st_size),
                    "baseline device/image byte extent differs")
            if baseline.get("menu_outside") is not None:
                require(outside == baseline["menu_outside"], "primary menu bytes outside owned spans changed")
            if baseline.get("recovery") is not None:
                require(recovery == baseline["recovery"], "immutable rescue/fallback changed")
        print("PASS: quiescent media identity and read-only FAT check")
        print(f"PASS: committed record; {count} current/previous payload size+SHA-256 matches")
        print("PASS: all four selected configuration size+SHA-256 matches")
        print("PASS: normal/rescue primary entries select current payloads; no known higher-priority shadow")
        print("PASS: static fallback and rescue manifest/payload hashes match")
        if gate:
            print("PASS: payloads/configs, EFI loader, menu and immutable rescue match expected source/clean-Install hashes")
            if args.require_previous:
                print("PASS: distinct previous boot attempt retained after Update")
        print(f"PASS: baseline {len(foreign)} foreign file(s) and recorded rescue/menu evidence unchanged" if baseline else "NOT CHECKED: arbitrary foreign-file or intermediate-state preservation (no mid-run baseline)")
        print("LIMIT: snapshot/hash report, not semantic validation or power-failure/boot proof")
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
