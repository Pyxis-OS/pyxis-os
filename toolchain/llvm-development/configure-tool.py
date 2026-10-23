#!/usr/bin/env python3
"""Keep upstream's generated graph, restricted to the installed archive closure."""
from pathlib import Path
import re
import shutil
import sys


target = Path(sys.argv[1])
destination = Path(sys.argv[2])
destination.mkdir(exist_ok=True)
generated = target / "tools/llvm-config"
text = (generated / "LibraryDependencies.inc").read_text()
pattern = re.compile(r'\{ "([a-z0-9-]+)", (nullptr|"[^"]+"), true, \{([^}]*)\} \},')
rows = {}
for match in pattern.finditer(text):
    name, library, dependencies = match.groups()
    rows[name] = (match, library.strip('"'), re.findall(r'"([^"]+)"', dependencies))
if not rows:
    raise ValueError("Unrecognised generated component graph")
available = {name for name, (_, library, _) in rows.items()
             if name != "all" and (library == "nullptr" or
                (target / "lib" / ("lib" + library + ".a")).is_file())}
while True:
    closed = {name for name in available if all(dep in available for dep in rows[name][2])}
    if closed == available:
        break
    available = closed
for required in ("bitwriter", "engine", "mcdisassembler", "core", "executionengine",
                 "scalaropts", "transformutils", "instcombine", "native", "mcjit"):
    if required not in available:
        raise ValueError("Incomplete requested LLVM closure: " + required)
# Removed components must not be advertised: Meson selects optional modules
# from --components, and upstream marks every configured library installed.
for name, (match, _, _) in reversed(list(rows.items())):
    if name == "all":
        dependencies = ", ".join('"' + entry + '"' for entry in sorted(available))
        text = text[:match.start()] + '{ "all", nullptr, true, {' + dependencies + '} },' + text[match.end():]
    elif name not in available:
        text = text[:match.start()] + text[match.end():]
text, count = re.subn(r'AvailableComponents\[\d+\]', f'AvailableComponents[{len(available) + 1}]', text)
if count != 1:
    raise ValueError("Unrecognised generated component array")
(destination / "LibraryDependencies.inc").write_text(text)
for name in ("BuildVariables.inc", "ExtensionDependencies.inc"):
    shutil.copyfile(generated / name, destination / name)
