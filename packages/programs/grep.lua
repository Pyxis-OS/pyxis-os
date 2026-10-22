-- grep from the sbase port.
return {
  id = "grep",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { grep = { path = "grep.pxe" } },
  entries = {
    { build = "ports/sbase/stage/bin/grep.pxe", at = "grep.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
