-- sleep from the Pyxis userland.
return {
  id = "sleep",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { sleep = { path = "sleep.pxe" } },
  entries = {
    { build = "userspace-root/sleep.pxe", at = "sleep.pxe" },
  },
}
