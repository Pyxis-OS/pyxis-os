-- sort from the sbase port.
return {
  id = "sort",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { sort = { path = "sort.pxe" } },
  entries = {
    { build = "ports/sbase/stage/bin/sort.pxe", at = "sort.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
