return {
  id = "dfrotz",
  variant = "2.55",
  kind = "program",
  depends = {},
  commands = { dfrotz = { path = "dfrotz.pxb", command = "dfrotz" } },
  entries = {
    { build = "ports/dfrotz/stage/bin/dfrotz.pxb", at = "dfrotz.pxb" },
    { tree = "../../ports/dfrotz", at = "share/sources/pyxis-dfrotz" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}
