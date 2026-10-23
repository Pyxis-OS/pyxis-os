# Licensing

Except for the third-party material identified below and files with their own
license notices, original Pyxis source code, build/configuration files and
project documentation in this repository are licensed under the Mozilla Public
License, version 2.0. The complete, unmodified license is in [LICENSE](LICENSE).

This Source Code Form is subject to the terms of the Mozilla Public License,
v. 2.0. If a copy of the MPL was not distributed with this file, You can obtain
one at https://mozilla.org/MPL/2.0/.

This directory-level notice applies to the original material described above;
it does not replace third-party notices. No Exhibit B incompatibility notice is
applied: MPL's standard secondary-license provisions remain available.

## Third-party and separately licensed material

The inline GitHub Octicons mark in `site/themes/pyxis/layouts/index.html`
is MIT-licensed; the full upstream [notice](site/themes/pyxis/static/licenses/github-mark.txt)
is also included in the generated site. [Provenance](site/README.md#github-mark)
records the pinned source and local wrapper changes.

The owner's original volume artwork in `assets/ui/volume/` (editable Aseprite
source and PNG export) is also licensed under MPL-2.0. Its attribution, supplied
source and conversion are recorded in [the asset note](assets/ui/volume/README.md).

The owner's original power overlay sound in `assets/ui/power/` (jsfxr preset
and WAV export) is also licensed under MPL-2.0. Its attribution, supplied source
and conversion are recorded in [the asset note](assets/ui/power/README.md).

- The optional `gohello` bundle links the BSD-3-Clause Go runtime from the pinned
  [pyxis-go](https://git.internal/PyxisOS/pyxis-go) fork. The separate
  [host build](toolchain/build-go.sh) retains upstream notices and stages its
  full `LICENSE` and exact revision inside the bundle. Ordinary images do not
  include Go or require this toolchain.
- `third_party/limine/`: the imported bootloader, protocol header and binaries
  retain [Limine's license](third_party/limine/LICENSE) and the protocol header's
  own embedded 0BSD notice. Third-party code inside the binaries is covered by
  Limine's [notices](third_party/limine/3RDPARTY.md), which accompany them.
- `third_party/tlsf/tlsf.c` and `tlsf.h`: retain the BSD terms and copyright
  notices in [tlsf.h](third_party/tlsf/tlsf.h). The Pyxis integration header and
  provenance documentation are original project material.
- `third_party/uacpi/`: the imported uACPI interpreter retains its
  [MIT license](third_party/uacpi/LICENSE) and
  [provenance](third_party/uacpi/UPSTREAM.md). The Caelum host interface in
  `kernel/acpi` is original project material.
- `third_party/siphash/`: imported reference code retains
  [CC0](third_party/siphash/LICENSE_CC0) and its provenance notices.
- `kernel/random/generator.c`: adapts OpenBSD's ISC-licensed `arc4random.c`;
  `kernel/random/chacha.c` adapts D. J. Bernstein's public-domain ChaCha core.
  Their pinned source provenance, local changes and complete retained notices
  are in [kernel/random/NOTICE](kernel/random/NOTICE). The kernel bundle includes
  that notice, and images stage it at `boot://share/licenses/kernel-random/NOTICE`.
- `kernel/wifi/`: the bounded AX200 implementation retains the permissive
  OpenBSD/Intel reference notices and local adaptation record in
  [kernel/wifi/NOTICE](kernel/wifi/NOTICE). Kernel bundles and archives carry
  that notice. The independent [Wi-Fi firmware pin](firmware/ax200-wifi/UPSTREAM.md)
  carries Intel binary terms, WHENCE and provenance under
  `boot://share/licenses/intel-wifi/`; those binaries are not vendored.
- AX200 Bluetooth firmware is fetched unchanged through the pinned
  [asset recipe](firmware/ax200/UPSTREAM.md). Intel's binary firmware terms
  govern those assets; the boot archive stages the complete
  `LICENCE.ibt_firmware`, WHENCE and provenance under
  `boot://share/licenses/intel-bluetooth/`. The firmware is not MPL-licensed
  kernel source and its binaries are not stored in this repository.
- `tools/remote/vendor/sha256.c` and `tools/remote/sha256.h`: the imported
  SHA-256 implementation/interface is public domain; see its retained notice
  and [source provenance](tools/remote/vendor/UPSTREAM.md).
- `arch/x86_64/amd/renoir_registers.h` the firmware inventory and bounded flip backend adapt AMD DC
  MIT register/table definitions, mono flip ordering and completion/state decoding.
  `arch/x86_64/amd/renoir_cursor_inventory.c` also adapts read-only cursor/clock
  definitions and reference-clock/VUPDATE candidate arithmetic; no cursor writes. Pinned provenance, local changes and complete
  retained notices are in [arch/x86_64/amd/NOTICE](arch/x86_64/amd/NOTICE); kernel
  bundles and images retain it at `boot://share/licenses/kernel-amd/NOTICE`.
- `kernel/fb/font.c`: Bizcat font data by Robey Pointer retains the CC BY 4.0
  attribution, source links and conversion notice in that file. The glyphs in
  `kernel/fb/bizcat_generated.inc` (box drawing, block elements and braille)
  are Pyxis work, drawn by `scripts/bizcat-glyphs.py` from Bizcat's cell
  geometry; the script only compares its light box characters with Bizcat's.
- `third_party/doom-shareware/`: game data retains its own
  [distribution terms](third_party/doom-shareware/LICENSE), independently of
  both Pyxis code and the Doom engine.
- `docs/userland/libc-probe/scratch.patch` contains sbase-derived code covered by
  [LICENSE.sbase](docs/userland/libc-probe/LICENSE.sbase).
- Kconfiglib is an externally installed, ISC-licensed host build dependency,
  pinned in `requirements.txt`, with no local changes. See
  [configuration setup](docs/development/configuration.md).
- `userspace`, `ports`, `third_party/lwip` and `fs` are separately versioned
  repositories. Their own licensing files and upstream notices govern their
  contents; the parent license does not override them.

Preserve the existing license and provenance of imported code and assets,
including local adaptations. Pyxis-authored provenance documents are covered by
MPL unless they carry a different notice. Built images, SDKs and toolchain
bundles contain components under several licenses; they are not wholly MPL.
