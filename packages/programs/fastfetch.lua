-- fastfetch from the fastfetch port.
return {
  id = "fastfetch",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { fastfetch = { path = "fastfetch.pxe" } },
  entries = {
    { build = "ports/fastfetch/stage/bin/fastfetch.pxe", at = "fastfetch.pxe" },
    { build = "ports/fastfetch/stage/share/licenses/fastfetch", at = "licenses/fastfetch" },
  },
}
