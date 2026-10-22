-- tree from the Pyxis userland.
return {
  id = "tree",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { tree = { path = "tree.pxe" } },
  entries = {
    { build = "userspace-root/tree.pxe", at = "tree.pxe" },
  },
}
