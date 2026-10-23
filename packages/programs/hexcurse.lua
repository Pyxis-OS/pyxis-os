return {
  id = "hexcurse",
  variant = "1.60.0",
  kind = "program",
  depends = {},
  commands = { hexcurse = { path = "hexcurse.pxb", command = "hexcurse" } },
  entries = {
    { build = "ports/hexcurse/stage/bin/hexcurse.pxb", at = "hexcurse.pxb" },
    { tree = "../../ports/hexcurse", at = "share/sources/pyxis-hexcurse" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}
