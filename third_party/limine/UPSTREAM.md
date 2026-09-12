Limine bootloader: released **v12.9.0**, source commit
`34fe53c3d27d229ea25c28fc3e04db362543715b`.
https://github.com/Limine-Bootloader/Limine/releases/tag/v12.9.0

The unmodified x86_64 UEFI loader and UEFI CD image were extracted from:
https://github.com/Limine-Bootloader/Limine/releases/download/v12.9.0/limine-binary.tar.xz

SHA-256:
```
9a738586bff5790bd8bfef4a4868a2939cba3f81f22f121306d668c97f1c85d8  limine-binary.tar.xz
f24efeecf6cfd3e11dd47a8263fece74509ec91f83b7f1d166b8ca30892d629f  BOOTX64.EFI
b8cb0c037bfa69a315740f799f403cad251b87f8c6622ec40eb701cae10903bf  limine-uefi-cd.bin
```

Protocol header: unmodified `include/limine.h` from
https://github.com/Limine-Bootloader/limine-protocol/commit/da65184e91f80fcb397270121b1e2515a11e01ee
Its 0BSD license is embedded. The bootloader license is in LICENSE.
Caelum requests base revision **6**, supported by v12.9.0's
`SUPPORTED_BASE_REVISION`, and paging request revision 1 (min/max mode).
Matching references: `PROTOCOL.md` at the protocol commit and `CONFIG.md` at
the bootloader release tag. No network access is needed to build.
