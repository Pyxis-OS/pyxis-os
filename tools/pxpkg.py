#!/usr/bin/env python3
"""Build and check a static Pyxis package repository (docs/interfaces/packages.md)."""

import argparse
import hashlib
import io
import math
import os
from pathlib import Path
import re
import stat
import sys
import tarfile
import tempfile


class PackageError(ValueError):
    pass


# Native literal parser limits: userspace/libconfig/literal.c and literal.h
# (the userspace submodule). Keep this reader and that parser in step whenever
# either side changes; the client parses every index and manifest with it.
LITERAL_SOURCE_LIMIT = 1024 * 1024
LITERAL_DEPTH_LIMIT = 32
LITERAL_ITEM_LIMIT = 8192
LITERAL_WORK_LIMIT = 16 * 1024 * 1024
LITERAL_RESERVED = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
    "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true",
    "until", "while",
}

# Format 1 bounds; the reference states the same numbers.
FORMAT = 1
KINDS = ("data", "toolchain")
ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9.-]{0,63}")
VERSION_PATTERN = re.compile(r"[0-9A-Za-z.+-]{1,64}")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
MAX_INVENTORY_LINE = 512
MAX_FILES = 65536
MAX_PATH = 250
MAX_COMPONENT = 255
MAX_FILE_SIZE = 8 * 1024 * 1024 * 1024 - 1
MANIFEST_NAME = "package.lua"
INVENTORY_NAME = "package.files"
TREE_NAME = "tree"
INDEX_NAME = "index.lua"
CHUNK = 1024 * 1024


def require(condition, message):
    if not condition:
        raise PackageError(message)


# Strict Lua literal data: a port of libconfig's grammar. Tables come back as
# lists of (key, value) pairs in source order; strings as bytes.

class Table:
    def __init__(self):
        self.entries = []

    def field(self, name):
        key = name.encode()
        for entry_key, value in self.entries:
            if entry_key == key:
                return value
        return None


class LiteralParser:
    def __init__(self, source):
        self.source = source
        self.position = 0
        self.values = 0
        self.entries = 0
        self.comments = 0
        self.work = len(source) * 2

    def fail(self, reason):
        line = self.source.count(b"\n", 0, self.position) + 1
        raise PackageError(f"literal line {line}: {reason}")

    def spend(self, count):
        if count > LITERAL_WORK_LIMIT - self.work:
            self.fail("literal parsing work limit exceeded")
        self.work += count

    def peek(self, offset=0):
        index = self.position + offset
        return self.source[index:index + 1]

    def bracket(self, start, delimiter):
        source = self.source
        if source[start:start + 1] != delimiter:
            return start
        end = start + 1
        while source[end:end + 1] == b"=":
            end += 1
        self.spend(end - start)
        return end + 1 if source[end:end + 1] == delimiter else start

    def long_body(self):
        start = self.position
        open_end = self.bracket(start, b"[")
        if open_end == start:
            self.fail("invalid long bracket")
        self.position = open_end
        while self.position < len(self.source):
            close_end = self.bracket(self.position, b"]")
            if close_end - self.position == open_end - start:
                body = (open_end, self.position)
                self.position = close_end
                return body
            self.position += 1
        self.fail("unterminated long string/comment")

    def skip(self):
        while self.position < len(self.source):
            byte = self.peek()
            if byte in b" \t\r\n\v\f" and byte:
                self.position += 1
            elif self.source.startswith(b"--", self.position):
                self.comment()
            else:
                break

    def comment(self):
        if self.comments == LITERAL_ITEM_LIMIT:
            self.fail("too many comments")
        self.comments += 1
        self.position += 2
        if self.bracket(self.position, b"[") != self.position:
            self.long_body()
        else:
            while self.position < len(self.source) and self.peek() not in (b"\n", b"\r"):
                self.position += 1

    def new_value(self):
        if self.values == LITERAL_ITEM_LIMIT:
            self.fail("too many literal values")
        self.values += 1

    def keyword(self, word):
        end = self.position + len(word)
        if self.source[self.position:end] != word:
            return False
        if end < len(self.source) and identifier_part(self.source[end:end + 1]):
            return False
        self.position = end
        return True

    def at_keyword(self, word):
        start = self.position
        matched = self.keyword(word)
        self.position = start
        return matched

    def quoted(self):
        quote = self.peek()
        self.position += 1
        out = bytearray()
        while self.position < len(self.source):
            byte = self.peek()
            self.position += 1
            if byte == quote:
                return bytes(out)
            if byte in (b"\r", b"\n"):
                self.fail("newline in quoted string")
            if byte != b"\\":
                out += byte
                continue
            if self.position == len(self.source):
                break
            byte = self.peek()
            self.position += 1
            simple = {b"a": 7, b"b": 8, b"f": 12, b"n": 10, b"r": 13, b"t": 9, b"v": 11}
            if byte in simple:
                out.append(simple[byte])
            elif byte in (b"\\", b"'", b'"'):
                out += byte
            elif byte in (b"\r", b"\n"):
                self.position -= 1
                self.newline()
                out.append(10)
            elif byte == b"z":
                while self.peek() and self.peek() in b" \t\r\n\v\f":
                    self.position += 1
            elif byte == b"x":
                digits = self.source[self.position:self.position + 2]
                if len(digits) != 2 or not all(chr(d) in "0123456789abcdefABCDEF" for d in digits):
                    self.fail("hex escape needs two digits")
                self.position += 2
                out.append(int(digits, 16))
            elif byte == b"u":
                out += self.unicode_escape()
            elif byte and byte.isdigit():
                number = int(byte)
                for _ in range(2):
                    if not (self.peek() and self.peek().isdigit()):
                        break
                    number = number * 10 + int(self.peek())
                    self.position += 1
                if number > 255:
                    self.fail("decimal escape is out of range")
                out.append(number)
            else:
                self.fail("unsupported string escape")
        self.fail("unterminated quoted string")

    def unicode_escape(self):
        if self.peek() != b"{":
            self.fail("Unicode escape needs braces")
        self.position += 1
        codepoint, digits = 0, 0
        while self.peek() and chr(self.peek()[0]) in "0123456789abcdefABCDEF":
            codepoint = codepoint * 16 + int(self.peek(), 16)
            if codepoint > 0x7FFFFFFF:
                self.fail("Unicode escape is out of range")
            self.position += 1
            digits += 1
        if not digits or self.peek() != b"}":
            self.fail("invalid Unicode escape")
        self.position += 1
        if codepoint < 0x80:
            return bytes([codepoint])
        count, remaining = 1, codepoint >> 6
        while remaining >= (1 << (6 - count)):
            count += 1
            remaining >>= 6
        out = bytearray([((0xFF << (7 - count)) | remaining) & 0xFF])
        for i in range(count, 0, -1):
            out.append(0x80 | ((codepoint >> ((i - 1) * 6)) & 0x3F))
        return bytes(out)

    def newline(self):
        first = self.peek()
        self.position += 1
        if (first, self.peek()) in ((b"\r", b"\n"), (b"\n", b"\r")):
            self.position += 1

    def long_string(self):
        start, end = self.long_body()
        body = self.source[start:end]
        if body[:2] in (b"\r\n", b"\n\r"):
            body = body[2:]
        elif body[:1] in (b"\r", b"\n"):
            body = body[1:]
        return re.sub(rb"\r\n|\n\r|\r", b"\n", body)

    def numeric(self):
        match = re.compile(
            rb"-?(0[xX][0-9a-fA-F]*(\.[0-9a-fA-F]*)?([pP][+-]?[0-9]*)?|"
            rb"[0-9]*(\.[0-9]*)?([eE][+-]?[0-9]*)?)").match(self.source, self.position)
        token = match.group(0)
        body = token.lstrip(b"-")
        hexadecimal = body[:2].lower() == b"0x"
        mantissa = re.split(rb"[pPeE]", body[2:] if hexadecimal else body)[0]
        if not mantissa.replace(b".", b""):
            self.fail("invalid literal number")
        exponent = re.search(rb"[pP]([+-]?)([0-9]*)$" if hexadecimal else rb"[eE]([+-]?)([0-9]*)$", body)
        if exponent and not exponent.group(2):
            self.fail("number exponent needs digits")
        self.position = match.end()
        following = self.peek()
        if len(token) > 128 or identifier_part(following) or following == b".":
            self.fail("unsupported literal number")
        integer = b"." not in body and not exponent
        text = token.decode()
        if integer:
            if hexadecimal:
                number = int(text.lstrip("-"), 16)
                if number >= 1 << 64:
                    self.fail("hex integer is out of range")
                number = (-number if text.startswith("-") else number) & ((1 << 64) - 1)
                return number - (1 << 64) if number >= 1 << 63 else number
            number = int(text)
            if -(1 << 63) <= number < 1 << 63:
                return number
        number = float.fromhex(text) if hexadecimal else float(text)
        if not math.isfinite(number):
            self.fail("number is not finite or representable")
        return number

    def table(self, depth):
        if depth == LITERAL_DEPTH_LIMIT:
            self.fail("literal table depth limit exceeded")
        self.position += 1
        result = Table()
        array_index = 0
        self.skip()
        while self.peek() != b"}":
            if not self.peek():
                self.fail("unterminated table")
            if self.entries == LITERAL_ITEM_LIMIT:
                self.fail("too many table entries")
            self.entries += 1
            if self.peek() == b"[" and self.bracket(self.position, b"[") == self.position:
                self.position += 1
                self.skip()
                key = self.value(depth + 1)
                if isinstance(key, Table):
                    self.fail("table keys are unsupported")
                self.skip()
                if self.peek() != b"]":
                    self.fail("explicit key needs closing bracket")
                self.position += 1
                self.skip()
                if self.peek() != b"=":
                    self.fail("explicit key needs equals")
                self.position += 1
            elif identifier_start(self.peek()) and not self.at_keyword(b"true") \
                    and not self.at_keyword(b"false"):
                start = self.position
                while identifier_part(self.peek()):
                    self.position += 1
                key = self.source[start:self.position]
                if key.decode() in LITERAL_RESERVED:
                    self.fail("reserved word cannot be a field key")
                self.new_value()
                self.skip()
                if self.peek() != b"=":
                    self.fail("literal field needs equals")
                self.position += 1
            else:
                self.new_value()
                array_index += 1
                key = array_index
            self.skip()
            value = self.value(depth + 1)
            for prior_key, _ in result.entries:
                self.spend(1 + (len(prior_key) if isinstance(prior_key, bytes) else 0))
                if same_key(prior_key, key):
                    self.fail("duplicate literal table key")
            result.entries.append((key, value))
            self.skip()
            if self.peek() in (b",", b";"):
                self.position += 1
                self.skip()
            elif self.peek() != b"}":
                self.fail("table entry needs separator")
        self.position += 1
        return result

    def value(self, depth):
        self.new_value()
        byte = self.peek()
        if byte == b"{":
            return self.table(depth)
        if byte in (b"'", b'"'):
            return self.quoted()
        if byte == b"[":
            return self.long_string()
        if byte and (byte.isdigit() or byte in b".-"):
            return self.numeric()
        if self.keyword(b"true"):
            return True
        if self.keyword(b"false"):
            return False
        self.fail("unsupported Lua expression; expected literal value")

    def document(self):
        if len(self.source) > LITERAL_SOURCE_LIMIT:
            raise PackageError("literal source exceeds limit")
        if b"\0" in self.source:
            self.position = self.source.index(b"\0")
            self.fail("literal source contains NUL")
        self.skip()
        if not self.keyword(b"return"):
            self.fail("literal configuration must return one table")
        self.skip()
        if self.peek() != b"{":
            self.fail("literal configuration must return one table")
        root = self.value(0)
        self.skip()
        if self.peek() == b";":
            self.position += 1
            self.skip()
        if self.position != len(self.source):
            self.fail("unexpected bytes after return table")
        return root


def identifier_start(byte):
    return bool(byte) and (byte.isalpha() and byte.isascii() or byte == b"_")


def identifier_part(byte):
    return identifier_start(byte) or (bool(byte) and byte.isdigit())


def same_key(a, b):
    numeric = (int, float)
    if isinstance(a, bool) or isinstance(b, bool):
        return type(a) is type(b) and a == b
    if isinstance(a, numeric) and isinstance(b, numeric):
        return a == b
    return type(a) is type(b) and a == b


def parse_literal(source):
    return LiteralParser(source).document()


# Lua literal text for the schemas below: identifier keys, strings, integers.

def literal_string(text):
    out = ['"']
    for character in text:
        code = ord(character)
        if character in ('"', "\\"):
            out.append("\\" + character)
        elif code < 0x20 or code == 0x7F:
            out.append(f"\\{code:03d}")
        else:
            out.append(character)
    out.append('"')
    return "".join(out)


def literal_value(value, indent):
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, str):
        return literal_string(value)
    inner = indent + "  "
    if isinstance(value, dict):
        if not value:
            return "{}"
        lines = [f"{inner}{key} = {literal_value(item, inner)}," for key, item in value.items()]
    else:
        if not value:
            return "{}"
        lines = [f"{inner}{literal_value(item, inner)}," for item in value]
    return "{\n" + "\n".join(lines) + "\n" + indent + "}"


def literal_document(value):
    return ("return " + literal_value(value, "") + "\n").encode("utf-8")


# Schemas.

def text_field(table, name, pattern=None):
    value = table.field(name)
    require(isinstance(value, bytes), f"{name} must be a string")
    text = value.decode("utf-8", errors="strict")
    if pattern is not None:
        require(pattern.fullmatch(text), f"invalid {name}: {text!r}")
    return text


def integer_field(table, name, minimum=0, maximum=None):
    value = table.field(name)
    require(isinstance(value, int) and not isinstance(value, bool), f"{name} must be an integer")
    require(value >= minimum and (maximum is None or value <= maximum), f"{name} out of range")
    return value


def exact_fields(table, names, label):
    require(isinstance(table, Table), f"{label} must be a table")
    keys = [key for key, _ in table.entries]
    require(all(isinstance(key, bytes) for key in keys), f"{label} has positional entries")
    present = {key.decode() for key in keys}
    require(present == set(names), f"{label} fields must be exactly {', '.join(sorted(names))}")


def array(value, label):
    require(isinstance(value, Table), f"{label} must be a table")
    items = []
    for index, (key, item) in enumerate(value.entries, 1):
        require(key == index, f"{label} must be a plain array")
        items.append(item)
    return items


def check_format(table, label):
    require(table.field("format") == FORMAT, f"{label} format must be {FORMAT}")


def manifest_check(table):
    exact_fields(table, ("format", "id", "version", "kind", "depends", "files", "bytes",
                         "inventory_sha256"), "package.lua")
    check_format(table, "package.lua")
    manifest = {
        "id": text_field(table, "id", ID_PATTERN),
        "version": text_field(table, "version", VERSION_PATTERN),
        "kind": text_field(table, "kind"),
        "files": integer_field(table, "files", 1, MAX_FILES),
        "bytes": integer_field(table, "bytes"),
        "inventory_sha256": text_field(table, "inventory_sha256", SHA256_PATTERN),
    }
    require(manifest["kind"] in KINDS, f"unknown kind: {manifest['kind']}")
    depends = []
    for item in array(table.field("depends"), "depends"):
        exact_fields(item, ("id", "version"), "dependency")
        depends.append((text_field(item, "id", ID_PATTERN),
                        text_field(item, "version", VERSION_PATTERN)))
    require(len(set(depends)) == len(depends), "duplicate dependency")
    manifest["depends"] = depends
    return manifest


def index_check(table):
    exact_fields(table, ("format", "packages"), INDEX_NAME)
    check_format(table, INDEX_NAME)
    entries, seen = [], set()
    for item in array(table.field("packages"), "packages"):
        exact_fields(item, ("id", "version", "kind", "archive", "size", "sha256"), "index entry")
        entry = {
            "id": text_field(item, "id", ID_PATTERN),
            "version": text_field(item, "version", VERSION_PATTERN),
            "kind": text_field(item, "kind"),
            "archive": text_field(item, "archive"),
            "size": integer_field(item, "size", 1),
            "sha256": text_field(item, "sha256", SHA256_PATTERN),
        }
        require(entry["kind"] in KINDS, f"unknown kind: {entry['kind']}")
        require(entry["archive"] == archive_path(entry["id"], entry["version"]),
                f"archive path must be {archive_path(entry['id'], entry['version'])}")
        key = (entry["id"], entry["version"])
        require(key not in seen, f"duplicate index entry {key[0]} {key[1]}")
        seen.add(key)
        entries.append(entry)
    return entries


def archive_path(package_id, version):
    return f"packages/{package_id}/{version}.tar"


# The inventory: one "SHA256 SIZE PATH" line per file, sorted by path bytes.

def path_check(path):
    raw = path.encode("utf-8", errors="strict")
    require(0 < len(raw) <= MAX_PATH, f"path length out of range: {path!r}")
    require(b"\0" not in raw and b"\\" not in raw and not path.startswith("/"),
            f"invalid path: {path!r}")
    parts = path.split("/")
    require(all(part not in ("", ".", "..") and len(part.encode()) <= MAX_COMPONENT
                for part in parts), f"invalid path component: {path!r}")
    require(ustar_splittable(f"{TREE_NAME}/{path}".encode()), f"path does not fit USTAR: {path!r}")
    return raw


def ustar_splittable(name):
    """USTAR stores a name of at most 100 bytes, after a prefix of at most 155 split at a slash."""
    if len(name) <= 100:
        return True
    return any(name[i:i + 1] == b"/" and i <= 155 and len(name) - i - 1 <= 100
               for i in range(len(name)))


def inventory_text(files):
    lines = [f"{digest} {size} {path}\n" for path, size, digest in files]
    return "".join(lines).encode("utf-8")


def inventory_parse(data):
    require(data.endswith(b"\n") and b"\r" not in data, "inventory must be LF-terminated lines")
    files, previous = [], None
    for line in data[:-1].split(b"\n"):
        require(len(line) + 1 <= MAX_INVENTORY_LINE, "inventory line too long")
        match = re.fullmatch(rb"([0-9a-f]{64}) (0|[1-9][0-9]{0,18}) (.+)", line)
        require(match, f"invalid inventory line: {line[:80]!r}")
        path = match.group(3).decode("utf-8", errors="strict")
        raw = path_check(path)
        size = int(match.group(2))
        require(size <= MAX_FILE_SIZE, f"file too large: {path}")
        require(previous is None or raw > previous, "inventory must be sorted without duplicates")
        previous = raw
        files.append((path, size, match.group(1).decode()))
        require(len(files) <= MAX_FILES, "too many files")
    return files


def member_order(files):
    """Archive members after the metadata: tree/, then each file's missing ancestors."""
    members, directories = [(TREE_NAME, None)], set()
    for path, size, _ in files:
        parts = path.split("/")
        for depth in range(1, len(parts)):
            directory = "/".join(parts[:depth])
            if directory not in directories:
                directories.add(directory)
                members.append((f"{TREE_NAME}/{directory}", None))
        members.append((f"{TREE_NAME}/{path}", size))
    file_set = {path for path, _, _ in files}
    require(not directories & file_set, "a path is both a file and a directory")
    return members


# Archives.

def tar_info(name, size):
    info = tarfile.TarInfo(name)
    info.mtime = 0
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    if size is None:
        info.type, info.mode = tarfile.DIRTYPE, 0o755
    else:
        info.type, info.mode, info.size = tarfile.REGTYPE, 0o644, size
    return info


def write_archive(output, manifest_bytes, inventory_bytes, files, sources):
    with tarfile.open(fileobj=output, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for name, data in ((MANIFEST_NAME, manifest_bytes), (INVENTORY_NAME, inventory_bytes)):
            archive.addfile(tar_info(name, len(data)), io.BytesIO(data))
        for name, size in member_order(files):
            if size is None:
                archive.addfile(tar_info(name, None))
                continue
            with open(sources[name[len(TREE_NAME) + 1:]], "rb") as source:
                archive.addfile(tar_info(name, size), source)


def read_member(archive, member, limit):
    require(member.size <= limit, f"{member.name} too large")
    data = archive.extractfile(member).read()
    require(len(data) == member.size, f"short member {member.name}")
    return data


def padding_check(raw, members):
    """Every byte outside headers and member data is zero, through the end."""
    raw.seek(0, os.SEEK_END)
    size = raw.tell()
    require(size % 512 == 0, "archive size is not a multiple of 512")
    ends = [m.offset_data + m.size for m in members]
    starts = [m.offset for m in members[1:]] + [size]
    for end, start in zip(ends, starts):
        raw.seek(end)
        gap = raw.read(start - end)
        require(gap.count(0) == len(gap), "nonzero bytes outside members")
    require(size - ends[-1] >= 1024, "archive lacks its end-of-archive blocks")


def check_archive(path, expected=None):
    """Verify one archive's structure and contents; return its manifest."""
    with tarfile.open(path, mode="r:") as archive, open(path, "rb") as raw:
        members = archive.getmembers()
        require(len(members) >= 3, "archive lacks metadata")
        for member in members:
            require(member.isreg() or member.isdir(), f"{member.name}: only files and directories")
            require(not member.pax_headers, f"{member.name}: extended headers are unsupported")
            raw.seek(member.offset)
            header = raw.read(512)
            require(header[257:265] == b"ustar\x0000", f"{member.name}: not a USTAR header")
        padding_check(raw, members)
        require([m.name for m in members[:2]] == [MANIFEST_NAME, INVENTORY_NAME]
                and all(m.isreg() for m in members[:2]), "archive must start with package.lua, package.files")
        manifest_bytes = read_member(archive, members[0], LITERAL_SOURCE_LIMIT)
        inventory_bytes = read_member(archive, members[1], MAX_FILES * MAX_INVENTORY_LINE)
        manifest = manifest_check(parse_literal(manifest_bytes))
        require(hashlib.sha256(inventory_bytes).hexdigest() == manifest["inventory_sha256"],
                "inventory hash does not match package.lua")
        files = inventory_parse(inventory_bytes)
        require(len(files) == manifest["files"], "file count does not match package.lua")
        require(sum(size for _, size, _ in files) == manifest["bytes"], "byte total does not match")
        order = member_order(files)
        actual = [(m.name, None if m.isdir() else m.size) for m in members[2:]]
        require(actual == order, "archive members do not equal the inventory")
        digests = {f"{TREE_NAME}/{path}": digest for path, _, digest in files}
        for member in members[2:]:
            if member.isreg():
                digest = hashlib.sha256()
                source = archive.extractfile(member)
                for block in iter(lambda: source.read(CHUNK), b""):
                    digest.update(block)
                require(digest.hexdigest() == digests[member.name], f"hash mismatch: {member.name}")
    if expected is not None:
        for field in ("id", "version", "kind"):
            require(manifest[field] == expected[field], f"package.lua {field} does not match the index")
    return manifest


def file_digest(path):
    digest, size = hashlib.sha256(), 0
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(CHUNK), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


# Recipes: trusted host input, the same literal shape as boot/initrd.lua entries.

def recipe_sources(recipe_path, table):
    exact_fields(table, ("id", "version", "kind", "depends", "entries"), "recipe")
    base = Path(recipe_path).resolve().parent
    sources = {}

    def add(path, source):
        path_check(path)
        require(path not in sources, f"two sources for {path}")
        sources[path] = source

    for item in array(table.field("entries"), "entries"):
        require(isinstance(item, Table), "recipe entry must be a table")
        names = {key.decode() for key, _ in item.entries if isinstance(key, bytes)}
        require(names in ({"file", "at"}, {"tree", "at"}), "recipe entry needs file or tree, and at")
        at = text_field(item, "at")
        if "file" in names:
            source = base / text_field(item, "file")
            mode = os.lstat(source).st_mode
            require(stat.S_ISREG(mode), f"not a regular file: {source}")
            add(at, source)
            continue
        root = base / text_field(item, "tree")
        require(stat.S_ISDIR(os.lstat(root).st_mode), f"not a directory: {root}")
        for directory, subdirectories, names_in in os.walk(root):
            subdirectories.sort()
            relative = Path(directory).relative_to(root)
            if directory != str(root):
                require(subdirectories or names_in, f"empty directories are unsupported: {directory}")
            for name in subdirectories:
                require(stat.S_ISDIR(os.lstat(Path(directory) / name).st_mode),
                        f"symbolic link or special entry: {Path(directory) / name}")
            for name in names_in:
                source = Path(directory) / name
                require(stat.S_ISREG(os.lstat(source).st_mode),
                        f"symbolic link or special entry: {source}")
                parts = [part for part in (at, *relative.parts, name) if part and part != "."]
                add("/".join(parts), source)
    require(sources, "a package needs at least one file")
    return sources


def write_atomically(target, data):
    fd, name = tempfile.mkstemp(prefix=f".{target.name}-", suffix=".tmp", dir=target.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(name, target)
        name = None
    finally:
        if name is not None:
            os.unlink(name)
    sync_directory(target.parent)


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def load_index(repository):
    path = Path(repository) / INDEX_NAME
    if not path.exists():
        return []
    return index_check(parse_literal(path.read_bytes()))


def index_document(entries):
    return literal_document({"format": FORMAT, "packages": [dict(entry) for entry in entries]})


def pack(recipe_path, repository):
    table = parse_literal(Path(recipe_path).read_bytes())
    sources = recipe_sources(recipe_path, table)
    package_id = text_field(table, "id", ID_PATTERN)
    version = text_field(table, "version", VERSION_PATTERN)
    kind = text_field(table, "kind")
    require(kind in KINDS, f"unknown kind: {kind}")
    depends = []
    for item in array(table.field("depends"), "depends"):
        exact_fields(item, ("id", "version"), "dependency")
        depends.append({"id": text_field(item, "id", ID_PATTERN),
                        "version": text_field(item, "version", VERSION_PATTERN)})

    entries = load_index(repository)
    require(all((e["id"], e["version"]) != (package_id, version) for e in entries),
            f"{package_id} {version} is already published")
    target = Path(repository) / archive_path(package_id, version)
    require(not target.exists(), f"{target} already exists")

    files = []
    for path in sorted(sources, key=lambda p: p.encode("utf-8")):
        digest, size = file_digest(sources[path])
        require(size <= MAX_FILE_SIZE, f"file too large: {path}")
        files.append((path, size, digest))
    require(len(files) <= MAX_FILES, "too many files")
    inventory_bytes = inventory_text(files)
    inventory_parse(inventory_bytes)
    manifest_bytes = literal_document({
        "format": FORMAT, "id": package_id, "version": version, "kind": kind,
        "depends": depends, "files": len(files), "bytes": sum(s for _, s, _ in files),
        "inventory_sha256": hashlib.sha256(inventory_bytes).hexdigest(),
    })

    target.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{version}-", suffix=".tmp", dir=target.parent)
    try:
        with os.fdopen(fd, "w+b") as output:
            write_archive(output, manifest_bytes, inventory_bytes, files, sources)
            output.flush()
            os.fsync(output.fileno())
        check_archive(name)
        os.replace(name, target)
        name = None
    finally:
        if name is not None:
            os.unlink(name)
    sync_directory(target.parent)

    digest, size = file_digest(target)
    entry = {"id": package_id, "version": version, "kind": kind,
             "archive": archive_path(package_id, version), "size": size, "sha256": digest}
    document = index_document(entries + [entry])
    index_check(parse_literal(document))
    write_atomically(Path(repository) / INDEX_NAME, document)
    return entry, len(files)


def check(repository):
    root = Path(repository)
    entries = load_index(repository)
    listed = set()
    for entry in entries:
        path = root / entry["archive"]
        digest, size = file_digest(path)
        require((size, digest) == (entry["size"], entry["sha256"]),
                f"{entry['archive']}: size or SHA-256 differs from the index")
        check_archive(path, entry)
        listed.add(entry["archive"])
    packages = root / "packages"
    if packages.exists():
        for path in packages.glob("*/*.tar"):
            relative = path.relative_to(root).as_posix()
            require(relative in listed, f"{relative} is not in the index")
    return entries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack_command = commands.add_parser("pack", help="publish one package from a recipe")
    pack_command.add_argument("recipe")
    pack_command.add_argument("repository")
    check_command = commands.add_parser("check", help="check the index and every archive")
    check_command.add_argument("repository")
    args = parser.parse_args()
    try:
        if args.command == "pack":
            entry, count = pack(args.recipe, args.repository)
            print(f"{entry['archive']}: published {entry['id']} {entry['version']}, "
                  f"{count} files, {entry['size']} archive bytes")
        else:
            entries = check(args.repository)
            print(f"{args.repository}: checked {len(entries)} package versions")
        return 0
    except (OSError, ValueError, UnicodeError, tarfile.TarError, RecursionError) as error:
        print(f"pxpkg: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
