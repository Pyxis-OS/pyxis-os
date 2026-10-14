Limine bootloader: released **v12.9.3**, source commit
`e3aa138d383ca4fe061f05931b5a54497cd502f2`.
https://github.com/Limine-Bootloader/Limine/releases/tag/v12.9.3

The unmodified x86_64 UEFI loader, UEFI CD image and third-party notices
(`3RDPARTY.md`, which must accompany the binaries) were extracted from:
https://github.com/Limine-Bootloader/Limine/releases/download/v12.9.3/limine-binary.tar.xz

The archive's detached signature (`limine-binary.tar.xz.sig`) verifies with
Mintsuki's key `05D2 9860 D0A0 668A AEFB 9D69 1F3C 021B ECA2 3821`, and its
SHA-256 matches the digest GitHub publishes for the release:
```
e02a473c6dc6df25159697f94930272397f21fd86e5d4eeee61bba72c95ec8d2  limine-binary.tar.xz
94308b4e9ee1783a65cfd60570240a80595baf125c5b18e729d3dd977194ad51  BOOTX64.EFI
55d02f44d25c8df0a1334133bc1a485a8de52eaf3a2c96ba84ed0e6d1436e974  limine-uefi-cd.bin
```

v12.9.3 fixes the page tables leaving out the highest memory map entry
([Limine issue #687](https://github.com/limine-bootloader/limine/issues/687)).
When the GOP framebuffer is that entry, as on laptops whose framebuffer lies
above all RAM, it is unmapped and Caelum's first early-console write faults
before any output. v12.9.1 also changed x86 AP start-up (a 32-bit TSS before
long mode) and I/O APIC register writes.

Protocol header: unmodified `include/limine.h` from
https://github.com/Limine-Bootloader/limine-protocol/commit/3a0526b700e356f0eac1b71a77697b3fd1c707a3,
the protocol commit v12.9.3 pins. The header is identical at the previous pin
`da65184e`; only `PROTOCOL.md` changed. Its 0BSD license is embedded. The
bootloader license is in LICENSE.
Caelum requests base revision **6**, supported by v12.9.3's
`SUPPORTED_BASE_REVISION`, and paging request revision 1 (min/max mode).
Matching references: `PROTOCOL.md` at the protocol commit and `CONFIG.md` at
the bootloader release tag. No network access is needed to build.
