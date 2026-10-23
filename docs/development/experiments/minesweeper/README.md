# Minesweeper package qualification

2026-10-22: parent `515ab399f068` (base main `0bf27b3b`), userspace
`d0afd234`, ports `87d84ec8` (drawing backend `ec02c7be`). Ordinary builder
`pyxis-llvm23.1.3-3d69e83`; current SDK/kernel and selected SDL2, raylib,
Minesweeper and bootstrap/package helpers rebuilt. No compiler rebuild.

```sh
make -j8 -f scripts/ports.mk game-ports-minesweeper
python3 tools/pxpkg.py pack --build build packages/minesweeper.lua REPOSITORY
python3 tools/pxpkg.py check REPOSITORY
```

The public repository held `minesweeper@upstream-d5f81cc+515ab399f068`: 11
regular files, 1,182,208-byte package, including the flag PNG and game/raylib/SDL2
notices. QEMU fetched it over HTTP, installed it into the stock private package
pool after confirmation, verified the files/command index, and launched
`minesweeper` through ordinary command search. Neither the game nor development
packages were added to the boot archive.

Q35 nested KVM, four max CPUs, 4 GiB, OVMF, Bochs 1920x1080 (1920x1048 content),
VirtIO network/RNG, PS/2 input, no HDA or persistent disk. The game rendered its
600x600 logical canvas as an aspect-fit square. HMP input exercised flag/unflag,
flagged-cell protection, reveal/flood fill, loss, reset, a full win, Escape and
relaunch. Read-only GDB inspected the live board to select safe cells; all
reveals used real pointer input, with no game-state modification. WIN reported
203 revealed safe cells. Reset returned PLAYING with zero revealed cells.

Switching spaces with Super+Right/Left preserved a flag while R was pressed in
the other space; R reset on return. Left/right clicks in the letterbox changed
no cells. Missing flag data in a copied development bundle produced status 1
and returned to the shell; the ordinary package launched afterwards. Remote
launch refused the missing required display grant before entry. After Escape,
read-only GDB found no display/pointer owner and all three display slots null.

These are functional QEMU checks, not performance or native qualification.
The final package pass used parent `f290629b7f43`, the same dependency pins and
byte-identical game executable, adding the MPL license/terms, covered SDL backend
source and SDL/raylib provenance. The documented `make -j8 game-packages
GAMES=minesweeper REPOSITORY=...` passed; the resulting 26-file, 1,284,096-byte
package passed host checking and a fresh QEMU install/verify/command launch.
This boot used VirtIO GPU. Standard VNC ExtendedDesktopSize changed the real
output from 1280x800 to 1000x700 and 800x600; read-only GDB confirmed actual
1000x668 and 800x568 content. A flag placed at cell (0,0) before resizing could
be removed at its newly mapped position afterwards. Escape returned to the shell.
Raw screenshots, command captures and GDB inspection stay local under
`/shared/present/minesweeper/qualification/`.


## SDK-view integration (2026-10-23)

Stack checkpoint `d07b90f1`, ports `cd89600c` (includes audio `9d733e0`),
with the same public headers/runtime as the SDK/kernel test build `8bc5af9e`
(userland `6de2b65f`, main `69cceab3`, including #1147). The selected game was
rebuilt against the refreshed audio-enabled static library and the shared SDK.
Host packing/checking passed: Minesweeper 28 files / 1,298,432 bytes, including
MPL configuration/adapter sources; no runtime development-package dependency.

A fresh QEMU boot, with the same four-CPU/4-GiB VirtIO-GPU configuration as the
SDK-view raylib check, installed and verified the final program/dev packages.
The local test image additionally admitted `d07b90f1256a` and the published
compiler's `fecca0ee87b0` ABI; no such declaration is committed. Published Clang
compiled the packaged drawing example and existing audio fixture through the
installed SDK view with no search flags; the audio fixture played and reopened.
Minesweeper launched by command name, drew its board/flag PNG, accepted pointer
and reset input, and exited. Read-only GDB then found both owners and all three
display slots null. This refresh is a smoke check; the full win/loss/resize
qualification remains at its earlier revisions above. Native game/audio checks
were not run. Raw records: `/shared/present/raylib/sdk-view-20261023/`.


## Native vela (chronium, 2026-10-23)

Attended packet `6ea0659991d65f863f630b66aff7c736a0242e3d`, userland
`5bb8336b`, ports `ee3fceb9`, ordinary full-distribution image and current-SDK
game. Chronium reported drawing, clicks, R reset and Escape exit all passed.
The temporary bundle used the same game executable/assets as the public port;
this was native gameplay, not a native package-install or performance check.
Packet and QEMU preparation record:
`/shared/present/raylib/native-audio-minesweeper-20261023/`.
