-- remote-terminal from the Pyxis userland.
return {
  id = "remote-terminal",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["remote-terminal"] = { path = "remote-terminal.pxe" } },
  entries = {
    { build = "userspace-root/remote-terminal.pxe", at = "remote-terminal.pxe" },
  },
}
