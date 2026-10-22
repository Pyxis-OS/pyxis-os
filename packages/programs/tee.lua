-- tee from the sbase port.
return {
  id = "tee",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { tee = { path = "tee.pxe" } },
  entries = {
    { build = "ports/sbase/stage/bin/tee.pxe", at = "tee.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
