# Intel AX200 Bluetooth firmware

Pin: linux-firmware release `20260309`, commit
`c822cbbb14ce5b8ee1f27346220640ac350bbf34`.
The exact paths, uncompressed sizes and SHA-256 identities are recorded in
[metadata.json](metadata.json). All four files come from this upstream root:

https://gitlab.com/kernel-firmware/linux-firmware/-/raw/c822cbbb14ce5b8ee1f27346220640ac350bbf34/

- `intel/ibt-20-1-3.sfi`: unchanged AX200 firmware binary, 801016 bytes.
- `intel/ibt-20-1-3.ddc`: unchanged AX200 device configuration, 9 bytes.
- `LICENCE.ibt_firmware`: complete Intel license, 2040 bytes.
- `WHENCE`: complete upstream provenance index, 421898 bytes. Its entry labels
  these assets `BT_CyclonePeak_A0_REL53636`; this is not a measured running build.

The requested firmware paths are regular files at this pin. Their bytes match
the Fedora deduplicated targets `ibt-20-0-3.sfi` and `ibt-0040-0041.ddc` recorded
in the [investigation](../../docs/development/experiments/bluetooth-task4/README.md#deferred-cold-upload-and-asset-provenance).
The build consumes only the requested upstream paths through the owner's mirror.
Firmware binaries and downloaded license/provenance stay outside Git.

`LICENCE.ibt_firmware` permits binary redistribution and use without modification.
Distributions must reproduce Intel's copyright notice and disclaimer. Intel and
supplier names may not endorse or promote derived products without prior written
permission; reverse engineering, decompilation and disassembly are prohibited.
The limited patent grant covers use of the software alone or with an OS licensed
under an OSI-approved license, excludes other combinations and grants no hardware
license. Ship the complete license, including its disclaimer, rather than this
summary. This dependency retains Intel's terms separately from Pyxis source
licensing. No local modifications are made to any downloaded file.

`scripts/fetch-ax200-firmware.py --metadata firmware/ax200/metadata.json --output
build/firmware/ax200` verifies each file's size and SHA-256, reuses valid cached
files, and atomically replaces invalid or missing files after validation. The
output preserves the four upstream paths and adds `PROVENANCE.json`, generated
from the manifest. Downloads use a finite timeout, reject redirects and read at
most each expected size plus one byte; partial files are removed on failure.

`mirror_root` is pending the owner's exact mirror prefix. An empty prefix fails
before reading cached assets or downloading. The fetcher appends only the
canonical paths from the manifest and never falls back to upstream or installed
host firmware. The URLs above identify provenance, not build download sources.

Image assembly stages the unchanged SFI/DDC at `share/firmware/intel/` and the
complete license, WHENCE and generated provenance at
`share/licenses/intel-bluetooth/`. Kernel compilation is independent of these
assets; image assembly requires every validated file. No compiler-container or
submodule revision change is needed.
