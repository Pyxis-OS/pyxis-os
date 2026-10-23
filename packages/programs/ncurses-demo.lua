return {
  id = "ncurses-demo",
  variant = "6.6",
  kind = "program",
  depends = {},
  commands = { ["ncurses-demo"] = { path = "ncurses-demo.pxb" } },
  entries = {
    { build = "ports/ncursesw/stage/bin/ncurses-demo.pxb", at = "ncurses-demo.pxb" },
    { tree = "../../ports/ncursesw", at = "share/sources/ncursesw" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
  },
}
