-- tail from the sbase port.
return {
  id = "tail",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { tail = { path = "tail.pxe" } },
  entries = {
    { build = "ports/sbase/stage/bin/tail.pxe", at = "tail.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
