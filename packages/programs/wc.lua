-- wc from the sbase port.
return {
  id = "wc",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { wc = { path = "wc.pxe" } },
  entries = {
    { build = "ports/sbase/stage/bin/wc.pxe", at = "wc.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
