-- One system's Minesweeper bundle and its application/library notices.
return {
  id = "minesweeper",
  variant = "upstream-d5f81cc",
  kind = "program",
  depends = {},
  commands = {
    minesweeper = { path = "minesweeper.pxb", command = "minesweeper" },
  },
  entries = {
    { build = "ports/minesweeper/stage/bin/minesweeper.pxb", at = "minesweeper.pxb" },
    { build = "ports/minesweeper/stage/share/licenses/minesweeper", at = "licenses/minesweeper" },
    { build = "ports/minesweeper/stage/share/minesweeper", at = "share/minesweeper" },
    { build = "ports/raylib/stage/dev/share/licenses/raylib", at = "licenses/raylib" },
    { build = "ports/sdl2/stage/dev/share/licenses/sdl2", at = "licenses/sdl2" },
    { build = "ports/sdl2/stage/dev/share/sdl2/source.txt", at = "share/sdl2/source.txt" },
    { build = "ports/raylib/stage/dev/share/raylib/source.txt", at = "share/raylib/source.txt" },
    { file = "../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
    { tree = "../ports/sdl2/pyxis", at = "sources/sdl2/pyxis" },
    { file = "../ports/sdl2/SDL_config.h", at = "sources/sdl2/SDL_config.h" },
    { file = "../ports/raylib/pyxis_config.h", at = "sources/raylib/pyxis_config.h" },
  },
}
