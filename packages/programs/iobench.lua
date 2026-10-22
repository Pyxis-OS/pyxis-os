-- iobench, a development tool from the Pyxis userland.
return {
  id = "iobench",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { iobench = { path = "iobench.pxe" } },
  entries = {
    { build = "userspace-root/iobench.pxe", at = "iobench.pxe" },
  },
}
