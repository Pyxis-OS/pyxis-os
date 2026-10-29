return {
  id = "vitetris",
  variant = "0.59.1",
  kind = "program",
  depends = {},
  commands = { vitetris = { path = "vitetris.pxb", command = "vitetris" } },
  entries = {
    { build = "ports/vitetris/stage/bin/vitetris.pxb", at = "vitetris.pxb" },
    { tree = "../../ports/vitetris", at = "share/sources/pyxis-vitetris" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}
