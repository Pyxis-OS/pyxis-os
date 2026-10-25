# SoH private host extraction

Task 3, qualified on the Codex Fedora VM, 2026-10-25. This produces private
Linux-host archives; it does not run or qualify the Pyxis game/resource reader.
ROM bytes, derived assets, cache, logs and source adaptations remain under
`/shared/oot/`. No ROM/archive digest, member list or asset is published.

## Sources and build

| Source | Exact revision |
| --- | --- |
| SoH 9.3.1 | `f0599f96f8d442f26d0fa1c5cedafddacd56633f` |
| Torch | `2ab12fe9660aec04e02ee89fe81baed304a1a1d6` |
| LUS (matching shader inputs) | `da6daf1a50a5e46d6641408d52227e31e85e46df` |
| spdlog, bundled fmt | `486b55554f11c9cccc913e11a87085b2a91f706f` |
| tinyxml2 | `321ea883b7190d4e85cae5512a12e5eaa8f8731f` |
| yaml-cpp | `56e3bb550c91fd7005566f19c079cb7a503223cf` |
| zlib | `51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` |

Existing builder `pyxis-llvm23.1.3-3d69e83`, using its **Linux host** GCC 14.2,
CMake 3.31.6 and Ninja 1.12.1, with host zlib headers. No Pyxis SDK, cross link
or compiler-container adoption/rebuild. Cached source copies only, with CMake
`FETCHCONTENT_FULLY_DISCONNECTED=ON` and explicit source-directory overrides.
Writable private dependency copies avoid upstream zlib's source-tree rename;
the source cache remains unchanged.

Configure `SOH_TOOLS_ONLY=ON`, Release; build only `soh-torch` and
`soh-o2r-packer`. The root forces `PORT_VERSION_ENDIANNESS=ON`,
`ROM_CRC_BSWAP=OFF`, `USE_STANDALONE=OFF`, `BUILD_OOT=ON`; UI, StormLib,
NAudio and other games are off. Neither game nor LUS target is configured.
Generate `soh.o2r` using `GenerateSohOtr`, including the pinned LUS shaders;
run `soh-torch` with the pinned asset YAML, private destination and version
`9.3.1` against the authorised big-endian input.

The pristine wrapper failed compilation: its filename argument disagrees with
`SohTorch::Extract`'s owned-byte-vector signature. The private fix performs a
bounded binary read, checks complete/stable length and big-endian header, then
moves the bytes into that existing API. It accepts big-endian z64 input only;
there is no byte-order conversion claim. The patch and its SHA-256 are retained
in the private kit, outside the repository. The corrected build and unchanged
repeat build passed. Missing input refuses before creating an output directory.

## Output and independent reopening

Private kit: `/shared/oot/soh-9.3.1-task3/`. Its README, source-pin manifest,
`HOST-SHA256SUMS`, `host-wrapper.patch`, `build-host.sh` and
`run-extraction.py` retain the reproducible preparation. Outputs:
`archives/soh.o2r` and `archives/oot.o2r`. Creation refuses existing outputs;
logs and TMPDIR stay private, core dumps are disabled, and the verified
archives are left read-only.

Reopen the existing results on this VM:

```sh
python3 /shared/oot/soh-9.3.1-task3/run-extraction.py --verify-only
```

**Passed:** both ZIP/O2R archives reopen with Python's independent ZIP reader,
every member decompresses with a valid CRC, and both exact seven-byte
big-endian `portVersion` records decode as **9.3.1**. The game's five-byte
`version` matches the input header in the order consumed by LUS. No packer or
extractor warning/error lines. Detailed results and private hashes are kept
only in `verification.json` and private logs.

The game archive contains 24 repeated member entries, all byte-identical;
neither version record is duplicated. Validation rejects conflicting duplicate
contents and unsafe relative names. This is structural/version evidence, not
proof that every exported resource is semantically correct. Target LUS resource
decoding, game startup and visual comparison remain tasks 4–5. No native-machine
or performance claim is made by this host preparation.
