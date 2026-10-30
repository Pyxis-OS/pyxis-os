#!/usr/bin/env python3
"""Build and check a static Pyxis package repository (docs/interfaces/packages.md)."""

import argparse
import fcntl
import hashlib
import io
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
import time

import pxa


class PackageError(ValueError):
    pass


# Native literal parser limits: userspace/lib/literal.c and include/literal.h
# (the userspace submodule). Keep this reader and that parser in step whenever
# either side changes; the client parses every index and manifest with it.
# The source limit is for the small documents the client reads into a fixed
# buffer (manifests, os-release, configuration); indexes and set records have
# none and are bounded by memory (parse_literal(..., sized=True)). The number
# of values and entries is not limited. Work is proportional to the source.
LITERAL_SOURCE_LIMIT = 1024 * 1024
LITERAL_DEPTH_LIMIT = 32
LITERAL_WORK_PER_BYTE = 64
LITERAL_WORK_BASE = 1024 * 1024
LITERAL_RESERVED = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function", "goto",
    "if", "in", "local", "nil", "not", "or", "repeat", "return", "then", "true",
    "until", "while",
}

# Bounds; the reference states the same numbers.
KINDS = ("data", "program", "toolchain", "dev", "release")
# A release package (decision 73) is a tagged build's kernel, core archive and
# os-release. Its version is the release tag and its index entry says which
# ABIs the release provides; pkg upgrade alone reads it.
RELEASE_FILES = ("caelum.elf", "initrd.cpio", "os-release")
# The shipped 0.0.10 selector knows only pyxis. Exactly one later release
# remains its permanent bootstrap; new clients follow the other two IDs.
RELEASE_IDS = ("pyxis", "pyxis-release", "pyxis-checkpoint")
LEGACY_ROUTE_RELEASE = "0.0.10"
ID_PATTERN = re.compile(r"[a-z0-9][a-z0-9.-]{0,63}")
# IDs that name roots and views a package must not be confused with: today's
# fixed roots and the pkg:// and sdk:// views (docs/wip/sdk-view.md, decision 53).
FIXED_ROOT_NAMES = ("boot", "tmp", "bin", "system", "home", "acpi", "packages", "pkg", "sdk")
VERSION_PATTERN = re.compile(r"[0-9A-Za-z][0-9A-Za-z.+-]{0,63}")
# A program version is VARIANT+LABEL, so a variant has no "+".
VARIANT_PATTERN = re.compile(r"[0-9A-Za-z][0-9A-Za-z.-]{0,63}")
# Released ABI names and exact development ABIs such as 0.0.11+abcdef012345.
ABI_PATTERN = re.compile(r"[0-9a-z][0-9a-z.+-]{0,63}")
REVISION_PATTERN = re.compile(r"[0-9a-f]{12}")
# Each published install ISO and QEMU image (docs/wip/qemu-image.md) has a line.
SUMS_LINE = re.compile(r"([0-9a-f]{64})  (pyxis-[0-9a-z][0-9a-z.-]{0,63}\.(?:iso|qcow2))")
# Kinds built for one system, which must record the ABI they were built for: native
# programs, and dev packages, whose libraries and headers form sdk://.
BOUND_KINDS = ("program", "toolchain", "dev")
# Only these export commands (docs/wip/sdk-view.md, decision 52).
COMMAND_KINDS = ("program", "toolchain")
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
# The published snapshot (docs/wip/pkg-upgrade.md, decision 71): pkg reads
# current, then index/N.lua and, for a signed repository, index/N.lua.sig and
# the signer certificate it names. index.lua stays pxpkg's working list.
CURRENT_NAME = "current"
SNAPSHOT_DIRECTORY = "index"
SIGNER_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{0,59}\.pem")
SIGNED_PATTERN = re.compile(r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z")
SERIAL_MAX = 2 ** 62
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
    def __init__(self, source, sized=False):
        self.source = source
        self.sized = sized
        self.position = 0
        self.work = len(source) * 2
        self.work_limit = len(source) * LITERAL_WORK_PER_BYTE + LITERAL_WORK_BASE

    def fail(self, reason):
        line = self.source.count(b"\n", 0, self.position) + 1
        raise PackageError(f"literal line {line}: {reason}")

    def spend(self, count):
        if count > self.work_limit - self.work:
            self.fail("literal parsing work exceeds its bound for this source")
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
        self.position += 2
        if self.bracket(self.position, b"[") != self.position:
            self.long_body()
        else:
            while self.position < len(self.source) and self.peek() not in (b"\n", b"\r"):
                self.position += 1

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
        mantissa = re.split(rb"[pP]", body[2:])[0] if hexadecimal else re.split(rb"[eE]", body)[0]
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
        try:
            number = float.fromhex(text) if hexadecimal else float(text)
        except OverflowError:
            number = math.inf
        if not math.isfinite(number):
            self.fail("number is not finite or representable")
        return number

    def table(self, depth):
        if depth == LITERAL_DEPTH_LIMIT:
            self.fail("literal table depth limit exceeded")
        self.position += 1
        result = Table()
        seen = set()
        array_index = 0
        self.skip()
        while self.peek() != b"}":
            if not self.peek():
                self.fail("unterminated table")
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
                self.skip()
                if self.peek() != b"=":
                    self.fail("literal field needs equals")
                self.position += 1
            else:
                array_index += 1
                key = array_index
            self.skip()
            value = self.value(depth + 1)
            identity = key_identity(key)
            if identity in seen:
                self.fail("duplicate literal table key")
            seen.add(identity)
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
        if not self.sized and len(self.source) > LITERAL_SOURCE_LIMIT:
            raise PackageError(f"literal source of {len(self.source)} bytes exceeds the "
                               f"small-document limit of {LITERAL_SOURCE_LIMIT}")
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


def key_identity(key):
    """Two keys are the same table key exactly when literal.c's same_key calls
    them equal: an integer-valued number is the integer, other numbers compare
    by value, and strings and booleans by content."""
    if type(key) is bool:
        return ("b", key)
    if type(key) is float:
        if key.is_integer() and -2**63 <= key < 2**63:
            return ("n", int(key))
        return ("f", key)
    if type(key) is int:
        return ("n", key)
    return ("s", key)


def parse_literal(source, sized=False):
    """SIZED parses a repository index or snapshot, which has no source limit."""
    return LiteralParser(source, sized).document()


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
        lines = []
        for key, item in value.items():
            field = key if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", key) and \
                key not in LITERAL_RESERVED else f"[{literal_string(key)}]"
            lines.append(f"{inner}{field} = {literal_value(item, inner)},")
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
    require(type(value) is int, f"{name} must be an integer literal")
    require(value >= minimum and (maximum is None or value <= maximum), f"{name} out of range")
    return value


def exact_fields(table, names, label, optional=()):
    require(isinstance(table, Table), f"{label} must be a table")
    keys = [key for key, _ in table.entries]
    require(all(isinstance(key, bytes) for key in keys), f"{label} has positional entries")
    present = {key.decode() for key in keys}
    require(set(names) <= present <= set(names) | set(optional),
            f"{label} has missing or unknown fields")


def array(value, label):
    require(isinstance(value, Table), f"{label} must be a table")
    items = []
    for index, (key, item) in enumerate(value.entries, 1):
        require(type(key) is int and key == index, f"{label} must be a plain array")
        items.append(item)
    return items


def dependency_field(item):
    """An exact { id, version }, or only { id }, which any version satisfies."""
    require(isinstance(item, Table), "dependency must be a table")
    if item.field("version") is None:
        exact_fields(item, ("id",), "dependency")
        return {"id": id_field(item)}
    exact_fields(item, ("id", "version"), "dependency")
    return {"id": id_field(item), "version": text_field(item, "version", VERSION_PATTERN)}


def id_field(table):
    value = text_field(table, "id", ID_PATTERN)
    require(value not in FIXED_ROOT_NAMES, f"package ID {value!r} is a fixed root name")
    return value


def abi_check(kind, abi):
    """abi is required on program, toolchain and dev packages and refused on data."""
    if kind in BOUND_KINDS:
        require(abi is not None, f"a {kind} package needs abi")
    else:
        require(abi is None, f"a {kind} package cannot name an abi")


def optional_abi(table):
    if table.field("abi") is None:
        return None
    return text_field(table, "abi", ABI_PATTERN)


def command_name(name):
    raw = name.encode("utf-8", errors="strict")
    require(0 < len(raw) <= 255 and name not in (".", "..") and
            not any(byte <= 0x20 or byte == 0x7f or byte in b"/:\\" for byte in raw),
            f"invalid command name: {name!r}")


def commands_check(value):
    if value is None:
        return {}
    require(isinstance(value, Table), "commands must be a table")
    commands = {}
    for key, item in value.entries:
        require(isinstance(key, bytes), "commands must be a named map")
        name = key.decode("utf-8", errors="strict")
        command_name(name)
        exact_fields(item, ("path",), "command target", ("command",))
        path = text_field(item, "path")
        path_check(path)
        parts = path.split("/")
        require(not any(part.endswith(".pxb") for part in parts[:-1]),
                f"command path traverses a bundle: {path!r}")
        for depth in range(1, len(parts)):
            require(ustar_split(f"{TREE_NAME}/{'/'.join(parts[:depth])}/".encode()),
                    f"command directory does not fit USTAR: {path!r}")
        target = {"path": path}
        if item.field("command") is not None:
            command = text_field(item, "command")
            command_name(command)
            require(name == command, "bundle export name must match its declared command")
            require(path.endswith(".pxb"), f"bundle command path must end in .pxb: {path!r}")
            require(ustar_split(f"{TREE_NAME}/{path}/".encode()),
                    f"bundle directory does not fit USTAR: {path!r}")
            target["command"] = command
        else:
            require(path.endswith(".pxe"), f"plain command path must end in .pxe: {path!r}")
        commands[name] = target
    return commands


def command_targets_check(commands, files, read_file):
    sizes = {path: size for path, size, _ in files}
    bundles = {}
    for target in commands.values():
        path = target["path"]
        if "command" not in target:
            require(path in sizes, f"missing command target: {path!r}")
            require(read_file(path, len(pxa.P1F_MAGIC)) == pxa.P1F_MAGIC,
                    f"command is not a native P1F file: {path!r}")
            continue
        if path not in bundles:
            prefix = path + "/"
            entries, tree = {}, pxa.NameTree()
            try:
                for member, size in sizes.items():
                    if not member.startswith(prefix):
                        continue
                    relative = member[len(prefix):]
                    parts = relative.split("/")
                    # Inventory admission already checks names; ZIP host budgets
                    # do not apply to an unpacked package bundle.
                    for depth in range(1, len(parts)):
                        tree.nodes["/".join(parts[:depth])] = True
                    tree.nodes[relative] = False
                    entries[relative] = pxa.Entry(relative, relative.encode(), False, size,
                                                 prefix=read_file(member, len(pxa.P1F_MAGIC)))
                manifest_path = prefix + "manifest.json"
                require(manifest_path in sizes, f"missing bundle manifest: {path!r}")
                require(sizes[manifest_path] <= pxa.MAX_MANIFEST, "bundle manifest byte limit")
                data = read_file(manifest_path, sizes[manifest_path])
                bundles[path] = pxa.manifest_check(data, entries, tree)
            except (pxa.ProfileError, ValueError, RecursionError) as error:
                raise PackageError(f"invalid command bundle {path!r}: {error}") from error
        require(target["command"] in bundles[path]["commands"],
                f"bundle does not declare command {target['command']!r}: {path!r}")


def manifest_check(table):
    exact_fields(table, ("id", "version", "kind", "depends", "files", "bytes",
                         "inventory_sha256"), "package.lua", ("abi", "commands"))
    manifest = {
        "id": id_field(table),
        "version": text_field(table, "version", VERSION_PATTERN),
        "kind": text_field(table, "kind"),
        "files": integer_field(table, "files", 0, MAX_FILES),
        "bytes": integer_field(table, "bytes"),
        "inventory_sha256": text_field(table, "inventory_sha256", SHA256_PATTERN),
    }
    require(manifest["kind"] in KINDS, f"unknown kind: {manifest['kind']}")
    manifest["abi"] = optional_abi(table)
    abi_check(manifest["kind"], manifest["abi"])
    depends = []
    for item in array(table.field("depends"), "depends"):
        dependency = dependency_field(item)
        require(dependency not in depends, "duplicate dependency")
        depends.append(dependency)
    require(all(dependency["id"] != manifest["id"] for dependency in depends),
            "a package cannot depend on itself")
    manifest["depends"] = depends
    manifest["commands"] = commands_check(table.field("commands"))
    require(not manifest["commands"] or manifest["kind"] in COMMAND_KINDS,
            "only program and toolchain packages export commands")
    return manifest


def index_check(table):
    exact_fields(table, ("packages",), INDEX_NAME)
    return entries_check(table.field("packages"))


def snapshot_check(table):
    """A published index/N.lua: the working entries plus who, which and when."""
    exact_fields(table, ("repository", "serial", "signed", "packages"), "published index",
                 ("signer",))
    header = {
        "repository": text_field(table, "repository", ID_PATTERN),
        "serial": integer_field(table, "serial", 1, SERIAL_MAX),
        "signed": text_field(table, "signed", SIGNED_PATTERN),
    }
    if table.field("signer") is not None:
        header["signer"] = text_field(table, "signer", SIGNER_PATTERN)
    return header, entries_check(table.field("packages"))


def entries_check(packages):
    entries, seen = [], set()
    for item in array(packages, "packages"):
        exact_fields(item, ("id", "version", "kind", "archive", "size", "sha256"), "index entry",
                     ("abi", "provides"))
        entry = {
            "id": id_field(item),
            "version": text_field(item, "version", VERSION_PATTERN),
            "kind": text_field(item, "kind"),
        }
        require(entry["kind"] in KINDS, f"unknown kind: {entry['kind']}")
        abi = optional_abi(item)
        abi_check(entry["kind"], abi)
        if abi is not None:
            entry["abi"] = abi
        entry["archive"] = text_field(item, "archive")
        entry["size"] = integer_field(item, "size", 1)
        entry["sha256"] = text_field(item, "sha256", SHA256_PATTERN)
        provides = item.field("provides")
        require((provides is not None) == (entry["kind"] == "release"),
                "provides is required on release entries and refused on others")
        if provides is not None:
            entry["provides"] = abi_list(provides, "provides")
            require(entry["provides"], "a release provides at least one ABI")
        require(entry["archive"] == archive_path(entry["id"], entry["version"]),
                f"archive path must be {archive_path(entry['id'], entry['version'])}")
        key = (entry["id"], entry["version"])
        require(key not in seen, f"duplicate index entry {key[0]} {key[1]}")
        seen.add(key)
        entries.append(entry)
    release_route_check(entries)
    return entries


def release_entries(entries):
    return [entry for entry in entries
            if entry["kind"] == "release" and entry["id"] in RELEASE_IDS]


def release_route_check(entries):
    route = release_entries(entries)
    if route:
        stock_abi_check(entries)
    versions = [entry["version"] for entry in route]
    require(len(set(versions)) == len(versions), "the release route repeats a version")
    modern = False
    bootstrap = 0
    anchor = False
    for entry in route:
        legacy = entry["id"] == "pyxis"
        require(not legacy or not modern,
                "the release route changes the permanent legacy checkpoint")
        if legacy:
            if anchor:
                bootstrap += 1
                require(bootstrap == 1,
                        "only one bootstrap checkpoint may follow the shipped 0.0.10 selector")
            if entry["version"] == LEGACY_ROUTE_RELEASE:
                anchor = True
        else:
            require(anchor and bootstrap == 1,
                    "publish the permanent pyxis bootstrap checkpoint after 0.0.10 first")
            modern = True


def stock_abi_check(entries):
    # Shipped 0.0.10 validates every entry before filtering for its own ABI.
    require(all("+" not in entry.get("abi", "") and
                all("+" not in name for name in entry.get("provides", []))
                for entry in entries),
            "stock release catalogs cannot contain development ABIs; use a separate development repository")


def release_order_preserved(entries, published):
    old = release_entries(published)
    new = release_entries(entries)
    require(new[:len(old)] == old,
            "published releases/checkpoints must remain unchanged and in publication order")


def abi_list(value, label):
    names = []
    for name in array(value, label):
        require(isinstance(name, bytes) and ABI_PATTERN.fullmatch(name.decode("utf-8", errors="replace")),
                f"{label}: invalid ABI name")
        require(name.decode() not in names, f"{label}: duplicate ABI name")
        names.append(name.decode())
    return names


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
    require(ustar_split(f"{TREE_NAME}/{path}".encode()), f"path does not fit USTAR: {path!r}")
    return raw


def inventory_text(files):
    lines = [f"{digest} {size} {path}\n" for path, size, digest in files]
    return "".join(lines).encode("utf-8")


def inventory_parse(data):
    """A package without files, such as one that only lists dependencies, has an
    empty inventory."""
    files, previous = [], None
    if not data:
        return files
    require(data.endswith(b"\n") and b"\r" not in data, "inventory must be LF-terminated lines")
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
    """Members after the metadata: tree/, then each file's missing ancestors and the file.

    Sorted paths keep every directory's contents contiguous, so a directory never
    reappears once a later path leaves it; a file may not also be an ancestor."""
    members, stack = [(f"{TREE_NAME}/", None)], []
    file_paths = set()
    for path, size, _ in files:
        parts = path.split("/")
        common = 0
        while common < len(stack) and common < len(parts) - 1 and stack[common] == parts[common]:
            common += 1
        del stack[common:]
        for depth in range(common, len(parts) - 1):
            stack.append(parts[depth])
            directory = "/".join(parts[:depth + 1])
            require(directory not in file_paths, f"{directory} is both a file and a directory")
            name = f"{TREE_NAME}/{directory}/"
            require(ustar_split(name.encode()), f"directory does not fit USTAR: {directory!r}")
            members.append((name, None))
        file_paths.add(path)
        members.append((f"{TREE_NAME}/{path}", size))
    return members


# Archives: plain USTAR, written and read by this tool rather than tarfile, so
# the host accepts exactly what the format document allows.

BLOCK = 512


def ustar_split(full):
    """Split a member name into (prefix, name): the whole name when it fits 100
    bytes, else at the first slash leaving at most 100 nonempty name bytes after
    a prefix of at most 155. None when it cannot be stored."""
    if 0 < len(full) <= 100:
        return b"", full
    for index in range(len(full)):
        if full[index:index + 1] == b"/" and index <= 155:
            name = full[index + 1:]
            if 0 < len(name) <= 100 and name != b"/":
                return full[:index], name
    return None


def octal_field(value, width):
    return b"%0*o\0" % (width - 1, value)


def header(full, size):
    prefix, name = ustar_split(full)
    directory = size is None
    block = bytearray(BLOCK)
    block[0:len(name)] = name
    block[100:108] = octal_field(0o755 if directory else 0o644, 8)
    block[108:116] = octal_field(0, 8)
    block[116:124] = octal_field(0, 8)
    block[124:136] = octal_field(0 if directory else size, 12)
    block[136:148] = octal_field(0, 12)
    block[148:156] = b" " * 8
    block[156:157] = b"5" if directory else b"0"
    block[257:265] = b"ustar\x0000"
    block[329:337] = octal_field(0, 8)
    block[337:345] = octal_field(0, 8)
    block[345:345 + len(prefix)] = prefix
    block[148:156] = b"%06o\0 " % sum(block)
    return bytes(block)


def write_archive(output, manifest_bytes, inventory_bytes, files, sources):
    def member(full, data_source, size):
        output.write(header(full.encode("utf-8"), size))
        if size is None:
            return
        written = 0
        for block in iter(lambda: data_source.read(CHUNK), b""):
            output.write(block)
            written += len(block)
        require(written == size, f"{full} changed while packing")
        output.write(bytes(-written % BLOCK))

    for name, data in ((MANIFEST_NAME, manifest_bytes), (INVENTORY_NAME, inventory_bytes)):
        member(name, io.BytesIO(data), len(data))
    for name, size in member_order(files):
        if size is None:
            member(name, None, None)
            continue
        with open(sources[name[len(TREE_NAME) + 1:]], "rb") as source:
            member(name, source, size)
    output.write(bytes(2 * BLOCK))


def numeric_field(field, label):
    """The one encoding the packer writes: zero-padded octal filling the field
    except its last byte, which is NUL. No base-256, spaces or short fields."""
    require(re.fullmatch(rb"[0-7]+\0", field), f"invalid {label} field")
    return int(field[:-1], 8)


def text_field_bytes(field, label):
    data, _, padding = field.partition(b"\0")
    require(padding.count(0) == len(padding), f"{label} field is not NUL-padded")
    return data


def read_ustar(raw):
    """Yield (name, size or None, data offset) for each member, strictly."""
    raw.seek(0, os.SEEK_END)
    total = raw.tell()
    require(total % BLOCK == 0, "archive size is not a multiple of 512")
    offset = 0
    while True:
        require(offset + BLOCK <= total, "archive lacks its end-of-archive blocks")
        raw.seek(offset)
        block = raw.read(BLOCK)
        if block.count(0) == BLOCK:
            raw.seek(offset)
            rest = raw.read()
            require(len(rest) >= 2 * BLOCK and rest.count(0) == len(rest),
                    "archive must end with zero blocks and nothing else")
            return
        require(re.fullmatch(rb"[0-7]{6}\0 ", block[148:156]), "invalid checksum field")
        stored = int(block[148:154], 8)
        require(stored == sum(block[:148]) + 8 * 32 + sum(block[156:]), "header checksum mismatch")
        require(block[257:265] == b"ustar\x0000", "not a plain USTAR header")
        typeflag = block[156:157]
        require(typeflag in (b"0", b"5"), f"unsupported member type {typeflag!r}")
        for start, end, label in ((100, 108, "mode"), (108, 116, "uid"), (116, 124, "gid"),
                                  (136, 148, "mtime"), (329, 337, "devmajor"), (337, 345, "devminor")):
            numeric_field(block[start:end], label)
        size = numeric_field(block[124:136], "size")
        require(block[157:257].count(0) == 100, "link names are unsupported")
        require(block[265:329].count(0) == 64, "owner names must be empty")
        name = text_field_bytes(block[0:100], "name")
        prefix = text_field_bytes(block[345:500], "prefix")
        require(block[500:512].count(0) == 12, "header padding is not zero")
        require(name, "empty name field")
        full = prefix + b"/" + name if prefix else name
        require(ustar_split(full) == (prefix, name), "member name is not split as the format requires")
        text = full.decode("utf-8", errors="strict")
        if typeflag == b"5":
            require(text.endswith("/") and size == 0, "directory needs a trailing slash and size 0")
            yield text, None, offset + BLOCK
        else:
            require(not text.endswith("/"), "file name ends with a slash")
            yield text, size, offset + BLOCK
        data_end = offset + BLOCK + size
        padded = data_end + (-size % BLOCK)
        require(padded <= total, "member data runs past the archive")
        raw.seek(data_end)
        gap = raw.read(padded - data_end)
        require(gap.count(0) == len(gap), "nonzero bytes outside members")
        offset = padded


def read_at(raw, offset, size, limit, label):
    require(size <= limit, f"{label} too large")
    raw.seek(offset)
    data = raw.read(size)
    require(len(data) == size, f"short {label}")
    return data


def check_archive(path, expected=None):
    """Verify one archive in the documented order; return its manifest."""
    with open(path, "rb") as raw:
        members = read_ustar(raw)
        first = [next(members, None) for _ in range(2)]
        require(all(first) and [m[0] for m in first] == [MANIFEST_NAME, INVENTORY_NAME]
                and all(m[1] is not None for m in first), "archive must start with package.lua, package.files")
        manifest_bytes = read_at(raw, first[0][2], first[0][1], LITERAL_SOURCE_LIMIT, MANIFEST_NAME)
        inventory_bytes = read_at(raw, first[1][2], first[1][1], MAX_FILES * MAX_INVENTORY_LINE,
                                  INVENTORY_NAME)
        manifest = manifest_check(parse_literal(manifest_bytes))
        if expected is not None:
            for field in ("id", "version", "kind"):
                require(manifest[field] == expected[field], f"package.lua {field} does not match the index")
            require(manifest["abi"] == expected.get("abi"), "package.lua abi does not match the index")
        require(hashlib.sha256(inventory_bytes).hexdigest() == manifest["inventory_sha256"],
                "inventory hash does not match package.lua")
        files = inventory_parse(inventory_bytes)
        require(len(files) == manifest["files"], "file count does not match package.lua")
        require(sum(size for _, size, _ in files) == manifest["bytes"], "byte total does not match")
        order = iter(member_order(files))
        digests = {f"{TREE_NAME}/{path}": digest for path, _, digest in files}
        offsets = {}
        for name, size, data_offset in members:
            require(next(order, None) == (name, size), f"unexpected member {name}")
            if size is None:
                continue
            offsets[name[len(TREE_NAME) + 1:]] = (data_offset, size)
            raw.seek(data_offset)
            digest, remaining = hashlib.sha256(), size
            while remaining:
                block = raw.read(min(CHUNK, remaining))
                require(block, f"short member {name}")
                digest.update(block)
                remaining -= len(block)
            require(digest.hexdigest() == digests[name], f"hash mismatch: {name}")
        require(next(order, None) is None, "archive members do not equal the inventory")
        def read_file(member, count):
            offset, size = offsets[member]
            return read_at(raw, offset, min(size, count), count, member)
        command_targets_check(manifest["commands"], files, read_file)
    return manifest


def file_digest(path):
    digest, size = hashlib.sha256(), 0
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(CHUNK), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


# Recipes: trusted host input, the same literal shape as boot/initrd.lua entries.

def raise_error(error):
    raise error


# A letter release such as 0.0.8a keeps its base release's ABI
# (docs/interfaces/abi.md).
LETTER_RELEASE_PATTERN = re.compile(r"([0-9]+\.[0-9]+\.[0-9]+)[a-z]")


def release_abi(release):
    match = LETTER_RELEASE_PATTERN.fullmatch(release)
    return match.group(1) if match else release


def build_identity(directory):
    """The ABI a build directory's packages name, and the label of their versions.

    The build's os-release decides: a tagged release build names its release's
    ABI and its versions carry the release tag. A development package names its
    full base+REV version as ABI and label. The record must be the generator's
    current output from a clean checkout, so it cannot claim an edited ABI list
    or an identity belonging to another source tree."""
    root = Path(directory).resolve()
    record = root / "os-release"
    require(record.is_file(), f"no {record}; build it with make")
    table = parse_literal(record.read_bytes())
    require(isinstance(table, Table), f"{record} must be a table")
    exact_fields(table, ("revision", "abi"), "os-release",
                 ("release", "version", "upgrade_from", "signed_floor"))
    revision = text_field(table, "revision")
    require(REVISION_PATTERN.fullmatch(revision), f"{root} was not built from a Git checkout")
    listed = abi_list(table.field("abi"), "os-release abi")
    release = None
    if table.field("release") is not None:
        release = text_field(table, "release")

    def git(*arguments):
        result = subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, text=True)
        require(result.returncode == 0, f"git {arguments[0]} failed: {result.stderr.strip()}")
        return result.stdout

    head = git("rev-parse", "--short=12", "HEAD").strip()
    require(head == revision, f"the build is for {revision} but the checkout is at {head}; rebuild it")
    changes = git("status", "--porcelain", "--ignore-submodules=none").splitlines()
    if changes:
        raise PackageError(f"the checkout has local changes, so {revision} would not name "
                           f"these bytes: {changes[0].strip()}")
    source = Path(git("rev-parse", "--show-toplevel").strip())
    generator = Path(__file__).resolve().parent.parent / "scripts/os-release.sh"
    generated = subprocess.run(["sh", str(generator)], cwd=source, capture_output=True)
    require(generated.returncode == 0,
            f"cannot derive build identity: {generated.stderr.decode(errors='replace').strip()}")
    require(record.read_bytes() == generated.stdout,
            f"{record} differs from the current generated build identity; rebuild it")
    if release:
        require(listed == [release_abi(release)],
                f"release {release} must provide only its canonical ABI")
        return root, release_abi(release), release
    version = text_field(table, "version", VERSION_PATTERN)
    require(version in listed, f"{record} does not provide its exact development ABI {version}")
    return root, version, version


def recipe_sources(recipe_path, table, build):
    base = Path(recipe_path).resolve().parent
    sources = {}

    def add(path, source):
        path_check(path)
        require(path not in sources, f"two sources for {path}")
        sources[path] = source

    def add_tree(root, at, exclude=()):
        require(stat.S_ISDIR(os.lstat(root).st_mode), f"not a directory: {root}")
        for directory, subdirectories, names_in in os.walk(root, onerror=raise_error):
            if Path(directory) == Path(root):
                for name in exclude:
                    require(name in subdirectories or name in names_in,
                            f"excluded entry is not in the tree: {name!r}")
                subdirectories[:] = [name for name in subdirectories if name not in exclude]
                names_in = [name for name in names_in if name not in exclude]
            subdirectories.sort()
            require(subdirectories or names_in, f"empty directories are unsupported: {directory}")
            relative = Path(directory).relative_to(root)
            for name in subdirectories:
                require(stat.S_ISDIR(os.lstat(Path(directory) / name).st_mode),
                        f"symbolic link or special entry: {Path(directory) / name}")
            for name in names_in:
                source = Path(directory) / name
                require(stat.S_ISREG(os.lstat(source).st_mode),
                        f"symbolic link or special entry: {source}")
                parts = [part for part in (at, *relative.parts, name) if part and part != "."]
                add("/".join(parts), source)

    # A tree may leave out entries at its top level with exclude.
    shapes = [{"file", "at"}, {"tree", "at"}, {"tree", "at", "exclude"}]
    if build is not None:
        shapes += [{"build", "at"}, {"build", "at", "exclude"}]
    for item in array(table.field("entries"), "entries"):
        require(isinstance(item, Table), "recipe entry must be a table")
        names = {key.decode() for key, _ in item.entries if isinstance(key, bytes)}
        require(len(item.entries) == len(names) and names in shapes,
                "recipe entry needs file, tree or (with --build) build, and at, and a tree may exclude")
        at = text_field(item, "at")
        exclude = []
        if "exclude" in names:
            for value in array(item.field("exclude"), "exclude"):
                require(isinstance(value, bytes), "exclude names entries by string")
                name = value.decode("utf-8", errors="strict")
                require(name and "/" not in name and name not in (".", "..") and name not in exclude,
                        f"invalid exclude entry: {name!r}")
                exclude.append(name)
        if "file" in names:
            source = base / text_field(item, "file")
            require(stat.S_ISREG(os.lstat(source).st_mode), f"not a regular file: {source}")
            add(at, source)
        elif "tree" in names:
            add_tree(base / text_field(item, "tree"), at, exclude)
        else:
            relative = text_field(item, "build")
            parts = relative.split("/")
            require(not relative.startswith("/") and all(p not in ("", ".", "..") for p in parts),
                    f"build path must stay inside the build directory: {relative!r}")
            source = build / relative
            mode = os.lstat(source).st_mode
            if stat.S_ISREG(mode):
                require(not exclude, "exclude applies only to trees")
                add(at, source)
            else:
                add_tree(source, at, exclude)
    return sources


# Publication. One writer at a time holds the repository lock; readers of a
# served repository see only whole archives and whole indexes.

LOCK_NAME = ".lock"
TEMPORARY_PATTERN = re.compile(r"\..*\.tmp")


def sync_directory(path):
    fd = os.open(path, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def temporary_file(directory, label):
    fd, name = tempfile.mkstemp(prefix=f".{label}-", suffix=".tmp", dir=directory)
    os.fchmod(fd, 0o644)
    return fd, Path(name)


def write_atomically(target, data):
    """Replace target whole; the caller syncs the directory afterwards."""
    fd, name = temporary_file(target.parent, target.name)
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.replace(name, target)
        name = None
    finally:
        if name is not None:
            name.unlink()


class RepositoryLock:
    """flock on .lock: packs take it exclusively and create it; check shares an
    existing one and works without it on a read-only copy."""

    def __init__(self, repository, exclusive):
        self.path = Path(repository) / LOCK_NAME
        self.exclusive = exclusive
        self.file = None

    def __enter__(self):
        if self.exclusive:
            self.file = open(self.path, "a")
        elif self.path.exists():
            self.file = open(self.path, "r")
        if self.file is not None:
            fcntl.flock(self.file, fcntl.LOCK_EX if self.exclusive else fcntl.LOCK_SH)
        return self

    def __exit__(self, *_):
        if self.file is not None:
            self.file.close()


def clean_leftovers(repository):
    """Under the exclusive lock, remove what interrupted packs left behind."""
    root = Path(repository)
    directories = [root] + [d for d in (root / "packages").glob("*") if d.is_dir()]
    for directory in directories:
        for path in directory.iterdir():
            if path.is_file() and TEMPORARY_PATTERN.fullmatch(path.name):
                path.unlink()
        if directory != root and not any(directory.iterdir()):
            directory.rmdir()


def load_index(repository):
    path = Path(repository) / INDEX_NAME
    if not path.exists():
        return []
    return index_check(parse_literal(path.read_bytes(), sized=True))


def index_document(entries):
    return literal_document({"packages": [dict(entry) for entry in entries]})


def recipe_package(recipe_path, build_directory=None):
    """Program, toolchain and dev recipes take the ABI of build_directory, and may
    pack files from it. A recipe that names a variant, as every program recipe
    does, is versioned VARIANT+LABEL for that build; other recipes name their
    version. A data recipe takes a build only with a variant."""
    table = parse_literal(Path(recipe_path).read_bytes())
    require(isinstance(table, Table), "recipe must be a table")
    kind = text_field(table, "kind")
    require(kind in KINDS, f"unknown kind: {kind}")
    package_id = id_field(table)
    if kind == "release":
        return release_package(recipe_path, table, package_id, build_directory)
    variant = table.field("variant") is not None
    require(variant or kind != "program", "a program recipe needs variant")
    require(not variant or kind != "toolchain", "a toolchain recipe names its version")
    build, abi, label = None, None, None
    if kind in BOUND_KINDS or variant:
        require(build_directory is not None,
                "a recipe with variant needs --build DIR" if variant else f"a {kind} recipe needs --build DIR")
        build, build_abi, label = build_identity(build_directory)
        if kind in BOUND_KINDS:
            abi = build_abi
    else:
        require(build_directory is None,
                "--build applies only to program, toolchain and dev recipes and recipes with variant")
    if variant:
        exact_fields(table, ("id", "variant", "kind", "depends", "entries"), "recipe", ("commands",))
        variant = text_field(table, "variant", VARIANT_PATTERN)
        version = f"{variant}+{label}"
        require(VERSION_PATTERN.fullmatch(version), f"version too long: {version}")
    else:
        exact_fields(table, ("id", "version", "kind", "depends", "entries"), "recipe", ("commands",))
        version = text_field(table, "version", VERSION_PATTERN)
    sources = recipe_sources(recipe_path, table, build)
    depends = []
    for item in array(table.field("depends"), "depends"):
        dependency = dependency_field(item)
        require(dependency["id"] != package_id, "a package cannot depend on itself")
        require(dependency not in depends, "duplicate dependency")
        depends.append(dependency)
    files = []
    for path in sorted(sources, key=lambda p: p.encode("utf-8")):
        digest, size = file_digest(sources[path])
        require(size <= MAX_FILE_SIZE, f"file too large: {path}")
        files.append((path, size, digest))
    require(len(files) <= MAX_FILES, "too many files")
    inventory_bytes = inventory_text(files)
    inventory_parse(inventory_bytes)
    member_order(files)
    commands = commands_check(table.field("commands"))
    require(not commands or kind in COMMAND_KINDS, "only program and toolchain packages export commands")
    def read_file(path, count):
        with open(sources[path], "rb") as source:
            return source.read(count)
    command_targets_check(commands, files, read_file)
    manifest = {"id": package_id, "version": version, "kind": kind}
    if abi is not None:
        manifest["abi"] = abi
    manifest.update({
        "depends": depends, "files": len(files), "bytes": sum(s for _, s, _ in files),
        "inventory_sha256": hashlib.sha256(inventory_bytes).hexdigest(),
    })
    if table.field("commands") is not None:
        manifest["commands"] = commands
    manifest_bytes = literal_document(manifest)
    manifest_check(parse_literal(manifest_bytes))
    return package_id, version, kind, abi, None, files, sources, manifest_bytes, inventory_bytes


def release_build(build_directory):
    """A clean build of a release tag, as build_identity checks, whose image
    lists no unsigned repository: what a release publishes runs on users'
    machines, and only development images use unsigned ones (decision 72)."""
    build, _, label = build_identity(build_directory)
    record = parse_literal((build / "os-release").read_bytes())
    release = record.field("release")
    require(release is not None, f"{build} is not a tagged release build")
    require(label == release.decode(), "the build's release and label differ")
    config = parse_literal((build / "packages.lua").read_bytes())
    require(isinstance(config, Table), f"{build}/packages.lua must be a table")
    for entry in array(config.field("repositories"), "packages.lua repositories"):
        require(isinstance(entry, Table) and entry.field("signed") is None,
                f"{build}/packages.lua lists an unsigned repository; a release build "
                f"needs PACKAGE_REPOSITORY_SIGNED=1")
    return build, record, label


def release_package(recipe_path, table, package_id, build_directory):
    """A tagged, clean build's RELEASE_FILES, versioned by its release tag and
    providing its os-release ABI list."""
    exact_fields(table, ("id", "kind", "depends", "entries"), "release recipe")
    require(build_directory is not None, "a release recipe needs --build DIR")
    require(not array(table.field("depends"), "depends"), "a release has no dependencies")
    build, record, label = release_build(build_directory)
    provides = abi_list(record.field("abi"), "os-release abi")
    require(provides, f"release {label} provides no ABI")
    sources = recipe_sources(recipe_path, table, build)
    require(sorted(sources) == sorted(RELEASE_FILES),
            f"a release packs exactly {', '.join(RELEASE_FILES)}")
    files = []
    for path in sorted(sources, key=lambda p: p.encode("utf-8")):
        digest, size = file_digest(sources[path])
        files.append((path, size, digest))
    inventory_bytes = inventory_text(files)
    inventory_parse(inventory_bytes)
    member_order(files)
    manifest = {"id": package_id, "version": label, "kind": "release", "depends": [],
                "files": len(files), "bytes": sum(s for _, s, _ in files),
                "inventory_sha256": hashlib.sha256(inventory_bytes).hexdigest()}
    manifest_bytes = literal_document(manifest)
    manifest_check(parse_literal(manifest_bytes))
    return (package_id, label, "release", None, provides, files, sources, manifest_bytes,
            inventory_bytes)


def sums_parse(data):
    """SHA256SUMS in sha256sum's text format: one line per published ISO."""
    entries = {}
    for line in data.decode("utf-8", errors="strict").splitlines():
        match = SUMS_LINE.fullmatch(line)
        require(match, f"invalid SHA256SUMS line: {line!r}")
        require(match[2] not in entries, f"SHA256SUMS lists {match[2]} twice")
        entries[match[2]] = match[1]
    require(data.endswith(b"\n") or not data, "SHA256SUMS must end with a newline")
    return entries


def release_image(build_directory, directory):
    """Copy a release build's install ISO into DIR as pyxis-VERSION.iso and add
    its line to DIR/SHA256SUMS, keeping every earlier release's (decision 75).
    A published ISO never changes, so other bytes under a listed name are refused."""
    build, _, label = release_build(build_directory)
    source = build / "pyxis.iso"
    require(source.is_file(), f"no {source}; build it with make image")
    name = f"pyxis-{label}.iso"
    root = Path(directory)
    if not root.exists():
        root.mkdir(parents=True)
        sync_directory(root.parent)
    sums = root / "SHA256SUMS"
    entries = sums_parse(sums.read_bytes()) if sums.exists() else {}
    digest, size = file_digest(source)
    require(entries.get(name, digest) == digest,
            f"{name} is already listed in {sums} with other bytes")
    target = root / name
    if target.exists():
        require(file_digest(target)[0] == digest, f"{target} already exists with other bytes")
    else:
        fd, temporary = temporary_file(root, name)
        try:
            with open(source, "rb") as original, os.fdopen(fd, "wb") as output:
                for block in iter(lambda: original.read(CHUNK), b""):
                    output.write(block)
                output.flush()
                os.fsync(output.fileno())
            require(file_digest(temporary)[0] == digest, f"{source} changed while it was copied")
            os.replace(temporary, target)
            temporary = None
        finally:
            if temporary is not None:
                temporary.unlink()
    if name not in entries:
        entries[name] = digest
        write_atomically(sums, "".join(f"{sha}  {iso}\n" for iso, sha in entries.items()).encode())
    sync_directory(root)
    return target, digest, size


def pack(recipe_path, repository, build_directory=None):
    package_id, version, kind, abi, provides, files, sources, manifest_bytes, inventory_bytes = \
        recipe_package(recipe_path, build_directory)
    root = Path(repository)
    if not root.exists():
        root.mkdir(parents=True)
        sync_directory(root.parent)
    with RepositoryLock(root, exclusive=True):
        clean_leftovers(root)
        entries = load_index(root)
        previous = read_current(root)
        if previous:
            header, _ = load_snapshot(root, previous)
            if header["repository"] in ("public", "releases"):
                stock_abi_check([{"abi": abi or "", "provides": provides or []}])
        require(all((e["id"], e["version"]) != (package_id, version) for e in entries),
                f"{package_id} {version} is already published")
        target = root / archive_path(package_id, version)
        require(not target.exists(), f"{target} already exists")
        for directory in (root / "packages", target.parent):
            if not directory.exists():
                directory.mkdir()
                sync_directory(directory.parent)

        fd, temporary = temporary_file(target.parent, version)
        published = False
        try:
            with os.fdopen(fd, "w+b") as output:
                write_archive(output, manifest_bytes, inventory_bytes, files, sources)
                output.flush()
                os.fsync(output.fileno())
            check_archive(temporary)
            digest, size = file_digest(temporary)
            entry = {"id": package_id, "version": version, "kind": kind}
            if abi is not None:
                entry["abi"] = abi
            entry.update({"archive": archive_path(package_id, version), "size": size,
                          "sha256": digest})
            if provides is not None:
                entry["provides"] = provides
            # The new index must be valid before anything becomes visible.
            document = index_document(entries + [entry])
            try:
                index_check(parse_literal(document, sized=True))
            except PackageError as error:
                raise PackageError(f"the index cannot take another entry ({error})") from None
            os.link(temporary, target)
            published = True
            sync_directory(target.parent)
            write_atomically(root / INDEX_NAME, document)
            published = False
            sync_directory(root)
        except BaseException:
            if published:
                target.unlink()
                sync_directory(target.parent)
            raise
        finally:
            temporary.unlink(missing_ok=True)
            sync_directory(target.parent)
            if not any(target.parent.iterdir()):
                target.parent.rmdir()
                sync_directory(target.parent.parent)
    return entry, len(files)


def read_current(root):
    path = root / CURRENT_NAME
    if not path.exists():
        return 0
    text = path.read_bytes()
    require(re.fullmatch(rb"[1-9][0-9]{0,18}\n", text), f"{path} must hold one serial")
    return int(text)


def load_snapshot(root, serial):
    path = root / SNAPSHOT_DIRECTORY / f"{serial}.lua"
    header, entries = snapshot_check(parse_literal(path.read_bytes(), sized=True))
    require(header["serial"] == serial, f"{path} names serial {header['serial']}")
    return header, entries


def publish(repository, name, signed_at=None, signer=None):
    """Write the next immutable index/N.lua from the working index, then point
    current at it. A signed publish names the signer certificate, copied beside
    it; tools/sign-repository.sh signs index/N.lua before anything is uploaded."""
    require(ID_PATTERN.fullmatch(name), f"invalid repository name: {name!r}")
    if signed_at is None:
        signed_at = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
    require(SIGNED_PATTERN.fullmatch(signed_at), f"--signed-at must be YYYY-MM-DDTHH:MM:SSZ")
    root = Path(repository)
    require(root.is_dir(), f"no repository at {repository}")
    with RepositoryLock(root, exclusive=True):
        clean_leftovers(root)
        entries = load_index(root)
        if name in ("public", "releases"):
            stock_abi_check(entries)
        previous = read_current(root)
        if previous:
            header, published = load_snapshot(root, previous)
            release_order_preserved(entries, published)
            require(header["repository"] == name,
                    f"{repository} publishes {header['repository']}, not {name}")
            # pkg refuses an index signed before the last one it accepted.
            require(signed_at >= header["signed"],
                    f"--signed-at {signed_at} is before serial {previous}'s {header['signed']}")
        serial = previous + 1
        require(serial <= SERIAL_MAX, "serial exhausted")
        document = {"repository": name, "serial": serial, "signed": signed_at}
        if signer is not None:
            certificate = Path(signer)
            require(SIGNER_PATTERN.fullmatch(certificate.name),
                    f"signer certificate name must match {SIGNER_PATTERN.pattern}")
            data = certificate.read_bytes()
            require(data.startswith(b"-----BEGIN CERTIFICATE-----"), f"{signer} is not a PEM certificate")
            target = root / certificate.name
            # A rotated leaf takes a new name; a published one never changes.
            require(not target.exists() or target.read_bytes() == data,
                    f"{target} exists with other bytes")
            if not target.exists():
                write_atomically(target, data)
            document["signer"] = certificate.name
        document["packages"] = [dict(entry) for entry in entries]
        data = literal_document(document)
        snapshot_check(parse_literal(data, sized=True))
        directory = root / SNAPSHOT_DIRECTORY
        if not directory.exists():
            directory.mkdir()
            sync_directory(root)
        target = directory / f"{serial}.lua"
        require(not target.exists(), f"{target} already exists")
        write_atomically(target, data)
        sync_directory(directory)
        write_atomically(root / CURRENT_NAME, f"{serial}\n".encode())
        sync_directory(root)
    return serial, target


def prune(repository, keep):
    """Remove every bound entry for an ABI not in keep, index first.

    The new index is renamed into place and synced before any archive goes, so
    a served index never names a missing archive. Unbound entries stay. Archives
    the index does not list, which only an interrupted pack or prune leaves, go
    too. A kept ABI with no builds, or an ID that would lose every build, refuses
    before anything changes, so a typo or a prune before repacking cannot empty
    the tree."""
    for abi in keep:
        require(ABI_PATTERN.fullmatch(abi), f"invalid abi: {abi!r}")
    root = Path(repository)
    require(root.is_dir(), f"no repository at {repository}")
    with RepositoryLock(root, exclusive=True):
        clean_leftovers(root)
        entries = load_index(root)
        kept = [e for e in entries if "abi" not in e or e["abi"] in keep]
        removed = [e for e in entries if e not in kept]
        empty = sorted(set(keep) - {e.get("abi") for e in entries})
        require(not empty, f"no builds for kept abi {', '.join(empty)}; pack them before pruning")
        orphaned = sorted({e["id"] for e in removed} - {e["id"] for e in kept})
        require(not orphaned, f"pruning would remove every build of {', '.join(orphaned)}; "
                "pack a build for a kept abi first")
        if removed:
            write_atomically(root / INDEX_NAME, index_document(kept))
            sync_directory(root)
        listed = {root / e["archive"] for e in kept}
        packages = root / "packages"
        unlisted = sorted(path for path in packages.glob("*/*.tar")
                          if path not in listed) if packages.exists() else []
        for target in unlisted:
            require(stat.S_ISREG(os.lstat(target).st_mode), f"not a regular file: {target}")
            target.unlink()
            sync_directory(target.parent)
            if not any(target.parent.iterdir()):
                target.parent.rmdir()
                sync_directory(target.parent.parent)
    return kept, removed, [path.relative_to(root).as_posix() for path in unlisted]


def check(repository, abi=None):
    """Verify the repository. With abi, also refuse when an ID that has bound
    entries has none for abi: a release-tied package was not rebuilt."""
    if abi is not None:
        require(ABI_PATTERN.fullmatch(abi), f"invalid abi: {abi!r}")
    root = Path(repository)
    require(root.is_dir(), f"no repository at {repository}")
    with RepositoryLock(root, exclusive=False):
        entries = load_index(root)
        listed = set()
        for entry in entries:
            path = root / entry["archive"]
            digest, size = file_digest(path)
            require((size, digest) == (entry["size"], entry["sha256"]),
                    f"{entry['archive']}: size or SHA-256 differs from the index")
            check_archive(path, entry)
            listed.add(entry["archive"])
        for path in root.iterdir():
            require(path.name in (INDEX_NAME, LOCK_NAME, "packages", "sources", CURRENT_NAME,
                                  SNAPSHOT_DIRECTORY) or SIGNER_PATTERN.fullmatch(path.name),
                    f"unexpected {path.name}")
        serial = read_current(root)
        if serial:
            # The published snapshot must list only archives that exist as indexed.
            header, published = load_snapshot(root, serial)
            release_order_preserved(entries, published)
            if "signer" in header:
                require((root / header["signer"]).is_file(), f"{header['signer']} is missing")
            for entry in published:
                path = root / entry["archive"]
                require(path.is_file() and file_digest(path) == (entry["sha256"], entry["size"]),
                        f"published serial {serial} lists {entry['archive']}, which differs or is missing")
        packages = root / "packages"
        if packages.exists():
            for path in packages.rglob("*"):
                relative = path.relative_to(root).as_posix()
                require(path.is_dir() and path.parent == packages or relative in listed,
                        f"{relative} is not in the index")
    if abi is not None:
        bound = {e["id"] for e in entries if "abi" in e}
        built = {e["id"] for e in entries if e.get("abi") == abi}
        missing = sorted(bound - built)
        require(not missing, f"no build for abi {abi}: {', '.join(missing)}")
    return entries, len({e["id"] for e in entries if "abi" in e})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack_command = commands.add_parser("pack", help="publish one package from a recipe")
    pack_command.add_argument("--build", metavar="DIR",
                              help="build directory whose ABI a program or toolchain package needs")
    pack_command.add_argument("recipe")
    pack_command.add_argument("repository")
    prune_command = commands.add_parser("prune", help="remove bound entries for other ABIs")
    prune_command.add_argument("repository")
    prune_command.add_argument("--keep-abi", metavar="ABI", nargs="+", required=True)
    publish_command = commands.add_parser(
        "publish", help="write the next index/N.lua and point current at it")
    publish_command.add_argument("--repository", required=True, metavar="NAME",
                                 help="the name pkg configures this repository under")
    publish_command.add_argument("--signed-at", metavar="TIME",
                                 help="UTC signing time, YYYY-MM-DDTHH:MM:SSZ; default now")
    publish_command.add_argument("--signer", metavar="CERT.pem",
                                 help="signer certificate the signature will be checked with")
    publish_command.add_argument("directory")
    image_command = commands.add_parser(
        "image", help="add a release build's install ISO to an images directory")
    image_command.add_argument("--build", metavar="DIR", required=True,
                               help="the tagged release build whose pyxis.iso is added")
    image_command.add_argument("directory")
    check_command = commands.add_parser("check", help="check the index and every archive")
    check_command.add_argument("--abi", help="also require a build for ABI of every bound ID")
    check_command.add_argument("repository")
    args = parser.parse_args()
    try:
        if args.command == "pack":
            entry, count = pack(args.recipe, args.repository, args.build)
            print(f"{entry['archive']}: published {entry['id']} {entry['version']}, "
                  f"{count} files, {entry['size']} archive bytes")
        elif args.command == "publish":
            serial, target = publish(args.directory, args.repository, args.signed_at, args.signer)
            print(f"{target}: published serial {serial} of {args.repository}")
        elif args.command == "image":
            target, digest, size = release_image(args.build, args.directory)
            print(f"{target}: {size} bytes, sha256 {digest}, listed in SHA256SUMS")
        elif args.command == "prune":
            kept, removed, deleted = prune(args.repository, args.keep_abi)
            for entry in removed:
                print(f"{entry['archive']}: removed {entry['id']} {entry['version']} "
                      f"(abi {entry['abi']})")
            for path in sorted(set(deleted) - {entry["archive"] for entry in removed}):
                print(f"{path}: removed an archive the index did not list")
            print(f"{args.repository}: kept {len(kept)} package versions, removed {len(removed)}")
        else:
            entries, bound = check(args.repository, args.abi)
            print(f"{args.repository}: checked {len(entries)} package versions")
            if args.abi:
                print(f"{args.repository}: all {bound} bound package IDs have a build for abi {args.abi}")
        return 0
    except (OSError, ValueError, UnicodeError, RecursionError) as error:
        print(f"pxpkg: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
