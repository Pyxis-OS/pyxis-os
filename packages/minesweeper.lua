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
  },
}
