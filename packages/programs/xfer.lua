-- xfer from the Pyxis userland.
return {
  id = "xfer",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { xfer = { path = "xfer.pxe" } },
  entries = {
    { build = "userspace-root/xfer.pxe", at = "xfer.pxe" },
    { build = "sdk/share/licenses/libsha256", at = "licenses/libsha256" },
  },
}
