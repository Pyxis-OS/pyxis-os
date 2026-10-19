#!/usr/bin/env python3
"""Symbolize Caelum panic and fault backtraces with addr2line.

    tools/symbolize.py [-e build/caelum.elf] < panic.log
    tools/symbolize.py [-e build/caelum.elf] [--return] ADDRESS...

Pasted log lines are echoed with each code address followed by its function
and file:line, inlined callers included. Frames ("#N 0x...") and a panic's
"caller" are return addresses, so the byte before each is looked up to name
the call itself; an exception's "rip" is exact. Raw ADDRESS arguments are
exact unless --return says they are return addresses. KASLR is off, so the
addresses match the ELF that was booted; use that build's caelum.elf.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

KERNEL_BASE = 0xffffffff80000000
# Address tokens the kernel prints, and whether each is a return address.
PATTERNS = (
    (re.compile(r"(?<!\S)(#\d+) (0x[0-9a-f]+)"), True),
    (re.compile(r"\b(caller) (0x[0-9a-f]+)"), True),
    (re.compile(r"\b(rip)[ =](0x[0-9a-f]+)"), False),
)


def addr2line_program():
    program = os.environ.get("ADDR2LINE")
    if program:
        return program
    for candidate in ("addr2line", "llvm-addr2line"):
        if shutil.which(candidate):
            return candidate
    sys.exit("symbolize: no addr2line or llvm-addr2line; set ADDR2LINE")


def lookup(elf, addresses):
    """Map each address to its "function at file:line" lines, inlining first."""
    if not addresses:
        return {}
    unique = sorted(set(addresses))
    result = subprocess.run(
        [addr2line_program(), "-e", elf, "-f", "-i", "-C", "-a"] + [f"0x{a:x}" for a in unique],
        check=True, capture_output=True, text=True).stdout.splitlines()
    # Paths print relative to the checkout the command runs in.
    root = os.getcwd() + os.sep
    symbols, current, function = {}, None, None
    for line in result:
        if re.fullmatch(r"0x[0-9a-f]+", line):
            current = int(line, 16)
            symbols[current] = []
        elif function is None:
            function = line
        else:
            location = line.split(" (discriminator")[0].replace(root, "")
            symbols[current].append(f"{function} at {location}")
            function = None
    return symbols


def describe(symbols, address):
    lines = symbols.get(address) or ["?? at ??:0"]
    first, inlined = lines[0], lines[1:]
    return first + "".join(f" (inlined into {line})" for line in inlined)


def kernel_address(text):
    value = int(text, 16)
    return value if value >= KERNEL_BASE else None


def symbolize_log(elf, lines):
    found = []
    for line in lines:
        for pattern, returns in PATTERNS:
            for match in pattern.finditer(line):
                address = kernel_address(match.group(2))
                if address is not None:
                    found.append((line, match.group(2), address - 1 if returns else address))
    symbols = lookup(elf, [entry[2] for entry in found])
    annotations = {}
    for line, token, address in found:
        annotations.setdefault(line, []).append(f"{token}: {describe(symbols, address)}")
    for line in lines:
        print(line)
        for note in annotations.get(line, []):
            print(f"    {note}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-e", "--elf", default="build/caelum.elf")
    parser.add_argument("--return", dest="returns", action="store_true",
                        help="treat raw addresses as return addresses")
    parser.add_argument("addresses", nargs="*")
    args = parser.parse_args()
    if not os.path.isfile(args.elf):
        sys.exit(f"symbolize: {args.elf} not found; pass the booted kernel with -e")
    if not args.addresses:
        symbolize_log(args.elf, sys.stdin.read().splitlines())
        return
    addresses = [int(text, 16) - (1 if args.returns else 0) for text in args.addresses]
    symbols = lookup(args.elf, addresses)
    for text, address in zip(args.addresses, addresses):
        print(f"{text}: {describe(symbols, address)}")


if __name__ == "__main__":
    main()
