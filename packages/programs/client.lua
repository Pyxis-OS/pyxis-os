-- client, a development tool from the Pyxis userland.
return {
  id = "client",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { client = { path = "client.pxe" } },
  entries = {
    { build = "userspace-root/client.pxe", at = "client.pxe" },
  },
}
