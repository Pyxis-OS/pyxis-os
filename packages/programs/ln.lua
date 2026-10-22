-- ln from the Pyxis userland.
return {
  id = "ln",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ln = { path = "ln.pxe" } },
  entries = {
    { build = "userspace-root/ln.pxe", at = "ln.pxe" },
  },
}
