-- cksum from the sbase port.
return {
  id = "cksum",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { cksum = { path = "cksum.pxe" } },
  entries = {
    { build = "ports/sbase/stage/bin/cksum.pxe", at = "cksum.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
