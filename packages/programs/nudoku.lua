return {
  id = "nudoku",
  variant = "8.0.1",
  kind = "program",
  depends = {},
  commands = { nudoku = { path = "nudoku.pxb", command = "nudoku" } },
  entries = {
    { build = "ports/nudoku/stage/bin/nudoku.pxb", at = "nudoku.pxb" },
    { tree = "../../ports/nudoku", at = "share/sources/pyxis-nudoku" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}
