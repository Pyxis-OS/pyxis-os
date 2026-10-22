-- init-fat32-write, a development tool from the Pyxis userland.
return {
  id = "init-fat32-write",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["init-fat32-write"] = { path = "init-fat32-write.pxe" } },
  entries = {
    { build = "userspace-root/init-fat32-write.pxe", at = "init-fat32-write.pxe" },
  },
}
