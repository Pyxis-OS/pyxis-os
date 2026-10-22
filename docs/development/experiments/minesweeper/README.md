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
