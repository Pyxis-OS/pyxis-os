-- tcc from the tcc port.
return {
  id = "tcc",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { tcc = { path = "tcc.pxe" } },
  entries = {
    { build = "ports/tcc/stage/bin/tcc.pxe", at = "tcc.pxe" },
    { build = "ports/tcc/stage/share/licenses/tcc", at = "licenses/tcc" },
  },
}
