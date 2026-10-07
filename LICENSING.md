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

- `third_party/limine/`: the imported bootloader, protocol header and binaries
  retain [Limine's license](third_party/limine/LICENSE) and the protocol header's
  own embedded 0BSD notice.
- `third_party/tlsf/tlsf.c` and `tlsf.h`: retain the BSD terms and copyright
  notices in [tlsf.h](third_party/tlsf/tlsf.h). The Pyxis integration header and
  provenance documentation are original project material.
- `third_party/uacpi/`: the imported uACPI interpreter retains its
  [MIT license](third_party/uacpi/LICENSE) and
  [provenance](third_party/uacpi/UPSTREAM.md). The Caelum host interface in
  `kernel/acpi` is original project material.
- `third_party/siphash/`: imported reference code retains
  [CC0](third_party/siphash/LICENSE_CC0) and its provenance notices.
- `tools/remote/vendor/sha256.c` and `tools/remote/sha256.h`: the imported
  SHA-256 implementation/interface is public domain; see its retained notice
  and [source provenance](tools/remote/vendor/UPSTREAM.md).
- `kernel/fb/font.c`: Bizcat font data by Robey Pointer retains the CC BY 4.0
  attribution, source links and conversion notice in that file.
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
