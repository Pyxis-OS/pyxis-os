#!/usr/bin/env python3
"""Pack and check the stored-only Pyxis archive profile; no native launch."""

import argparse
import binascii
from dataclasses import dataclass
import json
import os
from pathlib import Path
import stat
import struct
import sys
import tempfile


# Host admission settings, not ZIP encoding limits or native allocation budgets.
MAX_ARCHIVE = 512 * 1024 * 1024
MAX_ENTRIES = 16384
MAX_METADATA = 4 * 1024 * 1024
MAX_MANIFEST = 64 * 1024
CHUNK = 1024 * 1024
UTF8 = 0x0800
LOCAL = struct.Struct("<4s5H3I2H")
CENTRAL = struct.Struct("<4s6H3I5H2I")
END = struct.Struct("<4s4H2IH")
P1F_MAGIC = b"PXE1FLAT"
RIGHTS = {
    "memory": {"manage"}, "clock": {"read", "sleep"}, "launcher": {"launch"},
    "random": {"read"}, "system_info": {"read"}, "echo": {"send"},
    "tcp": {"connect", "listen"}, "pipe": {"create"}, "udp": {"open"},
    "display": {"draw"}, "audio": {"create"}, "screen_capture": {"capture"},
    "input": {"read"}, "output": {"write"}, "profile": {"memory", "host"},
}


class ProfileError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ProfileError(message)


def utf8(text):
    require(isinstance(text, str), "expected a string")
    encoded = text.encode("utf-8", errors="strict")
    require(b"\0" not in encoded, "NUL in string")
    return encoded


def path_name(name, directory=False):
    raw = utf8(name)
    require(raw and len(raw) <= 65535, "invalid ZIP name length")
    require(not name.startswith("/") and ":" not in name and "\\" not in name,
            f"absolute, drive or backslash path: {name!r}")
    if directory:
        require(name.endswith("/"), "directory name lacks trailing slash")
        name = name[:-1]
    parts = name.split("/")
    require(all(part not in ("", ".", "..") and len(utf8(part)) <= 255
                for part in parts), f"invalid path component: {name!r}")
    return name, raw


def manifest_name(name):
    raw = utf8(name)
    require(0 < len(raw) <= 255 and name not in (".", "..")
            and not any(c <= 0x20 or c == 0x7f or c in b"/:\\" for c in raw),
            f"invalid manifest name: {name!r}")


def manifest_path(path):
    raw = utf8(path)
    require(0 < len(raw) <= 1024, "invalid manifest path length")
    path_name(path)
    require(not path.endswith("/"), "manifest path has empty final component")


def fields(value, required, optional=()):
    require(type(value) is dict and set(value) >= set(required)
            and set(value) <= set(required) | set(optional), "invalid object fields")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def unsigned_integer(text):
    require(not text.startswith("-"), "negative JSON integer")
    value = int(text)
    require(value <= (1 << 64) - 1, "JSON integer exceeds uint64")
    return value


def invalid_number(text):
    raise ProfileError(f"unsupported JSON number: {text}")


def json_depth(value, depth=0):
    # Matches the native bundle parser's depth limit.
    require(depth <= 16, "JSON nesting limit")
    if isinstance(value, str):
        utf8(value)
    elif type(value) is dict:
        for key, child in value.items():
            utf8(key)
            json_depth(child, depth + 1)
    elif type(value) is list:
        for child in value:
            json_depth(child, depth + 1)


def manifest_check(data, entries, tree):
    require(0 < len(data) <= MAX_MANIFEST, "manifest byte limit")
    value = json.loads(data.decode("utf-8", errors="strict"),
                       object_pairs_hook=unique_object, parse_int=unsigned_integer,
                       parse_float=invalid_number, parse_constant=invalid_number)
    json_depth(value)
    fields(value, ("format", "id", "entry", "commands", "resource_dirs", "grants"),
           ("stack_bytes",))
    require(type(value["format"]) is int and value["format"] == 1,
            "unsupported manifest format")
    manifest_name(value["id"])
    if "stack_bytes" in value:
        size = value["stack_bytes"]
        require(type(size) is int and 1024 * 1024 <= size <= 8 * 1024 * 1024
                and size % 4096 == 0, "invalid stack_bytes")
    commands, roots, grants = value["commands"], value["resource_dirs"], value["grants"]
    require(type(commands) is dict and len(commands) <= 32, "invalid command map")
    require(type(roots) is dict and len(roots) <= 8, "invalid resource map")
    require(type(grants) is list and len(grants) <= 16, "invalid grant list")
    for name in commands:
        manifest_name(name)
    for name, path in roots.items():
        manifest_name(name)
        manifest_path(path)
        require(name != "app", "resource root conflicts with app")
        require(tree.nodes.get("app/" + path) is True, f"missing resource directory: {path!r}")
    seen = set(roots) | {"app"}
    for grant in grants:
        fields(grant, ("name", "resource", "rights", "required"))
        name, resource, rights = grant["name"], grant["resource"], grant["rights"]
        manifest_name(name)
        require(name not in seen, f"duplicate startup binding: {name!r}")
        seen.add(name)
        require(type(resource) is str and resource in RIGHTS, "unknown grant resource")
        require(type(grant["required"]) is bool, "required must be Boolean")
        require(type(rights) is list and rights, "rights must be a nonempty list")
        require(all(type(right) is str and right in RIGHTS[resource] for right in rights),
                "unknown grant right")
        require(len(set(rights)) == len(rights), "duplicate grant right")
    for path in [value["entry"], *commands.values()]:
        manifest_path(path)
        entry = entries.get("app/" + path)
        require(entry is not None and not entry.directory and entry.prefix == P1F_MAGIC,
                f"entry is not a native P1F file: {path!r}")
    return value


class NameTree:
    def __init__(self):
        self.nodes = {}
        self.actual = set()
        self.name_bytes = 0

    def add(self, name, directory):
        name, raw = path_name(name, directory)
        require(name not in self.actual, f"duplicate ZIP record: {name!r}")
        parts = name.split("/")
        for i in range(1, len(parts) + 1):
            path = "/".join(parts[:i])
            kind = True if i < len(parts) else directory
            if path in self.nodes:
                require(self.nodes[path] == kind, f"file/directory collision: {path!r}")
            else:
                self.name_bytes += len(utf8(path))
                require(self.name_bytes <= MAX_METADATA and len(self.nodes) < MAX_ENTRIES,
                        "inferred name-tree budget")
                self.nodes[path] = kind
        self.actual.add(name)
        return name, raw


@dataclass
class Entry:
    name: str
    raw: bytes
    directory: bool
    size: int
    crc: int = 0
    offset: int = 0
    data_offset: int = 0
    prefix: bytes = b""
    signature: tuple = ()
    local_fields: tuple = ()


def signature(info):
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns


def open_regular(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        require(stat.S_ISREG(os.fstat(fd).st_mode), "expected a regular file")
        return os.fdopen(fd, "rb")
    except BaseException:
        os.close(fd)
        raise


def read_exact(source, size):
    data = source.read(size)
    require(len(data) == size, "truncated ZIP record or payload")
    return data


def attributes_check(made_by, attributes, directory):
    platform = made_by >> 8
    require(platform in (0, 3), "unsupported ZIP attribute platform")
    dos = attributes & 0xffff
    require(dos & ~0x37 == 0, "special or unknown DOS attributes")
    require(not dos & 0x10 or directory, "directory attribute on file")
    mode = attributes >> 16
    if platform == 0:
        require(mode == 0, "unexpected DOS high attributes")
    else:
        kind = stat.S_IFMT(mode)
        require(kind in (0, stat.S_IFDIR if directory else stat.S_IFREG),
                "symlink, special file or conflicting Unix type")


def check_archive(path):
    with open_regular(path) as source:
        initial = signature(os.fstat(source.fileno()))
        size = initial[2]
        require(END.size <= size <= MAX_ARCHIVE, "archive byte limit")
        source.seek(size - END.size)
        end = END.unpack(read_exact(source, END.size))
        magic, disk, cd_disk, disk_count, count, cd_size, cd_offset, comment = end
        require(magic == b"PK\x05\x06" and disk == cd_disk == 0 and disk_count == count,
                "invalid, split or ZIP64 end record")
        require(0 < count <= MAX_ENTRIES and comment == 0, "entry count or ZIP comment")
        require(cd_offset + cd_size == size - END.size and cd_size <= MAX_METADATA,
                "invalid central-directory extent")
        source.seek(cd_offset)
        entries = {}
        tree = NameTree()
        total = 0
        metadata = cd_size + END.size
        for _ in range(count):
            require(source.tell() + CENTRAL.size <= size - END.size,
                    "central record exceeds directory")
            central = CENTRAL.unpack(read_exact(source, CENTRAL.size))
            (magic, made, needed, flags, method, time, date, crc, compressed, expanded,
             name_size, extra, entry_comment, start_disk, internal, external, offset) = central
            require(magic == b"PK\x01\x02" and needed in (10, 20) and flags == UTF8
                    and method == 0, "unsupported version, flags or method")
            require(extra == entry_comment == start_disk == 0 and internal & ~1 == 0,
                    "extra fields, comments or unsupported record attributes")
            require(compressed == expanded, "stored size mismatch")
            require(source.tell() + name_size <= size - END.size, "name exceeds directory")
            raw = read_exact(source, name_size)
            name = raw.decode("utf-8", errors="strict")
            directory = name.endswith("/")
            key, checked_raw = tree.add(name, directory)
            attributes_check(made, external, directory)
            require(not directory or expanded == crc == 0, "directory contains data")
            metadata += LOCAL.size + len(raw)
            total += expanded
            require(total <= MAX_ARCHIVE and metadata + tree.name_bytes <= MAX_METADATA,
                    "expanded or host metadata budget")
            entries[key] = Entry(key, checked_raw, directory, expanded, crc, offset)
            # Retain only fields needed for the independent local-record comparison.
            entries[key].local_fields = (needed, flags, method, time, date, crc,
                                         compressed, expanded, name_size, 0)
        require(source.tell() == cd_offset + cd_size, "central-directory count/size mismatch")
        require("manifest.json" in entries and not entries["manifest.json"].directory
                and tree.nodes.get("app") is True, "missing manifest.json or app/")
        require(entries["manifest.json"].size <= MAX_MANIFEST, "manifest byte limit")
        cursor = 0
        manifest = None
        for entry in sorted(entries.values(), key=lambda e: e.offset):
            require(entry.offset == cursor and cursor + LOCAL.size <= cd_offset,
                    "overlapping, gapped or prefixed local records")
            source.seek(cursor)
            local = LOCAL.unpack(read_exact(source, LOCAL.size))
            require(local[0] == b"PK\x03\x04" and local[1:] == entry.local_fields,
                    "local/central record mismatch")
            require(read_exact(source, len(entry.raw)) == entry.raw,
                    "local/central name mismatch")
            entry.data_offset = source.tell()
            cursor = entry.data_offset + entry.size
            require(cursor <= cd_offset, "payload overlaps central directory")
            crc = 0
            left = entry.size
            captured = bytearray() if entry.name == "manifest.json" else None
            while left:
                data = read_exact(source, min(left, CHUNK))
                if not entry.prefix:
                    entry.prefix = data[:len(P1F_MAGIC)]
                crc = binascii.crc32(data, crc)
                if captured is not None:
                    captured.extend(data)
                left -= len(data)
            require(crc == entry.crc, f"CRC mismatch: {entry.name!r}")
            if captured is not None:
                manifest = bytes(captured)
        require(cursor == cd_offset, "unindexed bytes before central directory")
        manifest_check(manifest, entries, tree)
        require(signature(os.fstat(source.fileno())) == initial, "archive changed during check")
        return len(entries), total, metadata + tree.name_bytes


def open_beneath(root, path, directory=False):
    parts = path.split("/") if path else []
    current = os.dup(root)
    try:
        for i, part in enumerate(parts):
            flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK
            if i < len(parts) - 1 or directory:
                flags |= os.O_DIRECTORY
            child = os.open(part, flags, dir_fd=current)
            os.close(current)
            current = child
        return current
    except BaseException:
        os.close(current)
        raise


def inventory(root):
    entries = []
    tree = NameTree()
    total = 0
    metadata = END.size
    pending = [("", os.scandir(root))]
    try:
        while pending:
            parent, children = pending[-1]
            child = next(children, None)
            if child is None:
                children.close()
                pending.pop()
                continue
            child = child.name
            name = parent + "/" + child if parent else child
            # Validate the lexical path before any host lookup or normalization.
            path_name(name)
            parent_fd = open_beneath(root, parent, directory=True)
            try:
                info = os.stat(child, dir_fd=parent_fd, follow_symlinks=False)
            finally:
                os.close(parent_fd)
            require(stat.S_ISREG(info.st_mode) or stat.S_ISDIR(info.st_mode),
                    f"source symlink or special file: {name!r}")
            directory = stat.S_ISDIR(info.st_mode)
            _, raw = tree.add(name + "/" if directory else name, directory)
            file_size = 0 if directory else info.st_size
            require(file_size >= 0, "negative source size")
            entry = Entry(name, raw, directory, file_size, signature=signature(info))
            entries.append(entry)
            total += file_size
            metadata += LOCAL.size + CENTRAL.size + len(raw) * 2
            require(len(entries) <= MAX_ENTRIES and total + metadata <= MAX_ARCHIVE
                    and metadata + tree.name_bytes <= MAX_METADATA, "source archive budget")
            if directory:
                fd = open_beneath(root, name, directory=True)
                try:
                    require(signature(os.fstat(fd)) == entry.signature, "source directory changed")
                    pending.append((name, os.scandir(fd)))
                finally:
                    os.close(fd)
    finally:
        for _, children in pending:
            children.close()
    require(tree.nodes.get("manifest.json") is False and tree.nodes.get("app") is True,
            "missing manifest.json or app/")
    return sorted(entries, key=lambda e: (e.name != "manifest.json", e.raw))


def write_archive(output, root, entries):
    for entry in entries:
        entry.offset = output.tell()
        header = (b"PK\x03\x04", 20, UTF8, 0, 0, 0x21, 0,
                  entry.size, entry.size, len(entry.raw), 0)
        output.write(LOCAL.pack(*header))
        output.write(entry.raw)
        if not entry.directory:
            fd = open_beneath(root, entry.name)
            with os.fdopen(fd, "rb") as source:
                require(stat.S_ISREG(os.fstat(fd).st_mode)
                        and signature(os.fstat(fd)) == entry.signature, "source file changed")
                left = entry.size
                while left:
                    data = read_exact(source, min(left, CHUNK))
                    output.write(data)
                    entry.crc = binascii.crc32(data, entry.crc)
                    left -= len(data)
                require(not source.read(1) and signature(os.fstat(fd)) == entry.signature,
                        "source file grew or changed")
        end = output.tell()
        output.seek(entry.offset + 14)
        output.write(struct.pack("<I", entry.crc))
        output.seek(end)
    cd_offset = output.tell()
    for entry in entries:
        mode = (stat.S_IFDIR | 0o755) if entry.directory else (stat.S_IFREG | 0o644)
        attributes = mode << 16 | (0x10 if entry.directory else 0x20)
        output.write(CENTRAL.pack(b"PK\x01\x02", (3 << 8) | 20, 20, UTF8, 0, 0, 0x21,
                                  entry.crc, entry.size, entry.size, len(entry.raw),
                                  0, 0, 0, 0, attributes, entry.offset))
        output.write(entry.raw)
    cd_size = output.tell() - cd_offset
    output.write(END.pack(b"PK\x05\x06", 0, 0, len(entries), len(entries),
                         cd_size, cd_offset, 0))
    require(output.tell() <= MAX_ARCHIVE, "archive byte limit")


def pack_archive(source_path, destination):
    source_path = Path(source_path).absolute()
    destination = Path(destination).absolute()
    # Resolve the parent, not an existing output symlink that replace will remove.
    target = destination.parent.resolve() / destination.name
    require(not target.is_relative_to(source_path.resolve()), "output lies inside source tree")
    root = os.open(source_path, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    temporary = None
    try:
        entries = inventory(root)
        fd, name = tempfile.mkstemp(prefix=".pxa-", suffix=".tmp", dir=target.parent)
        temporary = Path(name)
        with os.fdopen(fd, "w+b") as output:
            write_archive(output, root, entries)
        result = check_archive(temporary)
        os.replace(temporary, target)
        temporary = None
        return result
    finally:
        os.close(root)
        if temporary is not None:
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="package a quiescent unpacked bundle")
    pack.add_argument("source")
    pack.add_argument("output")
    check = commands.add_parser("check", help="check stored ZIP structure, CRCs and manifest")
    check.add_argument("archive")
    args = parser.parse_args()
    try:
        if args.command == "pack":
            result = pack_archive(args.source, args.output)
            label = args.output
        else:
            result = check_archive(args.archive)
            label = args.archive
        count, data, metadata = result
        print(f"{label}: checked stored PXA, {count} records, {data} file bytes, "
              f"{metadata} host metadata/name bytes")
        return 0
    except (OSError, ValueError, UnicodeError, struct.error, RecursionError, MemoryError) as error:
        print(f"pxa: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
