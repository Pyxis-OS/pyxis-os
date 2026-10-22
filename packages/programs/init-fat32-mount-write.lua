-- init-fat32-mount-write, a development tool from the Pyxis userland.
return {
  id = "init-fat32-mount-write",
  variant = "1",
  kind = "program",
  depends = {},
  commands = { ["init-fat32-mount-write"] = { path = "init-fat32-mount-write.pxe" } },
  entries = {
    { build = "userspace-root/init-fat32-mount-write.pxe", at = "init-fat32-mount-write.pxe" },
  },
}
