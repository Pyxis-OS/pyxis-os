-- dig from the Pyxis userland.
return {
  id = "dig",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { dig = { path = "dig.pxe" } },
  entries = {
    { build = "userspace-root/dig.pxe", at = "dig.pxe" },
  },
}
