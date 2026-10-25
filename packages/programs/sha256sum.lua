-- sha256sum from the sbase port.
return {
  id = "sha256sum",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { sha256sum = { path = "sha256sum.pxe" } },
  entries = {
    { build = "sdk/share/licenses/libsha256", at = "licenses/libsha256" },
    { build = "ports/sbase/stage/bin/sha256sum.pxe", at = "sha256sum.pxe" },
    { build = "ports/sbase/stage/share/licenses/sbase", at = "licenses/sbase" },
  },
}
