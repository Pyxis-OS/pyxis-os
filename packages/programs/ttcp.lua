-- ttcp, a development tool from the Pyxis userland.
return {
  id = "ttcp",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ttcp = { path = "ttcp.pxe" } },
  entries = {
    { build = "userspace-root/ttcp.pxe", at = "ttcp.pxe" },
  },
}
