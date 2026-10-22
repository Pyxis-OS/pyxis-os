-- uniq from the sbase port.
return {
  id = "uniq",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { uniq = { path = "uniq.pxe" } },
  entries = {
    { build = "ports/sbase/stage/bin/uniq.pxe", at = "uniq.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
