return {
  id = "cmatrix",
  variant = "2.0",
  kind = "program",
  depends = {},
  commands = { cmatrix = { path = "cmatrix.pxb", command = "cmatrix" } },
  entries = {
    { build = "ports/cmatrix/stage/bin/cmatrix.pxb", at = "cmatrix.pxb" },
    { tree = "../../ports/cmatrix", at = "share/sources/pyxis-cmatrix" },
    { file = "../../ports/LICENSE", at = "licenses/pyxis-ports/LICENSE" },
    { file = "../../ports/LICENSING.md", at = "licenses/pyxis-ports/LICENSING.md" },
  },
}
