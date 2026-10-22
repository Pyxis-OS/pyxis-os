-- server, a development tool from the Pyxis userland.
return {
  id = "server",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { server = { path = "server.pxe" } },
  entries = {
    { build = "userspace-root/server.pxe", at = "server.pxe" },
  },
}
