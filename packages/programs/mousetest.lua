-- mousetest, a development tool from the Pyxis userland.
return {
  id = "mousetest",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { mousetest = { path = "mousetest.pxe" } },
  entries = {
    { build = "userspace-root/mousetest.pxe", at = "mousetest.pxe" },
  },
}
