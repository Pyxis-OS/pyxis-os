# Packages archive cut

2026-10-19, slice 1 task 5. Baseline `c9303d87` (the task-4 payload merged
unchanged by main `840f80a1`); archive cut `ac586d2c` plus the SDK prerequisite
correction for the explicit font preparation target. Dependencies: userland
`d5f90549`, ports `e9b0eb4a`, unchanged fs `f3f22b6e` and lwIP `a1aadb91`.
Builder `pyxis-llvm23.1.3-3d69e83`; nested VM, ordinary default configuration,
`make -j8 image BOOT_MENU_TIMEOUT=5`. This is a byte-count comparison, not a
boot-time/performance benchmark.

| Artifact (bytes) | Before | After | Removed |
| --- | ---: | ---: | ---: |
| initrd.cpio | 105,754,112 | 70,097,408 | 35,656,704 |
| ISO | 116,867,072 | 81,211,392 | 35,655,680 |

The removed data totals 35,617,004 bytes: fonts/notices/provenance 12,731,749,
Doom shareware 4,196,020 and Quake shareware 18,689,235, plus archive records and
separately staged shareware notices. Other binaries, manifests and provenance
also change; the table reports complete artifacts, not an exact attribution of
every byte. Both default artifacts remain uncompressed.

## Package preparation

```sh
make font-assets
python3 tools/pxpkg.py pack packages/fonts.lua REPOSITORY
python3 tools/pxpkg.py pack packages/doom-shareware.lua REPOSITORY
python3 tools/pxpkg.py pack packages/quake-shareware.lua REPOSITORY
python3 tools/pxpkg.py check REPOSITORY
```

The font preparation fetched only the existing pinned cache releases and checked
the selected member hashes. All 33 packaged fonts, original notices and generated
provenance files matched the baseline bytes. Archives: fonts 12,776,960 bytes,
Doom shareware 4,211,200 and Quake shareware 18,717,696. The packer/checker accepted
all three recipes. No private game data was used or logged.

Both `DEVILUTIONX=1 EDUKE32=1` personal program opt-ins built without data inputs,
using the verified kernel/SDK/userland products and rebuilding the ports. The
personal initrd contained both bundles and neither data tree. A subsequent
ordinary assembly removed both opt-ins and returned to the default sizes above.
A nonempty retired `DOOM_WAD` input refused before opening the named file.

## QEMU observations

Q35/KVM, host CPU, four vCPUs, 2 GiB, OVMF, standard VGA at 1280×800,
VirtIO network/RNG, no disk or host filesystem. Stock live configuration with
its sparse npfs packages pool. A loopback-only host HTTP server supplied the
repository through QEMU user networking.

- Empty set: `font-min`, Doom and Quake refused launch naming their required
  missing packages. The archive had no fonts or game-data trees, and installed
  `font-min.pxb`, not the old bare executable.
- `pkg install fonts doom-data quake-data`, explicit `apply`, selected set 1.
  `pkg verify` checked every selected file and the command index successfully.
  The initial piped invocation cancelled at Review; the repeat used the
  documented persistent FIFO client and a separate confirmation.
- `font-min` rendered all nine styles, the Nerd gear glyph and pointer-position
  text, then Escape returned to the shell. Compiling the shipped source with
  TCC and `-lfont` into a new bundle using the shipped manifest also rendered
  all nine styles. Neither launch used a boot font path.
- Doom launched from its package, rendered the shareware demo level and quit
  normally back to the shell. Final `pkg status` still reported the three
  selected packages.

Raw command events, screenshots, build logs and size inputs remain under
`/shared/present/packages/task5-20261019/`; selected screenshots are preserved
under `/shared/present/screenshots-keep/packages/`. No new benchmark framework,
kernel instrumentation, native/PXE action or licensed-data qualification.
The other games, real toolchain/clangd, rollback and native slice qualification
remain task 7; task 4 has its own broader consumer checks.
