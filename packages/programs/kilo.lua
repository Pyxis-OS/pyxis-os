-- kilo from the kilo port.
return {
  id = "kilo",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { kilo = { path = "kilo.pxe" } },
  entries = {
    { build = "ports/kilo/stage/bin/kilo.pxe", at = "kilo.pxe" },
    { build = "ports/kilo/stage/share/licenses/kilo", at = "licenses/kilo" },
  },
}
