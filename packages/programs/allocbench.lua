-- allocbench, a development tool from the Pyxis userland.
return {
  id = "allocbench",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { allocbench = { path = "allocbench.pxe" } },
  entries = {
    { build = "userspace-root/allocbench.pxe", at = "allocbench.pxe" },
  },
}
