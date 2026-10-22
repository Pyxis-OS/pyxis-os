-- tcp from the Pyxis userland.
return {
  id = "tcp",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { tcp = { path = "tcp.pxe" } },
  entries = {
    { build = "userspace-root/tcp.pxe", at = "tcp.pxe" },
  },
}
