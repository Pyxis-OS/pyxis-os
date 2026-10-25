#!/usr/bin/env python3
"""Link Rust objects through the selected SDK's ordinary Pyxis C link contract."""
import os
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    sdk = os.environ.get("PYXIS_RUST_SDK", "")
    if not sdk or not Path(sdk).is_absolute():
        raise ValueError("PYXIS_RUST_SDK must name an absolute SDK directory")
    fragment = Path(__file__).resolve().with_name("link-flags.mk")
    rows = subprocess.check_output(
        ["make", "--no-print-directory", "-sf", str(fragment), "print-rust-link"],
        text=True).splitlines()
    if len(rows) != 4:
        raise ValueError("SDK did not supply the four expected link settings")
    compiler, flags, start, libraries = map(shlex.split, rows)
    if not compiler or not start or not libraries:
        raise ValueError("SDK link settings are incomplete")
    command = compiler + flags + start + sys.argv[1:] + libraries
    trace = os.environ.get("PYXIS_RUST_LINK_TRACE")
    if trace:
        import json
        with open(trace, "a", encoding="utf-8") as output:
            output.write(json.dumps(command) + "\n")
    subprocess.run(command, check=True)
    debug = os.environ.get("PYXIS_RUST_DEBUG_DIR")
    if debug:
        arguments = sys.argv[1:]
        if "-o" not in arguments:
            raise ValueError("Rust link did not name an output")
        position = arguments.index("-o") + 1
        destination = Path(debug) / (Path(arguments[position]).name + ".elf")
        destination.parent.mkdir(parents=True, exist_ok=True)
        arguments[position] = str(destination)
        subprocess.run(compiler + flags + start + arguments + libraries +
                       ["-Wl,--oformat=elf", "-Wl,-Map," + str(destination.with_suffix(".map"))],
                       check=True)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit(f"Pyxis Rust link failed: {error}")
