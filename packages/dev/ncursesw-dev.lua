return {
  id = "ncursesw-dev",
  variant = "6.6",
  kind = "dev",
  depends = { { id = "pyxis-dev" } },
  entries = {
    { build = "ports/ncursesw/stage/dev", at = "" },
    { tree = "../../ports/ncursesw", at = "share/sources/ncursesw" },
    { file = "../../ports/LICENSE", at = "share/licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "share/licenses/pyxis-ports/LICENSING.md" },
  },
}
