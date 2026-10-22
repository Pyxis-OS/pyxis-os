-- ping from the Pyxis userland.
return {
  id = "ping",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ping = { path = "ping.pxe" } },
  entries = {
    { build = "userspace-root/ping.pxe", at = "ping.pxe" },
  },
}
